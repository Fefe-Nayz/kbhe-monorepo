#include "flash_storage_codec.h"

#include <stddef.h>
#include <string.h>

_Static_assert(sizeof(flash_storage_object_header_t) == 32u,
               "object flash header must stay word aligned");
_Static_assert(sizeof(flash_storage_commit_t) == 8u,
               "object commit must stay two words");
_Static_assert(sizeof(flash_storage_bank_header_t) == 20u,
               "bank header must stay word aligned");

uint32_t flash_storage_codec_crc32(const void *data, uint32_t len) {
  uint32_t crc = 0xFFFFFFFFu;

  if (data == NULL && len != 0u) {
    return 0u;
  }
  crc = flash_storage_codec_crc32_update(crc, data, len);
  return ~crc;
}

uint32_t flash_storage_codec_crc32_update(uint32_t state, const void *data,
                                          uint32_t len) {
  const uint8_t *bytes = (const uint8_t *)data;
  uint32_t crc = state;

  if (bytes == NULL && len != 0u) {
    return 0u;
  }
  while (len-- != 0u) {
    crc ^= *bytes++;
    for (uint32_t bit = 0u; bit < 8u; bit++) {
      crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)-(int32_t)(crc & 1u));
    }
  }
  return crc;
}

bool flash_storage_codec_record_size(uint32_t payload_length,
                                     uint32_t *payload_size,
                                     uint32_t *record_size) {
  uint32_t aligned = 0u;
  if (payload_length == 0u || payload_size == NULL || record_size == NULL ||
      payload_length > UINT32_MAX - 3u) {
    return false;
  }
  aligned = (payload_length + 3u) & ~3u;
  if (aligned > UINT32_MAX - sizeof(flash_storage_object_header_t) -
                    sizeof(flash_storage_commit_t)) {
    return false;
  }
  *payload_size = aligned;
  *record_size = sizeof(flash_storage_object_header_t) + aligned +
                 sizeof(flash_storage_commit_t);
  return true;
}

void flash_storage_codec_prepare_header(
    flash_storage_object_header_t *header, uint16_t object_namespace,
    uint16_t object_id, uint16_t schema_version, const void *payload,
    uint32_t length, uint32_t generation) {
  flash_storage_codec_prepare_header_with_crc(
      header, object_namespace, object_id, schema_version, length, generation,
      flash_storage_codec_crc32(payload, length));
}

void flash_storage_codec_prepare_header_with_crc(
    flash_storage_object_header_t *header, uint16_t object_namespace,
    uint16_t object_id, uint16_t schema_version, uint32_t length,
    uint32_t generation, uint32_t payload_crc32) {
  memset(header, 0, sizeof(*header));
  header->magic = FLASH_STORAGE_OBJECT_MAGIC;
  header->format_version = FLASH_STORAGE_FORMAT_VERSION;
  header->header_size = sizeof(*header);
  header->object_namespace = object_namespace;
  header->object_id = object_id;
  header->schema_version = schema_version;
  header->flags = FLASH_STORAGE_OBJECT_FLAGS_NONE;
  header->length = length;
  header->generation = generation;
  header->payload_crc32 = payload_crc32;
  header->header_crc32 = flash_storage_codec_crc32(
      header, offsetof(flash_storage_object_header_t, header_crc32));
}

bool flash_storage_codec_header_is_valid(
    const flash_storage_object_header_t *header) {
  if (header == NULL || header->magic != FLASH_STORAGE_OBJECT_MAGIC ||
      header->format_version != FLASH_STORAGE_FORMAT_VERSION ||
      header->header_size != sizeof(*header) ||
      header->flags != FLASH_STORAGE_OBJECT_FLAGS_NONE ||
      header->object_namespace == 0u || header->length == 0u ||
      header->generation == 0u) {
    return false;
  }
  return flash_storage_codec_crc32(
             header, offsetof(flash_storage_object_header_t, header_crc32)) ==
         header->header_crc32;
}

bool flash_storage_codec_record_is_committed(
    const flash_storage_object_header_t *header, const void *payload,
    const flash_storage_commit_t *commit) {
  return flash_storage_codec_header_is_valid(header) && payload != NULL &&
         commit != NULL && commit->generation == header->generation &&
         commit->magic == FLASH_STORAGE_COMMIT_MAGIC &&
         flash_storage_codec_crc32(payload, header->length) ==
             header->payload_crc32;
}

void flash_storage_codec_prepare_bank_header(
    flash_storage_bank_header_t *header, uint32_t generation,
    bool committed) {
  memset(header, 0, sizeof(*header));
  header->magic = FLASH_STORAGE_BANK_MAGIC;
  header->format_version = FLASH_STORAGE_BANK_FORMAT_VERSION;
  header->header_size = sizeof(*header);
  header->generation = generation == 0u ? 1u : generation;
  header->header_crc32 = flash_storage_codec_crc32(
      header, offsetof(flash_storage_bank_header_t, header_crc32));
  header->commit_magic = committed ? FLASH_STORAGE_BANK_COMMIT_MAGIC
                                   : UINT32_MAX;
}

bool flash_storage_codec_bank_header_is_valid(
    const flash_storage_bank_header_t *header) {
  return header != NULL && header->magic == FLASH_STORAGE_BANK_MAGIC &&
         header->format_version == FLASH_STORAGE_BANK_FORMAT_VERSION &&
         header->header_size == sizeof(*header) && header->generation != 0u &&
         header->commit_magic == FLASH_STORAGE_BANK_COMMIT_MAGIC &&
         flash_storage_codec_crc32(
             header, offsetof(flash_storage_bank_header_t, header_crc32)) ==
             header->header_crc32;
}

bool flash_storage_codec_select_bank(
    const flash_storage_bank_header_t *bank_a,
    const flash_storage_bank_header_t *bank_b, uint8_t *selected_bank) {
  bool valid_a = flash_storage_codec_bank_header_is_valid(bank_a);
  bool valid_b = flash_storage_codec_bank_header_is_valid(bank_b);

  if (selected_bank == NULL || (!valid_a && !valid_b)) {
    return false;
  }
  if (valid_a && !valid_b) {
    *selected_bank = 0u;
  } else if (!valid_a && valid_b) {
    *selected_bank = 1u;
  } else {
    *selected_bank =
        (int32_t)(bank_b->generation - bank_a->generation) > 0 ? 1u : 0u;
  }
  return true;
}
