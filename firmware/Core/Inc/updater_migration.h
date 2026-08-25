#ifndef UPDATER_MIGRATION_H_
#define UPDATER_MIGRATION_H_

#include <stdint.h>

#define UPDATER_MIGRATION_DESCRIPTOR_MAGIC_BYTES                            \
  {'K', 'B', 'H', 'E', 'M', 'I', 'G', '3'}
#define UPDATER_MIGRATION_DESCRIPTOR_SCHEMA 2u
#define UPDATER_MIGRATION_DESCRIPTOR_SIZE 128u
#define UPDATER_MIGRATION_EXECUTABLE_MAX_SIZE 0x00010000u
#define UPDATER_MIGRATION_SOURCE_PROTOCOL 0x0002u
#define UPDATER_MIGRATION_TARGET_PROTOCOL 0x0003u
#define UPDATER_MIGRATION_FLAG_BOOTADDR_RESUMABLE (1u << 0)
#define UPDATER_MIGRATION_FLAG_V3_TRAILER_PRESEEDED (1u << 1)
#define UPDATER_MIGRATION_FLAG_V3_REFRESH_ALLOWED (1u << 2)
#define UPDATER_MIGRATION_REQUIRED_FLAGS                                    \
  (UPDATER_MIGRATION_FLAG_BOOTADDR_RESUMABLE |                             \
   UPDATER_MIGRATION_FLAG_V3_TRAILER_PRESEEDED |                           \
   UPDATER_MIGRATION_FLAG_V3_REFRESH_ALLOWED)
#define UPDATER_MIGRATION_TARGET_ID_BYTES                                   \
  {'K', 'B', 'H', 'E', '7', '5', 'H', 'E',                                \
   'F', '7', '2', '3', 'V', 'E', 'T', '6'}

typedef struct __attribute__((packed)) {
  uint8_t magic[8];
  uint16_t schema;
  uint16_t descriptor_size;
  uint16_t source_protocol;
  uint16_t target_protocol;
  uint32_t flags;
  uint32_t bootloader_offset;
  uint32_t bootloader_size;
  uint32_t bootloader_crc32;
  uint32_t image_size;
  uint8_t target_id[16];
  uint8_t bootloader_sha512[64];
  uint8_t bootloader_version_major;
  uint8_t bootloader_version_minor;
  uint8_t bootloader_version_patch;
  uint8_t reserved0;
  uint8_t reserved[4];
  uint32_t descriptor_crc32;
} updater_migration_descriptor_t;

#endif /* UPDATER_MIGRATION_H_ */
