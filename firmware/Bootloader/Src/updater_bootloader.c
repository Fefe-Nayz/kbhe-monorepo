#include "updater_bootloader.h"

#include "monocypher-ed25519.h"
#include "stm32f7xx_hal.h"
#include "updater_shared.h"
#include "updater_validation.h"
#include "updater_version_floor.h"

#include <string.h>

typedef struct __attribute__((packed)) {
  updater_signature_manifest_t manifest;
  uint8_t signature[UPDATER_SIGNATURE_SIZE];
} updater_auth_blob_t;

typedef struct {
  updater_auth_blob_t auth;
  uint16_t auth_received;
  bool authorized;
  bool active;
  uint32_t image_size;
  uint32_t aligned_size;
  uint32_t image_crc32;
  updater_fw_version_t fw_version;
  uint32_t next_offset;
} updater_session_t;

_Static_assert(sizeof(updater_signature_manifest_t) ==
                   UPDATER_SIGNATURE_MANIFEST_SIZE,
               "firmware signature manifest layout changed");
_Static_assert(sizeof(updater_auth_blob_t) == UPDATER_AUTH_BLOB_SIZE,
               "firmware authorization blob layout changed");

static updater_session_t s_session;
static uint8_t s_last_request[UPDATER_PACKET_SIZE];
static uint8_t s_last_response[UPDATER_PACKET_SIZE];
static bool s_last_exchange_valid = false;
static volatile bool s_pending_jump_after_response = false;
static volatile bool s_jump_to_app = false;

#if defined(UPDATER_HOST_TEST)
extern const void *updater_host_flash_address(uint32_t address, uint32_t len);
#endif

static const void *flash_read_address(uint32_t address, uint32_t len) {
#if defined(UPDATER_HOST_TEST)
  return updater_host_flash_address(address, len);
#else
  (void)len;
  return (const void *)(uintptr_t)address;
#endif
}

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
  uint32_t flags = UPDATER_FLAG_SIGNATURE_REQUIRED;
  if (s_session.active) {
    flags |= UPDATER_FLAG_SESSION_ACTIVE;
  }
  if (s_session.authorized) {
    flags |= UPDATER_FLAG_MANIFEST_AUTHENTICATED;
  }
  updater_progress_payload_t progress = {
      .next_offset = s_session.next_offset,
      .accepted_length = accepted_length,
      .flags = flags,
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
  erase.NbSectors = 2;

  if (HAL_FLASHEx_Erase(&erase, &sector_error) != HAL_OK) {
    success = false;
  }

  HAL_FLASH_Lock();
  return success;
}

static bool flash_program_words(uint32_t address, const uint8_t *data,
                                uint32_t len) {
  bool success = true;

  if (data == NULL || len == 0u ||
      (address % UPDATER_FLASH_WRITE_ALIGN) != 0u ||
      (len % UPDATER_FLASH_WRITE_ALIGN) != 0u ||
      address < UPDATER_APP_BASE || address > UPDATER_APP_SLOT_END ||
      len > (UPDATER_APP_SLOT_END - address)) {
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
  updater_fw_version_t installed = {0};

  (void)request;

  hello.protocol_version = UPDATER_PROTOCOL_VERSION;
  hello.flags = UPDATER_FLAG_SIGNATURE_REQUIRED;
  hello.app_base = UPDATER_APP_BASE;
  hello.app_max_size = UPDATER_APP_MAX_IMAGE_SIZE;
  hello.write_align = UPDATER_FLASH_WRITE_ALIGN;

  if (updater_read_valid_app_version(&installed) &&
      updater_version_floor_allows(installed)) {
    hello.flags |= UPDATER_FLAG_APP_VALID;
    hello.installed_fw_version_major = installed.major;
    hello.installed_fw_version_minor = installed.minor;
    hello.installed_fw_version_patch = installed.patch;
  }
  if (s_session.active) {
    hello.flags |= UPDATER_FLAG_SESSION_ACTIVE;
  }
  if (s_session.authorized) {
    hello.flags |= UPDATER_FLAG_MANIFEST_AUTHENTICATED;
  }

  memcpy(response->payload, &hello, sizeof(hello));
  response->length = sizeof(hello);
}

static void handle_begin(const updater_packet_t *request,
                         updater_packet_t *response) {
  updater_begin_request_t begin = {0};
  uint32_t aligned_size = 0u;

  if (request->length != sizeof(begin) || s_session.active) {
    response->status = s_session.active ? UPDATER_STATUS_INVALID_STATE
                                        : UPDATER_STATUS_INVALID_PARAMETER;
    return;
  }
  if (!s_session.authorized) {
    response->status = UPDATER_STATUS_AUTH_REQUIRED;
    return;
  }

  memcpy(&begin, request->payload, sizeof(begin));
  if (!updater_signature_manifest_is_sane(&s_session.auth.manifest) ||
      begin.reserved != 0u ||
      begin.image_size != s_session.auth.manifest.image_size ||
      begin.image_crc32 != s_session.auth.manifest.image_crc32 ||
      begin.fw_version_major != s_session.auth.manifest.fw_version_major ||
      begin.fw_version_minor != s_session.auth.manifest.fw_version_minor ||
      begin.fw_version_patch != s_session.auth.manifest.fw_version_patch) {
    response->status = UPDATER_STATUS_INVALID_PARAMETER;
    return;
  }

  if (!updater_align_up_checked(begin.image_size, UPDATER_FLASH_WRITE_ALIGN,
                                &aligned_size) ||
      aligned_size > UPDATER_APP_MAX_IMAGE_SIZE) {
    response->status = UPDATER_STATUS_INVALID_PARAMETER;
    return;
  }

  updater_fw_version_t candidate = {
      .major = begin.fw_version_major,
      .minor = begin.fw_version_minor,
      .patch = begin.fw_version_patch,
  };
  updater_version_floor_result_t floor_result =
      updater_version_floor_prepare(candidate);
  if (floor_result == UPDATER_VERSION_FLOOR_ROLLBACK) {
    response->status = UPDATER_STATUS_ROLLBACK_REJECTED;
    return;
  }
  if (floor_result != UPDATER_VERSION_FLOOR_OK) {
    response->status = UPDATER_STATUS_STORAGE_ERROR;
    return;
  }

  if (!flash_erase_application_slot()) {
    response->status = UPDATER_STATUS_ERROR;
    return;
  }

  s_session.active = true;
  s_session.image_size = begin.image_size;
  s_session.aligned_size = aligned_size;
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

  if (request->offset > s_session.aligned_size ||
      request->length > (s_session.aligned_size - request->offset)) {
    response->status = UPDATER_STATUS_INVALID_PARAMETER;
    updater_response_set_progress(response, 0u);
    return;
  }
  if (!updater_data_padding_is_canonical(
          s_session.image_size, request->offset, request->payload,
          request->length)) {
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

static void handle_auth(const updater_packet_t *request,
                        updater_packet_t *response) {
  uint8_t *auth_bytes = (uint8_t *)&s_session.auth;
  uint32_t remaining = 0u;

  if (s_session.active) {
    response->status = UPDATER_STATUS_INVALID_STATE;
    updater_response_set_progress(response, 0u);
    return;
  }

  /* Offset zero starts a fresh authorization attempt. Duplicate packets are
   * served from the exchange cache before reaching this handler. */
  if (request->offset == 0u) {
    crypto_wipe(&s_session.auth, sizeof(s_session.auth));
    s_session.auth_received = 0u;
    s_session.authorized = false;
  }

  remaining = UPDATER_AUTH_BLOB_SIZE - s_session.auth_received;
  if (request->length == 0u || request->length > remaining ||
      request->offset != s_session.auth_received) {
    response->status = UPDATER_STATUS_INVALID_PARAMETER;
    updater_response_set_progress(response, 0u);
    return;
  }

  memcpy(&auth_bytes[s_session.auth_received], request->payload,
         request->length);
  s_session.auth_received += request->length;

  if (s_session.auth_received == UPDATER_AUTH_BLOB_SIZE) {
    if (!updater_signature_manifest_verify(&s_session.auth.manifest,
                                           s_session.auth.signature)) {
      crypto_wipe(&s_session.auth, sizeof(s_session.auth));
      s_session.auth_received = 0u;
      response->status = UPDATER_STATUS_AUTH_FAILED;
      updater_response_set_progress(response, 0u);
      return;
    }
    updater_trailer_t installed;
    if (updater_read_trailer(&installed) &&
        updater_is_app_image_valid_with_trailer(&installed)) {
      bool strictly_newer = updater_version_is_strictly_newer(
          s_session.auth.manifest.fw_version_major,
          s_session.auth.manifest.fw_version_minor,
          s_session.auth.manifest.fw_version_patch,
          installed.fw_version_major, installed.fw_version_minor,
          installed.fw_version_patch);
      bool same_version =
          s_session.auth.manifest.fw_version_major ==
              installed.fw_version_major &&
          s_session.auth.manifest.fw_version_minor ==
              installed.fw_version_minor &&
          s_session.auth.manifest.fw_version_patch ==
              installed.fw_version_patch;
      /* The signed one-shot migrator and its final application intentionally
       * share one release version. Permit that equal-version replacement only
       * while the currently authenticated image carries a fully valid
       * KBHEMIG3 descriptor. Once the normal application trailer is installed,
       * strict anti-rollback applies again. */
      bool replacing_migrator =
          same_version &&
          updater_app_has_valid_migration_descriptor(&installed);
      if (!strictly_newer && !replacing_migrator) {
        crypto_wipe(&s_session.auth, sizeof(s_session.auth));
        s_session.auth_received = 0u;
        response->status = UPDATER_STATUS_ROLLBACK_REJECTED;
        updater_response_set_progress(response, 0u);
        return;
      }
    }
    s_session.authorized = true;
  }
  updater_response_set_progress(response, request->length);
}

static void handle_finish(const updater_packet_t *request,
                          updater_packet_t *response) {
  updater_trailer_t trailer;
  updater_signature_manifest_t manifest;
  const void *image;
  uint32_t computed_crc;
  uint32_t trailer_write_size = 0u;

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

  if (!s_session.authorized) {
    response->status = UPDATER_STATUS_AUTH_REQUIRED;
    updater_response_set_progress(response, 0u);
    return;
  }

  if (!updater_is_app_vector_valid_for_image(UPDATER_APP_BASE,
                                              s_session.image_size)) {
    response->status = UPDATER_STATUS_INVALID_IMAGE;
    updater_response_set_progress(response, 0u);
    return;
  }

  image = flash_read_address(UPDATER_APP_BASE, s_session.image_size);
  if (image == NULL) {
    response->status = UPDATER_STATUS_ERROR;
    updater_response_set_progress(response, 0u);
    return;
  }
  computed_crc = updater_crc32_compute(image, s_session.image_size);
  if (computed_crc != s_session.image_crc32) {
    response->status = UPDATER_STATUS_VERIFY_FAILED;
    updater_response_set_progress(response, 0u);
    return;
  }

  updater_signature_manifest_prepare(
      &manifest, image, s_session.image_size, s_session.image_crc32,
      s_session.fw_version);

  if (memcmp(&manifest, &s_session.auth.manifest, sizeof(manifest)) != 0 ||
      !updater_signature_manifest_verify(&manifest,
                                         s_session.auth.signature)) {
    crypto_wipe(&manifest, sizeof(manifest));
    response->status = UPDATER_STATUS_AUTH_FAILED;
    updater_response_set_progress(response, 0u);
    return;
  }
  crypto_wipe(&manifest, sizeof(manifest));

  updater_trailer_prepare(&trailer, s_session.image_size, s_session.image_crc32,
                          s_session.fw_version, s_session.auth.signature);
  if (!updater_align_up_checked(sizeof(trailer), UPDATER_FLASH_WRITE_ALIGN,
                                &trailer_write_size)) {
    response->status = UPDATER_STATUS_ERROR;
    updater_response_set_progress(response, 0u);
    return;
  }
  if (!flash_program_words(UPDATER_TRAILER_ADDR, (const uint8_t *)&trailer,
                           trailer_write_size)) {
    response->status = UPDATER_STATUS_ERROR;
    updater_response_set_progress(response, 0u);
    return;
  }

  s_session.active = false;
  s_session.authorized = false;
  s_session.auth_received = 0u;
  crypto_wipe(&s_session.auth, sizeof(s_session.auth));
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
  updater_fw_version_t installed = {0};
  (void)request;

  if (!updater_read_valid_app_version(&installed) ||
      !updater_version_floor_allows(installed)) {
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
                                       uint16_t request_len,
                                       uint8_t *response) {
  const updater_packet_t *req = NULL;
  updater_packet_t *resp = (updater_packet_t *)response;

  if (response == NULL) {
    return false;
  }
  memset(response, 0, UPDATER_PACKET_SIZE);
  if (request == NULL || request_len != UPDATER_PACKET_SIZE) {
    if (request != NULL && request_len > 0u) {
      resp->command = request[0];
      if (request_len > 1u) {
        resp->sequence = request[1];
      }
    }
    resp->status = UPDATER_STATUS_INVALID_PARAMETER;
    return true;
  }
  req = (const updater_packet_t *)request;

  if (updater_request_is_duplicate(request)) {
    memcpy(response, s_last_response, sizeof(s_last_response));
    return true;
  }

  updater_response_init(req, resp);

  if (req->length > UPDATER_PAYLOAD_SIZE) {
    resp->status = UPDATER_STATUS_INVALID_PARAMETER;
    updater_cache_exchange(request, response);
    return true;
  }

  switch ((updater_command_t)req->command) {
  case UPDATER_CMD_HELLO:
    if (req->length == 0u) {
      handle_hello(req, resp);
    } else {
      resp->status = UPDATER_STATUS_INVALID_PARAMETER;
    }
    break;

  case UPDATER_CMD_BEGIN:
    handle_begin(req, resp);
    break;

  case UPDATER_CMD_DATA:
    handle_data(req, resp);
    break;

  case UPDATER_CMD_FINISH:
    if (req->length == 0u) {
      handle_finish(req, resp);
    } else {
      resp->status = UPDATER_STATUS_INVALID_PARAMETER;
    }
    break;

  case UPDATER_CMD_ABORT:
    if (req->length == 0u) {
      handle_abort(req, resp);
    } else {
      resp->status = UPDATER_STATUS_INVALID_PARAMETER;
    }
    break;

  case UPDATER_CMD_BOOT:
    if (req->length == 0u) {
      handle_boot(req, resp);
    } else {
      resp->status = UPDATER_STATUS_INVALID_PARAMETER;
    }
    break;

  case UPDATER_CMD_AUTH:
    handle_auth(req, resp);
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
