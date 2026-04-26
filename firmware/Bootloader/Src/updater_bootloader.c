#include "updater_bootloader.h"

#include "stm32f7xx_hal.h"
#include "updater_shared.h"

#include <string.h>

typedef struct {
  bool active;
  uint32_t image_size;
  uint32_t aligned_size;
  uint32_t image_crc32;
  updater_fw_version_t fw_version;
  uint32_t next_offset;
} updater_session_t;

static updater_session_t s_session;
static uint8_t s_last_request[UPDATER_PACKET_SIZE];
static uint8_t s_last_response[UPDATER_PACKET_SIZE];
static bool s_last_exchange_valid = false;
static volatile bool s_pending_jump_after_response = false;
static volatile bool s_jump_to_app = false;

static void updater_response_init(const updater_packet_t *request,
                                  updater_packet_t *response) {
  memset(response, 0, sizeof(*response));
  response->command = request->command;
  response->sequence = request->sequence;
  response->status = UPDATER_STATUS_OK;
  response->offset = s_session.next_offset;
}

static void updater_response_set_progress(updater_packet_t *response,
                                          uint32_t accepted_length) {
  updater_progress_payload_t progress = {
      .next_offset = s_session.next_offset,
      .accepted_length = accepted_length,
      .flags = s_session.active ? UPDATER_FLAG_SESSION_ACTIVE : 0u,
      .reserved = 0u,
  };

  memcpy(response->payload, &progress, sizeof(progress));
  response->length = sizeof(progress);
  response->offset = s_session.next_offset;
}

static void updater_cache_exchange(const uint8_t *request,
                                   const uint8_t *response) {
  memcpy(s_last_request, request, sizeof(s_last_request));
  memcpy(s_last_response, response, sizeof(s_last_response));
  s_last_exchange_valid = true;
}

static bool updater_request_is_duplicate(const uint8_t *request) {
  return s_last_exchange_valid &&
         (memcmp(s_last_request, request, sizeof(s_last_request)) == 0);
}

static void flash_clear_status_flags(void) {
  __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                         FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR |
                         FLASH_FLAG_ERSERR);
}

static bool flash_erase_application_slot(void) {
  FLASH_EraseInitTypeDef erase = {0};
  uint32_t sector_error = 0;
  bool success = true;

  HAL_FLASH_Unlock();
  flash_clear_status_flags();

  erase.TypeErase = FLASH_TYPEERASE_SECTORS;
  erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;
  erase.Sector = FLASH_SECTOR_4;
  erase.NbSectors = 3;

  if (HAL_FLASHEx_Erase(&erase, &sector_error) != HAL_OK) {
    success = false;
  }

  HAL_FLASH_Lock();
  return success;
}

static bool flash_program_words(uint32_t address, const uint8_t *data,
                                uint32_t len) {
  bool success = true;

  if ((address % UPDATER_FLASH_WRITE_ALIGN) != 0u ||
      (len % UPDATER_FLASH_WRITE_ALIGN) != 0u) {
    return false;
  }

  HAL_FLASH_Unlock();
  flash_clear_status_flags();

  for (uint32_t i = 0; i < len && success; i += 4u) {
    uint32_t word;
    memcpy(&word, data + i, sizeof(word));
    if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, address + i, word) !=
        HAL_OK) {
      success = false;
    }
  }

  HAL_FLASH_Lock();
  return success;
}

static void handle_hello(const updater_packet_t *request,
                         updater_packet_t *response) {
  updater_hello_payload_t hello = {0};

  (void)request;

  hello.protocol_version = UPDATER_PROTOCOL_VERSION;
  hello.app_base = UPDATER_APP_BASE;
  hello.app_max_size = UPDATER_APP_MAX_IMAGE_SIZE;
  hello.write_align = UPDATER_FLASH_WRITE_ALIGN;

  if (updater_is_app_image_valid()) {
    updater_fw_version_t installed = updater_get_app_version();
    hello.flags |= UPDATER_FLAG_APP_VALID;
    hello.installed_fw_version_major = installed.major;
    hello.installed_fw_version_minor = installed.minor;
    hello.installed_fw_version_patch = installed.patch;
  }
  if (s_session.active) {
    hello.flags |= UPDATER_FLAG_SESSION_ACTIVE;
  }

  memcpy(response->payload, &hello, sizeof(hello));
  response->length = sizeof(hello);
}

static void handle_begin(const updater_packet_t *request,
                         updater_packet_t *response) {
  updater_begin_request_t begin = {0};

  if (request->length < sizeof(begin)) {
    response->status = UPDATER_STATUS_INVALID_PARAMETER;
    return;
  }

  memcpy(&begin, request->payload, sizeof(begin));
  if ((begin.image_size == 0u) ||
      (begin.image_size > UPDATER_APP_MAX_IMAGE_SIZE)) {
    response->status = UPDATER_STATUS_INVALID_PARAMETER;
    return;
  }

  if (!flash_erase_application_slot()) {
    response->status = UPDATER_STATUS_ERROR;
    return;
  }

  memset(&s_session, 0, sizeof(s_session));
  s_session.active = true;
  s_session.image_size = begin.image_size;
  s_session.aligned_size =
      updater_align_up(begin.image_size, UPDATER_FLASH_WRITE_ALIGN);
  s_session.image_crc32 = begin.image_crc32;
  s_session.fw_version.major = begin.fw_version_major;
  s_session.fw_version.minor = begin.fw_version_minor;
  s_session.fw_version.patch = begin.fw_version_patch;
  s_session.next_offset = 0u;
  s_pending_jump_after_response = false;
  s_jump_to_app = false;

  updater_response_set_progress(response, 0u);
}

static void handle_data(const updater_packet_t *request,
                        updater_packet_t *response) {
  uint32_t flash_address;

  if (!s_session.active) {
    response->status = UPDATER_STATUS_INVALID_STATE;
    updater_response_set_progress(response, 0u);
    return;
  }

  if ((request->length == 0u) || (request->length > UPDATER_PAYLOAD_SIZE) ||
      ((request->offset % UPDATER_FLASH_WRITE_ALIGN) != 0u) ||
      ((request->length % UPDATER_FLASH_WRITE_ALIGN) != 0u)) {
    response->status = UPDATER_STATUS_INVALID_PARAMETER;
    updater_response_set_progress(response, 0u);
    return;
  }

  if (request->offset != s_session.next_offset) {
    response->status = UPDATER_STATUS_INVALID_STATE;
    updater_response_set_progress(response, 0u);
    return;
  }

  if ((request->offset + request->length) > s_session.aligned_size) {
    response->status = UPDATER_STATUS_INVALID_PARAMETER;
    updater_response_set_progress(response, 0u);
    return;
  }

  flash_address = UPDATER_APP_BASE + request->offset;
  if (!flash_program_words(flash_address, request->payload, request->length)) {
    response->status = UPDATER_STATUS_ERROR;
    updater_response_set_progress(response, 0u);
    return;
  }

  s_session.next_offset += request->length;
  updater_response_set_progress(response, request->length);
}

static void handle_finish(const updater_packet_t *request,
                          updater_packet_t *response) {
  updater_trailer_t trailer;
  uint32_t computed_crc;

  (void)request;

  if (!s_session.active) {
    response->status = UPDATER_STATUS_INVALID_STATE;
    updater_response_set_progress(response, 0u);
    return;
  }

  if (s_session.next_offset != s_session.aligned_size) {
    response->status = UPDATER_STATUS_INVALID_STATE;
    updater_response_set_progress(response, 0u);
    return;
  }

  if (!updater_is_app_vector_valid(UPDATER_APP_BASE)) {
    response->status = UPDATER_STATUS_INVALID_IMAGE;
    updater_response_set_progress(response, 0u);
    return;
  }

  computed_crc =
      updater_crc32_compute((const void *)UPDATER_APP_BASE, s_session.image_size);
  if (computed_crc != s_session.image_crc32) {
    response->status = UPDATER_STATUS_VERIFY_FAILED;
    updater_response_set_progress(response, 0u);
    return;
  }

  updater_trailer_prepare(&trailer, s_session.image_size, s_session.image_crc32,
                          s_session.fw_version);
  if (!flash_program_words(UPDATER_TRAILER_ADDR, (const uint8_t *)&trailer,
                           updater_align_up(sizeof(trailer),
                                            UPDATER_FLASH_WRITE_ALIGN))) {
    response->status = UPDATER_STATUS_ERROR;
    updater_response_set_progress(response, 0u);
    return;
  }

  s_session.active = false;
  updater_response_set_progress(response, 0u);
}

static void handle_abort(const updater_packet_t *request,
                         updater_packet_t *response) {
  (void)request;

  memset(&s_session, 0, sizeof(s_session));
  s_pending_jump_after_response = false;
  s_jump_to_app = false;
  updater_response_set_progress(response, 0u);
}

static void handle_boot(const updater_packet_t *request,
                        updater_packet_t *response) {
  (void)request;

  if (!updater_is_app_image_valid()) {
    response->status = UPDATER_STATUS_INVALID_IMAGE;
    return;
  }

  s_pending_jump_after_response = true;
}

void updater_bootloader_init(void) {
  memset(&s_session, 0, sizeof(s_session));
  memset(s_last_request, 0, sizeof(s_last_request));
  memset(s_last_response, 0, sizeof(s_last_response));
  s_last_exchange_valid = false;
  s_pending_jump_after_response = false;
  s_jump_to_app = false;
}

bool updater_bootloader_process_packet(const uint8_t *request,
                                       uint8_t *response) {
  const updater_packet_t *req = (const updater_packet_t *)request;
  updater_packet_t *resp = (updater_packet_t *)response;

  if (updater_request_is_duplicate(request)) {
    memcpy(response, s_last_response, sizeof(s_last_response));
    return true;
  }

  updater_response_init(req, resp);

  switch ((updater_command_t)req->command) {
  case UPDATER_CMD_HELLO:
    handle_hello(req, resp);
    break;

  case UPDATER_CMD_BEGIN:
    handle_begin(req, resp);
    break;

  case UPDATER_CMD_DATA:
    handle_data(req, resp);
    break;

  case UPDATER_CMD_FINISH:
    handle_finish(req, resp);
    break;

  case UPDATER_CMD_ABORT:
    handle_abort(req, resp);
    break;

  case UPDATER_CMD_BOOT:
    handle_boot(req, resp);
    break;

  default:
    resp->status = UPDATER_STATUS_INVALID_COMMAND;
    break;
  }

  resp->offset = s_session.next_offset;
  updater_cache_exchange(request, response);
  return true;
}

void updater_bootloader_notify_response_sent(void) {
  if (s_pending_jump_after_response) {
    s_pending_jump_after_response = false;
    s_jump_to_app = true;
  }
}

bool updater_bootloader_should_jump_to_app(void) { return s_jump_to_app; }
