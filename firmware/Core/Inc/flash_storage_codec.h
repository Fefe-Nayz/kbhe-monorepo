#ifndef FLASH_STORAGE_CODEC_H_
#define FLASH_STORAGE_CODEC_H_

#include <stdbool.h>
#include <stdint.h>

#define FLASH_STORAGE_OBJECT_MAGIC 0x4B424F32u /* "KBO2" */
#define FLASH_STORAGE_COMMIT_MAGIC 0x4B42434Du /* "KBCM" */
#define FLASH_STORAGE_FORMAT_VERSION 1u
#define FLASH_STORAGE_OBJECT_FLAGS_NONE 0u
#define FLASH_STORAGE_BANK_MAGIC 0x4B424232u /* "KBB2" */
#define FLASH_STORAGE_BANK_COMMIT_MAGIC 0x4B42424Du /* "KBBM" */
#define FLASH_STORAGE_BANK_FORMAT_VERSION 1u

typedef struct __attribute__((packed)) {
  uint32_t magic;
  uint16_t format_version;
  uint16_t header_size;
  uint16_t object_namespace;
  uint16_t object_id;
  uint16_t schema_version;
  uint16_t flags;
  uint32_t length;
  uint32_t generation;
  uint32_t payload_crc32;
  uint32_t header_crc32;
} flash_storage_object_header_t;

/* Generation is programmed first; magic is the final atomic commit word. */
typedef struct __attribute__((packed)) {
  uint32_t generation;
  uint32_t magic;
} flash_storage_commit_t;

/* commit_magic is programmed only after every live/new record is durable. */
typedef struct __attribute__((packed)) {
  uint32_t magic;
  uint16_t format_version;
  uint16_t header_size;
  uint32_t generation;
  uint32_t header_crc32;
  uint32_t commit_magic;
} flash_storage_bank_header_t;

uint32_t flash_storage_codec_crc32(const void *data, uint32_t len);
uint32_t flash_storage_codec_crc32_update(uint32_t state, const void *data,
                                          uint32_t len);
bool flash_storage_codec_record_size(uint32_t payload_length,
                                     uint32_t *payload_size,
                                     uint32_t *record_size);
void flash_storage_codec_prepare_header(
    flash_storage_object_header_t *header, uint16_t object_namespace,
    uint16_t object_id, uint16_t schema_version, const void *payload,
    uint32_t length, uint32_t generation);
void flash_storage_codec_prepare_header_with_crc(
    flash_storage_object_header_t *header, uint16_t object_namespace,
    uint16_t object_id, uint16_t schema_version, uint32_t length,
    uint32_t generation, uint32_t payload_crc32);
bool flash_storage_codec_header_is_valid(
    const flash_storage_object_header_t *header);
bool flash_storage_codec_record_is_committed(
    const flash_storage_object_header_t *header, const void *payload,
    const flash_storage_commit_t *commit);
void flash_storage_codec_prepare_bank_header(
    flash_storage_bank_header_t *header, uint32_t generation,
    bool committed);
bool flash_storage_codec_bank_header_is_valid(
    const flash_storage_bank_header_t *header);
bool flash_storage_codec_select_bank(
    const flash_storage_bank_header_t *bank_a,
    const flash_storage_bank_header_t *bank_b, uint8_t *selected_bank);

#endif /* FLASH_STORAGE_CODEC_H_ */
