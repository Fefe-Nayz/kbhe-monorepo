#ifndef UPDATER_SHARED_H_
#define UPDATER_SHARED_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KBHE_USB_VID 0x9172U
#define KBHE_APP_USB_PID 0x0002U
#define KBHE_UPDATER_USB_PID 0x0003U

#define UPDATER_PROTOCOL_VERSION 0x0003U
#define UPDATER_PACKET_SIZE 64U
#define UPDATER_PAYLOAD_SIZE 56U
#define UPDATER_FLASH_WRITE_ALIGN 4U

#define UPDATER_RAM_BASE 0x20000000UL
#define UPDATER_BOOTRAM_BASE 0x2003FF00UL
#define UPDATER_RAM_END UPDATER_BOOTRAM_BASE

#define UPDATER_BOOTLOADER_BASE 0x08000000UL
#define UPDATER_BOOTLOADER_SIZE 0x00010000UL
#define UPDATER_BOOTLOADER_CODE_SIZE 0x0000C000UL
#define UPDATER_VERSION_FLOOR_BASE 0x0800C000UL
#define UPDATER_VERSION_FLOOR_SIZE 0x00004000UL

#define UPDATER_APP_BASE 0x08010000UL
#define UPDATER_APP_SLOT_SIZE 0x00030000UL
#define UPDATER_TRAILER_RESERVED_SIZE 0x00000100UL
#define UPDATER_APP_MAX_IMAGE_SIZE                                             \
  (UPDATER_APP_SLOT_SIZE - UPDATER_TRAILER_RESERVED_SIZE)
#define UPDATER_TRAILER_ADDR (UPDATER_APP_BASE + UPDATER_APP_MAX_IMAGE_SIZE)
#define UPDATER_APP_SLOT_END (UPDATER_APP_BASE + UPDATER_APP_SLOT_SIZE)

#define UPDATER_TRAILER_MAGIC 0x55445452UL
#define UPDATER_BOOT_REQUEST_MAGIC 0x4B425550UL
#define UPDATER_SIGNATURE_SIZE 64U
#define UPDATER_SIGNATURE_MANIFEST_SIZE 84U
#define UPDATER_AUTH_BLOB_SIZE                                                \
  (UPDATER_SIGNATURE_MANIFEST_SIZE + UPDATER_SIGNATURE_SIZE)

typedef enum {
  BOOT_REQUEST_ACTION_NONE = 0,
  BOOT_REQUEST_ACTION_ENTER_UPDATER = 1,
} boot_request_action_t;

typedef struct __attribute__((packed)) {
  uint32_t magic;
  uint32_t action;
} boot_request_t;

typedef enum {
  UPDATER_CMD_HELLO = 0x01,
  UPDATER_CMD_BEGIN = 0x02,
  UPDATER_CMD_DATA = 0x03,
  UPDATER_CMD_FINISH = 0x04,
  UPDATER_CMD_ABORT = 0x05,
  UPDATER_CMD_BOOT = 0x06,
  UPDATER_CMD_AUTH = 0x07,
} updater_command_t;

typedef enum {
  UPDATER_STATUS_OK = 0x00,
  UPDATER_STATUS_ERROR = 0x01,
  UPDATER_STATUS_INVALID_COMMAND = 0x02,
  UPDATER_STATUS_INVALID_PARAMETER = 0x03,
  UPDATER_STATUS_INVALID_STATE = 0x04,
  UPDATER_STATUS_VERIFY_FAILED = 0x05,
  UPDATER_STATUS_INVALID_IMAGE = 0x06,
  UPDATER_STATUS_AUTH_REQUIRED = 0x07,
  UPDATER_STATUS_AUTH_FAILED = 0x08,
  UPDATER_STATUS_ROLLBACK_REJECTED = 0x09,
  UPDATER_STATUS_STORAGE_ERROR = 0x0A,
} updater_status_t;

enum {
  UPDATER_FLAG_APP_VALID = 1u << 0,
  UPDATER_FLAG_SESSION_ACTIVE = 1u << 1,
  UPDATER_FLAG_SIGNATURE_REQUIRED = 1u << 2,
  UPDATER_FLAG_MANIFEST_AUTHENTICATED = 1u << 3,
};

typedef struct __attribute__((packed)) {
  uint8_t command;
  uint8_t sequence;
  uint8_t status;
  uint8_t length;
  uint32_t offset;
  uint8_t payload[UPDATER_PAYLOAD_SIZE];
} updater_packet_t;

typedef struct __attribute__((packed)) {
  uint32_t magic;
  uint32_t image_size;
  uint32_t image_crc32;
  uint8_t fw_version_major;
  uint8_t fw_version_minor;
  uint8_t fw_version_patch;
  uint8_t reserved;
  uint8_t signature[UPDATER_SIGNATURE_SIZE];
  uint32_t trailer_crc32;
} updater_trailer_t;

typedef struct __attribute__((packed)) {
  uint32_t image_size;
  uint32_t image_crc32;
  uint8_t fw_version_major;
  uint8_t fw_version_minor;
  uint8_t fw_version_patch;
  uint8_t reserved;
} updater_begin_request_t;

/*
 * Canonical bytes signed with Ed25519 for firmware releases. All integer
 * fields are little-endian on the wire. The image hash covers exactly
 * image_size bytes (never the 0xFF transport padding).
 */
typedef struct __attribute__((packed)) {
  uint8_t context[8]; /* "KBHEFW3\0" */
  uint32_t image_size;
  uint32_t image_crc32;
  uint8_t fw_version_major;
  uint8_t fw_version_minor;
  uint8_t fw_version_patch;
  uint8_t reserved;
  uint8_t image_sha512[64];
} updater_signature_manifest_t;

typedef struct __attribute__((packed)) {
  uint16_t protocol_version;
  uint16_t flags;
  uint32_t app_base;
  uint32_t app_max_size;
  uint32_t write_align;
  uint8_t installed_fw_version_major;
  uint8_t installed_fw_version_minor;
  uint8_t installed_fw_version_patch;
  uint8_t reserved;
} updater_hello_payload_t;

typedef struct __attribute__((packed)) {
  uint32_t next_offset;
  uint32_t accepted_length;
  uint32_t flags;
  uint32_t reserved;
} updater_progress_payload_t;

extern volatile boot_request_t g_boot_request;

void boot_request_clear(void);
void boot_request_set(boot_request_action_t action);
bool boot_request_take(boot_request_action_t action);

typedef struct {
  uint8_t major;
  uint8_t minor;
  uint8_t patch;
} updater_fw_version_t;

uint32_t updater_crc32_compute(const void *data, uint32_t len);
void updater_signature_manifest_prepare(
    updater_signature_manifest_t *manifest, const void *image,
    uint32_t image_size, uint32_t image_crc32,
    updater_fw_version_t fw_version);
bool updater_signature_manifest_is_sane(
    const updater_signature_manifest_t *manifest);
bool updater_signature_manifest_verify(
    const updater_signature_manifest_t *manifest,
    const uint8_t signature[UPDATER_SIGNATURE_SIZE]);
void updater_trailer_prepare(updater_trailer_t *trailer, uint32_t image_size,
                             uint32_t image_crc32,
                             updater_fw_version_t fw_version,
                             const uint8_t signature[UPDATER_SIGNATURE_SIZE]);
bool updater_trailer_is_valid(const updater_trailer_t *trailer);
bool updater_read_trailer(updater_trailer_t *out_trailer);
bool updater_is_app_vector_valid(uint32_t app_base);
bool updater_is_app_vector_valid_for_image(uint32_t app_base,
                                           uint32_t image_size);
bool updater_is_app_image_valid_with_trailer(
    const updater_trailer_t *trailer);
/**
 * Validate the signed-image-internal KBHEMIG3 descriptor and embedded updater.
 * The caller must separately authenticate `trailer` before granting any
 * privilege based on this structural result.
 */
bool updater_app_has_valid_migration_descriptor(
    const updater_trailer_t *trailer);
bool updater_is_app_image_valid(void);
bool updater_read_valid_app_version(updater_fw_version_t *version_out);
updater_fw_version_t updater_get_app_version(void);
uint32_t updater_align_up(uint32_t value, uint32_t align);
bool updater_align_up_checked(uint32_t value, uint32_t align,
                              uint32_t *aligned_value);

#ifdef __cplusplus
}
#endif

#endif /* UPDATER_SHARED_H_ */
