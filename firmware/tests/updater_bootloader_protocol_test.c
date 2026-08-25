#include "updater_bootloader.h"
#include "firmware_public_key.h"
#include "monocypher-ed25519.h"
#include "updater_migration.h"
#include "updater_shared.h"
#include "updater_version_floor.h"

#include "stm32f7xx_hal.h"

#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef RELEASE_SIGNING_VECTOR_PATH
#error "RELEASE_SIGNING_VECTOR_PATH must name the shared JSON test vector"
#endif

#ifdef NDEBUG
#error "updater protocol checks must never be compiled out"
#endif

#define FIXTURE_IMAGE_SIZE 64u
#define MIGRATION_FIXTURE_IMAGE_SIZE 256u

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__,      \
              #condition);                                                     \
      exit(EXIT_FAILURE);                                                      \
    }                                                                          \
  } while (0)

typedef struct __attribute__((packed)) {
  updater_signature_manifest_t manifest;
  uint8_t signature[UPDATER_SIGNATURE_SIZE];
} test_auth_blob_t;

typedef struct {
  uint8_t image[FIXTURE_IMAGE_SIZE];
  updater_signature_manifest_t manifest;
  uint8_t signature[UPDATER_SIGNATURE_SIZE];
} signing_fixture_t;

static uint8_t s_flash[UPDATER_APP_SLOT_SIZE];
static uint8_t s_floor_flash[UPDATER_VERSION_FLOOR_SIZE];
static bool s_flash_unlocked;
static bool s_fail_next_erase;
static bool s_fail_next_program;
static bool s_fail_next_floor_program;
static uint32_t s_erase_attempts;
static uint32_t s_erase_successes;
static uint32_t s_program_attempts;
static uint32_t s_program_successes;
static uint8_t s_next_sequence;
static const uint8_t s_test_signing_seed[32] = {
    0x9Du, 0x61u, 0xB1u, 0x9Du, 0xEFu, 0xFDu, 0x5Au, 0x60u,
    0xBAu, 0x84u, 0x4Au, 0xF4u, 0x92u, 0xECu, 0x2Cu, 0xC4u,
    0x44u, 0x49u, 0xC5u, 0x69u, 0x7Bu, 0x32u, 0x69u, 0x19u,
    0x70u, 0x3Bu, 0xACu, 0x03u, 0x1Cu, 0xAEu, 0x7Fu, 0x60u,
};

static void sign_test_manifest(updater_signature_manifest_t *manifest,
                               uint8_t signature[UPDATER_SIGNATURE_SIZE]) {
  uint8_t seed[sizeof(s_test_signing_seed)];
  uint8_t secret_key[64];
  uint8_t public_key[32];
  memcpy(seed, s_test_signing_seed, sizeof(seed));
  crypto_ed25519_key_pair(secret_key, public_key, seed);
  CHECK(memcmp(public_key, KBHE_FIRMWARE_RELEASE_PUBLIC_KEY,
               sizeof(public_key)) == 0);
  crypto_ed25519_sign(signature, secret_key, (const uint8_t *)manifest,
                      sizeof(*manifest));
  crypto_wipe(seed, sizeof(seed));
  crypto_wipe(secret_key, sizeof(secret_key));
}

static bool flash_range_is_valid(uint32_t address, uint32_t len) {
  if (address < UPDATER_APP_BASE || len > UPDATER_APP_SLOT_SIZE) {
    return false;
  }
  uint32_t offset = address - UPDATER_APP_BASE;
  return offset <= UPDATER_APP_SLOT_SIZE - len;
}

const void *updater_host_flash_address(uint32_t address, uint32_t len) {
  if (!flash_range_is_valid(address, len)) {
    return NULL;
  }
  return &s_flash[address - UPDATER_APP_BASE];
}

uint32_t updater_version_floor_test_read_word(uint32_t address) {
  uint32_t value;
  uint32_t offset;
  CHECK(address >= UPDATER_VERSION_FLOOR_BASE);
  offset = address - UPDATER_VERSION_FLOOR_BASE;
  CHECK(offset <= sizeof(s_floor_flash) - sizeof(value));
  CHECK((offset & 3u) == 0u);
  memcpy(&value, &s_floor_flash[offset], sizeof(value));
  return value;
}

bool updater_version_floor_test_program_word(uint32_t address,
                                             uint32_t value) {
  uint32_t current;
  uint32_t offset;
  CHECK(address >= UPDATER_VERSION_FLOOR_BASE);
  offset = address - UPDATER_VERSION_FLOOR_BASE;
  CHECK(offset <= sizeof(s_floor_flash) - sizeof(value));
  CHECK((offset & 3u) == 0u);
  if (s_fail_next_floor_program) {
    s_fail_next_floor_program = false;
    return false;
  }
  memcpy(&current, &s_floor_flash[offset], sizeof(current));
  if ((current & value) != value) {
    return false;
  }
  current &= value;
  memcpy(&s_floor_flash[offset], &current, sizeof(current));
  return true;
}

HAL_StatusTypeDef HAL_FLASH_Unlock(void) {
  s_flash_unlocked = true;
  return HAL_OK;
}

HAL_StatusTypeDef HAL_FLASH_Lock(void) {
  s_flash_unlocked = false;
  return HAL_OK;
}

HAL_StatusTypeDef HAL_FLASHEx_Erase(FLASH_EraseInitTypeDef *erase,
                                    uint32_t *sector_error) {
  ++s_erase_attempts;
  CHECK(s_flash_unlocked);
  CHECK(erase != NULL);
  CHECK(erase->TypeErase == FLASH_TYPEERASE_SECTORS);
  CHECK(erase->Sector == FLASH_SECTOR_4);
  CHECK(erase->NbSectors == 2u);
  if (s_fail_next_erase) {
    s_fail_next_erase = false;
    if (sector_error != NULL) {
      *sector_error = FLASH_SECTOR_4;
    }
    return HAL_ERROR;
  }
  memset(s_flash, 0xff, sizeof(s_flash));
  ++s_erase_successes;
  return HAL_OK;
}

HAL_StatusTypeDef HAL_FLASH_Program(uint32_t type, uint32_t address,
                                    uint64_t data) {
  uint32_t word = (uint32_t)data;
  uint8_t bytes[sizeof(word)];
  uint8_t *destination;

  ++s_program_attempts;
  CHECK(s_flash_unlocked);
  CHECK(type == FLASH_TYPEPROGRAM_WORD);
  CHECK((address % UPDATER_FLASH_WRITE_ALIGN) == 0u);
  CHECK(flash_range_is_valid(address, sizeof(word)));
  if (s_fail_next_program) {
    s_fail_next_program = false;
    return HAL_ERROR;
  }

  destination = &s_flash[address - UPDATER_APP_BASE];
  memcpy(bytes, &word, sizeof(bytes));
  for (size_t i = 0; i < sizeof(bytes); ++i) {
    /* STM32 flash can clear bits but cannot restore them without an erase. */
    if ((destination[i] & bytes[i]) != bytes[i]) {
      return HAL_ERROR;
    }
  }
  for (size_t i = 0; i < sizeof(bytes); ++i) {
    destination[i] &= bytes[i];
  }
  ++s_program_successes;
  return HAL_OK;
}

static void simulated_power_on(bool erase_flash) {
  if (erase_flash) {
    memset(s_flash, 0xff, sizeof(s_flash));
    memset(s_floor_flash, 0xff, sizeof(s_floor_flash));
    s_erase_attempts = 0u;
    s_erase_successes = 0u;
    s_program_attempts = 0u;
    s_program_successes = 0u;
  }
  s_flash_unlocked = false;
  s_fail_next_erase = false;
  s_fail_next_program = false;
  s_fail_next_floor_program = false;
  s_next_sequence = 1u;
  updater_bootloader_init();
}

static uint8_t hex_nibble(char value) {
  if (value >= '0' && value <= '9') {
    return (uint8_t)(value - '0');
  }
  value = (char)tolower((unsigned char)value);
  CHECK(value >= 'a' && value <= 'f');
  return (uint8_t)(value - 'a' + 10);
}

static void decode_named_hex(const char *json, const char *name, uint8_t *out,
                             size_t out_len) {
  const char *cursor = strstr(json, name);
  CHECK(cursor != NULL);
  cursor = strchr(cursor + strlen(name), ':');
  CHECK(cursor != NULL);
  cursor = strchr(cursor, '"');
  CHECK(cursor != NULL);
  ++cursor;
  for (size_t i = 0; i < out_len; ++i) {
    CHECK(isxdigit((unsigned char)cursor[i * 2u]));
    CHECK(isxdigit((unsigned char)cursor[i * 2u + 1u]));
    out[i] = (uint8_t)((hex_nibble(cursor[i * 2u]) << 4) |
                       hex_nibble(cursor[i * 2u + 1u]));
  }
  CHECK(cursor[out_len * 2u] == '"');
}

static signing_fixture_t load_signing_fixture(void) {
  signing_fixture_t fixture = {0};
  char json[4096];
  FILE *stream = fopen(RELEASE_SIGNING_VECTOR_PATH, "rb");
  size_t json_len;
  const char *firmware_section;

  CHECK(stream != NULL);
  json_len = fread(json, 1u, sizeof(json) - 1u, stream);
  CHECK(!ferror(stream));
  CHECK(feof(stream));
  CHECK(fclose(stream) == 0);
  json[json_len] = '\0';
  firmware_section = strstr(json, "\"firmware\"");
  CHECK(firmware_section != NULL);
  decode_named_hex(firmware_section, "\"dataHex\"", fixture.image,
                   sizeof(fixture.image));
  decode_named_hex(firmware_section, "\"manifestHex\"",
                   (uint8_t *)&fixture.manifest, sizeof(fixture.manifest));
  sign_test_manifest(&fixture.manifest, fixture.signature);
  return fixture;
}

static updater_packet_t make_request(uint8_t command, uint32_t offset,
                                     const void *payload, uint8_t length) {
  updater_packet_t request = {0};
  CHECK(length <= UPDATER_PAYLOAD_SIZE);
  CHECK(payload != NULL || length == 0u);
  request.command = command;
  request.sequence = s_next_sequence++;
  request.offset = offset;
  request.length = length;
  if (length != 0u) {
    memcpy(request.payload, payload, length);
  }
  return request;
}

static updater_packet_t exchange(const updater_packet_t *request) {
  updater_packet_t response;
  CHECK(request != NULL);
  memset(&response, 0xa5, sizeof(response));
  CHECK(updater_bootloader_process_packet((const uint8_t *)request,
                                          sizeof(*request),
                                          (uint8_t *)&response));
  CHECK(response.command == request->command);
  CHECK(response.sequence == request->sequence);
  CHECK(response.length <= UPDATER_PAYLOAD_SIZE);
  return response;
}

static updater_progress_payload_t response_progress(
    const updater_packet_t *response) {
  updater_progress_payload_t progress;
  CHECK(response != NULL);
  CHECK(response->length == sizeof(progress));
  memcpy(&progress, response->payload, sizeof(progress));
  CHECK(progress.reserved == 0u);
  CHECK(progress.next_offset == response->offset);
  return progress;
}

static updater_begin_request_t fixture_begin(const signing_fixture_t *fixture) {
  updater_begin_request_t begin = {
      .image_size = fixture->manifest.image_size,
      .image_crc32 = fixture->manifest.image_crc32,
      .fw_version_major = fixture->manifest.fw_version_major,
      .fw_version_minor = fixture->manifest.fw_version_minor,
      .fw_version_patch = fixture->manifest.fw_version_patch,
      .reserved = 0u,
  };
  return begin;
}

static updater_packet_t send_authorization(const signing_fixture_t *fixture,
                                           bool tamper_signature,
                                           bool duplicate_first_chunk) {
  test_auth_blob_t auth;
  updater_packet_t final_response = {0};
  const uint8_t *bytes = (const uint8_t *)&auth;
  uint32_t offset = 0u;

  memcpy(&auth.manifest, &fixture->manifest, sizeof(auth.manifest));
  memcpy(auth.signature, fixture->signature, sizeof(auth.signature));
  if (tamper_signature) {
    auth.signature[sizeof(auth.signature) - 1u] ^= 0x80u;
  }

  while (offset < sizeof(auth)) {
    uint32_t remaining = (uint32_t)sizeof(auth) - offset;
    uint8_t length =
        (uint8_t)(remaining > UPDATER_PAYLOAD_SIZE ? UPDATER_PAYLOAD_SIZE
                                                  : remaining);
    updater_packet_t request =
        make_request(UPDATER_CMD_AUTH, offset, &bytes[offset], length);
    updater_packet_t response = exchange(&request);
    if (offset + length < sizeof(auth)) {
      CHECK(response.status == UPDATER_STATUS_OK);
    }
    updater_progress_payload_t progress = response_progress(&response);
    CHECK(progress.accepted_length ==
          ((response.status == UPDATER_STATUS_OK) ? length : 0u));
    if (duplicate_first_chunk && offset == 0u) {
      updater_packet_t duplicate_response = exchange(&request);
      CHECK(memcmp(&duplicate_response, &response, sizeof(response)) == 0);
    }
    final_response = response;
    offset += length;
  }
  return final_response;
}

static updater_packet_t begin_update(const signing_fixture_t *fixture,
                                     updater_packet_t *request_out) {
  updater_begin_request_t begin = fixture_begin(fixture);
  updater_packet_t request = make_request(UPDATER_CMD_BEGIN, 0u, &begin,
                                          (uint8_t)sizeof(begin));
  if (request_out != NULL) {
    *request_out = request;
  }
  return exchange(&request);
}

static updater_packet_t send_data(const uint8_t *image, uint32_t offset,
                                  uint8_t length,
                                  updater_packet_t *request_out) {
  updater_packet_t request =
      make_request(UPDATER_CMD_DATA, offset, &image[offset], length);
  if (request_out != NULL) {
    *request_out = request;
  }
  return exchange(&request);
}

static void install_signed_test_image(const uint8_t *image,
                                      uint32_t image_size,
                                      updater_fw_version_t version) {
  updater_signature_manifest_t manifest;
  updater_trailer_t trailer;
  uint8_t signature[UPDATER_SIGNATURE_SIZE];
  uint32_t crc = updater_crc32_compute(image, image_size);

  CHECK(image != NULL);
  CHECK(image_size <= UPDATER_APP_MAX_IMAGE_SIZE);
  updater_signature_manifest_prepare(&manifest, image, image_size, crc,
                                     version);
  sign_test_manifest(&manifest, signature);
  updater_trailer_prepare(&trailer, image_size, crc, version, signature);
  memset(s_flash, 0xff, sizeof(s_flash));
  memcpy(s_flash, image, image_size);
  memcpy(&s_flash[UPDATER_TRAILER_ADDR - UPDATER_APP_BASE], &trailer,
         sizeof(trailer));
  crypto_wipe(&manifest, sizeof(manifest));
  crypto_wipe(signature, sizeof(signature));
}

static void build_migration_test_image(
    uint8_t image[MIGRATION_FIXTURE_IMAGE_SIZE]) {
  updater_migration_descriptor_t descriptor = {0};
  const uint32_t initial_sp = UPDATER_RAM_END;
  const uint32_t migrator_reset = UPDATER_APP_BASE + 9u;
  const uint32_t bootloader_reset = UPDATER_BOOTLOADER_BASE + 9u;
  const uint32_t bootloader_offset = 64u;
  const uint32_t bootloader_size = 64u;
  const uint8_t magic[8] = UPDATER_MIGRATION_DESCRIPTOR_MAGIC_BYTES;
  const uint8_t target_id[16] = UPDATER_MIGRATION_TARGET_ID_BYTES;

  memset(image, 0xff, MIGRATION_FIXTURE_IMAGE_SIZE);
  memcpy(image, &initial_sp, sizeof(initial_sp));
  memcpy(image + sizeof(initial_sp), &migrator_reset,
         sizeof(migrator_reset));
  memcpy(image + bootloader_offset, &initial_sp, sizeof(initial_sp));
  memcpy(image + bootloader_offset + sizeof(initial_sp), &bootloader_reset,
         sizeof(bootloader_reset));

  memcpy(descriptor.magic, magic, sizeof(descriptor.magic));
  descriptor.schema = UPDATER_MIGRATION_DESCRIPTOR_SCHEMA;
  descriptor.descriptor_size = sizeof(descriptor);
  descriptor.source_protocol = UPDATER_MIGRATION_SOURCE_PROTOCOL;
  descriptor.target_protocol = UPDATER_MIGRATION_TARGET_PROTOCOL;
  descriptor.flags = UPDATER_MIGRATION_REQUIRED_FLAGS;
  descriptor.bootloader_offset = bootloader_offset;
  descriptor.bootloader_size = bootloader_size;
  descriptor.bootloader_crc32 =
      updater_crc32_compute(image + bootloader_offset, bootloader_size);
  descriptor.image_size = MIGRATION_FIXTURE_IMAGE_SIZE;
  memcpy(descriptor.target_id, target_id, sizeof(descriptor.target_id));
  crypto_sha512(descriptor.bootloader_sha512, image + bootloader_offset,
                bootloader_size);
  descriptor.descriptor_crc32 = updater_crc32_compute(
      &descriptor,
      offsetof(updater_migration_descriptor_t, descriptor_crc32));
  memcpy(image + MIGRATION_FIXTURE_IMAGE_SIZE - sizeof(descriptor),
         &descriptor, sizeof(descriptor));
}

static void test_equal_version_migration_replacement(
    const signing_fixture_t *final_app) {
  uint8_t migration_image[MIGRATION_FIXTURE_IMAGE_SIZE];
  updater_trailer_t installed;
  updater_packet_t response;
  updater_fw_version_t version = {
      .major = final_app->manifest.fw_version_major,
      .minor = final_app->manifest.fw_version_minor,
      .patch = final_app->manifest.fw_version_patch,
  };

  simulated_power_on(true);
  CHECK(updater_version_floor_prepare(version) == UPDATER_VERSION_FLOOR_OK);
  build_migration_test_image(migration_image);
  install_signed_test_image(migration_image, sizeof(migration_image), version);
  simulated_power_on(false);
  CHECK(updater_read_trailer(&installed));
  CHECK(updater_is_app_image_valid_with_trailer(&installed));
  CHECK(updater_app_has_valid_migration_descriptor(&installed));

  /* The signed final application may replace a signed migrator with the exact
   * same release version. This is the only equal-version exception. */
  response = send_authorization(final_app, false, false);
  CHECK(response.status == UPDATER_STATUS_OK);

  install_signed_test_image(final_app->image, sizeof(final_app->image), version);
  simulated_power_on(false);
  CHECK(updater_read_trailer(&installed));
  CHECK(updater_is_app_image_valid_with_trailer(&installed));
  CHECK(!updater_app_has_valid_migration_descriptor(&installed));
  response = send_authorization(final_app, false, false);
  CHECK(response.status == UPDATER_STATUS_ROLLBACK_REJECTED);

  /* Even an authenticated image with a near-miss descriptor cannot unlock the
   * exception. Re-signing here models a release-key-authorized but malformed
   * migration artifact. */
  build_migration_test_image(migration_image);
  migration_image[sizeof(migration_image) - 1u] ^= 1u;
  install_signed_test_image(migration_image, sizeof(migration_image), version);
  simulated_power_on(false);
  CHECK(updater_read_trailer(&installed));
  CHECK(updater_is_app_image_valid_with_trailer(&installed));
  CHECK(!updater_app_has_valid_migration_descriptor(&installed));
  response = send_authorization(final_app, false, false);
  CHECK(response.status == UPDATER_STATUS_ROLLBACK_REJECTED);

  /* Unknown signed descriptor capabilities are also fail-closed. Recompute
   * the descriptor CRC so the rejection specifically covers the flag set. */
  build_migration_test_image(migration_image);
  updater_migration_descriptor_t *descriptor =
      (updater_migration_descriptor_t *)&migration_image[
          sizeof(migration_image) - sizeof(updater_migration_descriptor_t)];
  descriptor->flags |= 1u << 31;
  descriptor->descriptor_crc32 = updater_crc32_compute(
      descriptor,
      offsetof(updater_migration_descriptor_t, descriptor_crc32));
  install_signed_test_image(migration_image, sizeof(migration_image), version);
  simulated_power_on(false);
  CHECK(updater_read_trailer(&installed));
  CHECK(updater_is_app_image_valid_with_trailer(&installed));
  CHECK(!updater_app_has_valid_migration_descriptor(&installed));
  response = send_authorization(final_app, false, false);
  CHECK(response.status == UPDATER_STATUS_ROLLBACK_REJECTED);
}

static void test_real_manifest_encoder(const signing_fixture_t *fixture) {
  updater_signature_manifest_t actual;
  updater_signature_manifest_t tampered;
  updater_fw_version_t version = {
      .major = 1u,
      .minor = 2u,
      .patch = 3u,
  };
  uint32_t crc = updater_crc32_compute(fixture->image, sizeof(fixture->image));

  _Static_assert(sizeof(updater_signature_manifest_t) == 84u,
                 "signed firmware manifest must stay exactly 84 bytes");
  CHECK(crc == fixture->manifest.image_crc32);
  updater_signature_manifest_prepare(&actual, fixture->image,
                                     sizeof(fixture->image), crc, version);
  CHECK(memcmp(&actual, &fixture->manifest, sizeof(actual)) == 0);
  CHECK(updater_signature_manifest_is_sane(&actual));
  CHECK(updater_signature_manifest_verify(&actual, fixture->signature));

  tampered = actual;
  tampered.image_sha512[17] ^= 1u;
  CHECK(!updater_signature_manifest_verify(&tampered, fixture->signature));
  tampered = actual;
  tampered.context[0] ^= 1u;
  CHECK(!updater_signature_manifest_is_sane(&tampered));
  CHECK(!updater_signature_manifest_verify(&tampered, fixture->signature));
}

static void test_authentication_gate_and_malformed_packets(
    const signing_fixture_t *fixture) {
  updater_packet_t response;
  updater_packet_t request;
  updater_begin_request_t begin = fixture_begin(fixture);
  uint8_t short_request[2] = {UPDATER_CMD_BEGIN, 0x91u};
  uint8_t short_response[UPDATER_PACKET_SIZE];

  simulated_power_on(true);
  CHECK(!updater_bootloader_process_packet(NULL, 0u, NULL));
  CHECK(updater_bootloader_process_packet(short_request,
                                          (uint16_t)sizeof(short_request),
                                          short_response));
  CHECK(short_response[0] == UPDATER_CMD_BEGIN);
  CHECK(short_response[1] == 0x91u);
  CHECK(short_response[2] == UPDATER_STATUS_INVALID_PARAMETER);

  request = make_request(UPDATER_CMD_BEGIN, 0u, &begin, (uint8_t)sizeof(begin));
  response = exchange(&request);
  CHECK(response.status == UPDATER_STATUS_AUTH_REQUIRED);
  CHECK(s_erase_attempts == 0u);

  request = make_request(UPDATER_CMD_DATA, 0u, fixture->image, 4u);
  response = exchange(&request);
  CHECK(response.status == UPDATER_STATUS_INVALID_STATE);
  CHECK(s_program_attempts == 0u);

  request = make_request(UPDATER_CMD_FINISH, 0u, NULL, 0u);
  response = exchange(&request);
  CHECK(response.status == UPDATER_STATUS_INVALID_STATE);
  request = make_request(UPDATER_CMD_BOOT, 0u, NULL, 0u);
  response = exchange(&request);
  CHECK(response.status == UPDATER_STATUS_INVALID_IMAGE);

  request = make_request(0xfeu, 0u, NULL, 0u);
  response = exchange(&request);
  CHECK(response.status == UPDATER_STATUS_INVALID_COMMAND);
  request = make_request(UPDATER_CMD_HELLO, 0u, NULL, 0u);
  request.length = UPDATER_PAYLOAD_SIZE + 1u;
  response = exchange(&request);
  CHECK(response.status == UPDATER_STATUS_INVALID_PARAMETER);

  request = make_request(UPDATER_CMD_AUTH, 4u, fixture->image, 4u);
  response = exchange(&request);
  CHECK(response.status == UPDATER_STATUS_INVALID_PARAMETER);
  CHECK(s_erase_attempts == 0u);

  response = send_authorization(fixture, true, true);
  CHECK(response.status == UPDATER_STATUS_AUTH_FAILED);
  CHECK((response_progress(&response).flags &
         UPDATER_FLAG_MANIFEST_AUTHENTICATED) == 0u);
  /* Replaying a non-current AUTH fragment is rejected and cannot promote the
   * failed authorization attempt. */
  response = exchange(&request);
  CHECK(response.status == UPDATER_STATUS_INVALID_PARAMETER);
  CHECK(s_erase_attempts == 0u);
  response = begin_update(fixture, NULL);
  CHECK(response.status == UPDATER_STATUS_AUTH_REQUIRED);
  CHECK(s_erase_attempts == 0u);

  response = send_authorization(fixture, false, true);
  CHECK(response.status == UPDATER_STATUS_OK);
  CHECK((response_progress(&response).flags &
         UPDATER_FLAG_MANIFEST_AUTHENTICATED) != 0u);

  begin.image_crc32 ^= 1u;
  request = make_request(UPDATER_CMD_BEGIN, 0u, &begin, (uint8_t)sizeof(begin));
  response = exchange(&request);
  CHECK(response.status == UPDATER_STATUS_INVALID_PARAMETER);
  CHECK(s_erase_attempts == 0u);
  begin = fixture_begin(fixture);
  begin.reserved = 1u;
  request = make_request(UPDATER_CMD_BEGIN, 0u, &begin, (uint8_t)sizeof(begin));
  response = exchange(&request);
  CHECK(response.status == UPDATER_STATUS_INVALID_PARAMETER);
  CHECK(s_erase_attempts == 0u);

  request = make_request(UPDATER_CMD_ABORT, 0u, NULL, 0u);
  response = exchange(&request);
  CHECK(response.status == UPDATER_STATUS_OK);
  CHECK((response_progress(&response).flags &
         UPDATER_FLAG_MANIFEST_AUTHENTICATED) == 0u);
  response = begin_update(fixture, NULL);
  CHECK(response.status == UPDATER_STATUS_AUTH_REQUIRED);
  CHECK(s_erase_attempts == 0u);
}

static void test_power_cut_recovery_and_complete_update(
    const signing_fixture_t *fixture) {
  updater_packet_t request;
  updater_packet_t response;
  updater_packet_t begin_request;
  updater_packet_t first_data_request;
  updater_packet_t second_data_request;
  uint8_t oversized_tail[12] = {0};
  uint32_t program_count;
  uint8_t saved_byte;

  simulated_power_on(true);
  response = send_authorization(fixture, false, true);
  CHECK(response.status == UPDATER_STATUS_OK);
  response = begin_update(fixture, &begin_request);
  CHECK(response.status == UPDATER_STATUS_OK);
  CHECK(s_erase_attempts == 1u && s_erase_successes == 1u);
  CHECK((response_progress(&response).flags & UPDATER_FLAG_SESSION_ACTIVE) !=
        0u);
  response = exchange(&begin_request);
  CHECK(response.status == UPDATER_STATUS_OK);
  CHECK(s_erase_attempts == 1u);

  request = make_request(UPDATER_CMD_AUTH, 0u, &fixture->manifest, 8u);
  response = exchange(&request);
  CHECK(response.status == UPDATER_STATUS_INVALID_STATE);

  request = make_request(UPDATER_CMD_DATA, 2u, fixture->image, 4u);
  response = exchange(&request);
  CHECK(response.status == UPDATER_STATUS_INVALID_PARAMETER);
  request = make_request(UPDATER_CMD_DATA, 4u, fixture->image, 4u);
  response = exchange(&request);
  CHECK(response.status == UPDATER_STATUS_INVALID_STATE);
  request = make_request(UPDATER_CMD_FINISH, 0u, NULL, 0u);
  response = exchange(&request);
  CHECK(response.status == UPDATER_STATUS_INVALID_STATE);

  response = send_data(fixture->image, 0u, 56u, &first_data_request);
  CHECK(response.status == UPDATER_STATUS_OK);
  CHECK(response_progress(&response).accepted_length == 56u);
  program_count = s_program_successes;
  response = exchange(&first_data_request);
  CHECK(response.status == UPDATER_STATUS_OK);
  CHECK(s_program_successes == program_count);

  /* Simulate a reset/power cut. Flash persists; RAM authorization, session and
   * duplicate cache must not. A partial image must never be bootable. */
  simulated_power_on(false);
  response = exchange(&first_data_request);
  CHECK(response.status == UPDATER_STATUS_INVALID_STATE);
  request = make_request(UPDATER_CMD_BOOT, 0u, NULL, 0u);
  response = exchange(&request);
  CHECK(response.status == UPDATER_STATUS_INVALID_IMAGE);
  CHECK(!updater_bootloader_should_jump_to_app());

  response = send_authorization(fixture, false, false);
  CHECK(response.status == UPDATER_STATUS_OK);
  response = begin_update(fixture, NULL);
  CHECK(response.status == UPDATER_STATUS_OK);
  CHECK(s_erase_attempts == 2u && s_erase_successes == 2u);

  response = send_data(fixture->image, 0u, 56u, &first_data_request);
  CHECK(response.status == UPDATER_STATUS_OK);
  request = make_request(UPDATER_CMD_FINISH, 0u, NULL, 0u);
  response = exchange(&request);
  CHECK(response.status == UPDATER_STATUS_INVALID_STATE);
  memcpy(oversized_tail, &fixture->image[56], 8u);
  request = make_request(UPDATER_CMD_DATA, 56u, oversized_tail, 12u);
  response = exchange(&request);
  CHECK(response.status == UPDATER_STATUS_INVALID_PARAMETER);

  response = send_data(fixture->image, 56u, 8u, &second_data_request);
  CHECK(response.status == UPDATER_STATUS_OK);
  CHECK(response.offset == sizeof(fixture->image));
  program_count = s_program_successes;
  response = exchange(&second_data_request);
  CHECK(response.status == UPDATER_STATUS_OK);
  CHECK(s_program_successes == program_count);
  response = exchange(&first_data_request);
  CHECK(response.status == UPDATER_STATUS_INVALID_STATE);
  CHECK(s_program_successes == program_count);

  saved_byte = s_flash[0];
  s_flash[0] ^= 1u;
  request = make_request(UPDATER_CMD_FINISH, 0u, NULL, 0u);
  response = exchange(&request);
  CHECK(response.status == UPDATER_STATUS_INVALID_IMAGE);
  CHECK(s_program_successes == program_count);
  s_flash[0] = saved_byte;

  saved_byte = s_flash[20];
  s_flash[20] ^= 1u;
  request = make_request(UPDATER_CMD_FINISH, 0u, NULL, 0u);
  response = exchange(&request);
  CHECK(response.status == UPDATER_STATUS_VERIFY_FAILED);
  CHECK(s_program_successes == program_count);
  s_flash[20] = saved_byte;

  request = make_request(UPDATER_CMD_FINISH, 0u, NULL, 0u);
  response = exchange(&request);
  CHECK(response.status == UPDATER_STATUS_OK);
  CHECK((response_progress(&response).flags &
         (UPDATER_FLAG_SESSION_ACTIVE |
          UPDATER_FLAG_MANIFEST_AUTHENTICATED)) == 0u);
  CHECK(s_program_successes ==
        program_count + sizeof(updater_trailer_t) / sizeof(uint32_t));
  program_count = s_program_successes;
  updater_packet_t finish_response = response;
  response = exchange(&request);
  CHECK(memcmp(&response, &finish_response, sizeof(response)) == 0);
  CHECK(s_program_successes == program_count);
  CHECK(updater_is_app_image_valid());
  CHECK(!updater_trailer_is_valid(NULL));
  updater_trailer_t canonical_trailer;
  CHECK(updater_read_trailer(&canonical_trailer));
  canonical_trailer.reserved = 1u;
  canonical_trailer.trailer_crc32 = updater_crc32_compute(
      &canonical_trailer, sizeof(canonical_trailer) - sizeof(uint32_t));
  CHECK(!updater_trailer_is_valid(&canonical_trailer));

  request = make_request(UPDATER_CMD_FINISH, 0u, NULL, 0u);
  response = exchange(&request);
  CHECK(response.status == UPDATER_STATUS_INVALID_STATE);

  request = make_request(UPDATER_CMD_HELLO, 0u, NULL, 0u);
  response = exchange(&request);
  CHECK(response.status == UPDATER_STATUS_OK);
  CHECK(response.length == sizeof(updater_hello_payload_t));
  updater_hello_payload_t hello;
  memcpy(&hello, response.payload, sizeof(hello));
  CHECK(hello.protocol_version == UPDATER_PROTOCOL_VERSION);
  CHECK(hello.app_base == UPDATER_APP_BASE);
  CHECK(hello.app_max_size == UPDATER_APP_MAX_IMAGE_SIZE);
  CHECK(hello.write_align == UPDATER_FLASH_WRITE_ALIGN);
  CHECK((hello.flags & (UPDATER_FLAG_APP_VALID |
                        UPDATER_FLAG_SIGNATURE_REQUIRED)) ==
        (UPDATER_FLAG_APP_VALID | UPDATER_FLAG_SIGNATURE_REQUIRED));
  CHECK(hello.installed_fw_version_major == 1u);
  CHECK(hello.installed_fw_version_minor == 2u);
  CHECK(hello.installed_fw_version_patch == 3u);

  request = make_request(UPDATER_CMD_BOOT, 0u, NULL, 0u);
  response = exchange(&request);
  CHECK(response.status == UPDATER_STATUS_OK);
  CHECK(!updater_bootloader_should_jump_to_app());
  updater_packet_t abort_request =
      make_request(UPDATER_CMD_ABORT, 0u, NULL, 0u);
  response = exchange(&abort_request);
  CHECK(response.status == UPDATER_STATUS_OK);
  updater_bootloader_notify_response_sent();
  CHECK(!updater_bootloader_should_jump_to_app());

  request = make_request(UPDATER_CMD_BOOT, 0u, NULL, 0u);
  response = exchange(&request);
  CHECK(response.status == UPDATER_STATUS_OK);
  updater_packet_t duplicate_boot = exchange(&request);
  CHECK(memcmp(&duplicate_boot, &response, sizeof(response)) == 0);
  CHECK(!updater_bootloader_should_jump_to_app());
  updater_bootloader_notify_response_sent();
  CHECK(updater_bootloader_should_jump_to_app());

  /* Simulate power loss after a newer BEGIN commits its floor but before the
   * erase reaches the still-valid old slot. That old signed image must neither
   * be advertised as bootable nor accepted by BOOT. The reset-time main path
   * uses the same floor predicate. */
  CHECK(updater_version_floor_prepare(
            (updater_fw_version_t){.major = 1u, .minor = 2u, .patch = 4u}) ==
        UPDATER_VERSION_FLOOR_OK);
  simulated_power_on(false);
  request = make_request(UPDATER_CMD_HELLO, 0u, NULL, 0u);
  response = exchange(&request);
  CHECK(response.status == UPDATER_STATUS_OK);
  memcpy(&hello, response.payload, sizeof(hello));
  CHECK((hello.flags & UPDATER_FLAG_APP_VALID) == 0u);
  request = make_request(UPDATER_CMD_BOOT, 0u, NULL, 0u);
  response = exchange(&request);
  CHECK(response.status == UPDATER_STATUS_INVALID_IMAGE);
  CHECK(!updater_bootloader_should_jump_to_app());

  /* A valid installed image enforces strict monotonicity. */
  uint32_t erases_before_rollback = s_erase_attempts;
  response = send_authorization(fixture, false, false);
  CHECK(response.status == UPDATER_STATUS_ROLLBACK_REJECTED);
  CHECK((response_progress(&response).flags &
         UPDATER_FLAG_MANIFEST_AUTHENTICATED) == 0u);
  CHECK(s_erase_attempts == erases_before_rollback);
  response = begin_update(fixture, NULL);
  CHECK(response.status == UPDATER_STATUS_AUTH_REQUIRED);
  CHECK(s_erase_attempts == erases_before_rollback);

  /* A power cut while programming the first-ever floor record can leave a
   * non-erased journal with no committed entry. Even if the application and
   * its signature are otherwise valid, every reboot/jump surface must fail
   * closed instead of treating that corrupt journal as factory blank. */
  CHECK(updater_is_app_image_valid());
  memset(s_floor_flash, 0xff, sizeof(s_floor_flash));
  s_floor_flash[0] = 0u;
  simulated_power_on(false);
  request = make_request(UPDATER_CMD_HELLO, 0u, NULL, 0u);
  response = exchange(&request);
  CHECK(response.status == UPDATER_STATUS_OK);
  memcpy(&hello, response.payload, sizeof(hello));
  CHECK((hello.flags & UPDATER_FLAG_APP_VALID) == 0u);
  request = make_request(UPDATER_CMD_BOOT, 0u, NULL, 0u);
  response = exchange(&request);
  CHECK(response.status == UPDATER_STATUS_INVALID_IMAGE);
  CHECK(!updater_bootloader_should_jump_to_app());
}

static void test_flash_failures_and_abort(const signing_fixture_t *fixture) {
  updater_packet_t request;
  updater_packet_t response;
  updater_packet_t failed_begin;
  updater_packet_t failed_data;
  uint32_t attempts;

  simulated_power_on(true);
  response = send_authorization(fixture, false, false);
  CHECK(response.status == UPDATER_STATUS_OK);
  s_fail_next_floor_program = true;
  response = begin_update(fixture, NULL);
  CHECK(response.status == UPDATER_STATUS_STORAGE_ERROR);
  CHECK(s_erase_attempts == 0u);
  s_fail_next_erase = true;
  response = begin_update(fixture, &failed_begin);
  CHECK(response.status == UPDATER_STATUS_ERROR);
  CHECK(s_erase_attempts == 1u && s_erase_successes == 0u);
  attempts = s_erase_attempts;
  response = exchange(&failed_begin);
  CHECK(response.status == UPDATER_STATUS_ERROR);
  CHECK(s_erase_attempts == attempts);

  response = begin_update(fixture, NULL);
  CHECK(response.status == UPDATER_STATUS_OK);
  CHECK(s_erase_successes == 1u);
  s_fail_next_program = true;
  response = send_data(fixture->image, 0u, 56u, &failed_data);
  CHECK(response.status == UPDATER_STATUS_ERROR);
  CHECK(response.offset == 0u);
  attempts = s_program_attempts;
  response = exchange(&failed_data);
  CHECK(response.status == UPDATER_STATUS_ERROR);
  CHECK(s_program_attempts == attempts);

  response = send_data(fixture->image, 0u, 56u, NULL);
  CHECK(response.status == UPDATER_STATUS_OK);
  request = make_request(UPDATER_CMD_ABORT, 0u, NULL, 0u);
  response = exchange(&request);
  CHECK(response.status == UPDATER_STATUS_OK);
  updater_packet_t duplicate_abort = exchange(&request);
  CHECK(memcmp(&duplicate_abort, &response, sizeof(response)) == 0);
  response = send_data(fixture->image, 56u, 8u, NULL);
  CHECK(response.status == UPDATER_STATUS_INVALID_STATE);
  response = begin_update(fixture, NULL);
  CHECK(response.status == UPDATER_STATUS_AUTH_REQUIRED);
  CHECK(s_erase_successes == 1u);
}

int main(void) {
  signing_fixture_t fixture = load_signing_fixture();

  CHECK(sizeof(test_auth_blob_t) == UPDATER_AUTH_BLOB_SIZE);
  CHECK(fixture.manifest.image_size == sizeof(fixture.image));
  test_real_manifest_encoder(&fixture);
  test_authentication_gate_and_malformed_packets(&fixture);
  test_power_cut_recovery_and_complete_update(&fixture);
  test_flash_failures_and_abort(&fixture);
  test_equal_version_migration_replacement(&fixture);

  puts("updater_bootloader_protocol_test: ok");
  return EXIT_SUCCESS;
}
