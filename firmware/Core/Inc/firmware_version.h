#ifndef FIRMWARE_VERSION_H_
#define FIRMWARE_VERSION_H_

#include <stdint.h>

#define FIRMWARE_VERSION_MAJOR 2u
#define FIRMWARE_VERSION_MINOR 0u
#define FIRMWARE_VERSION_PATCH 8u
#define FIRMWARE_VERSION_PACKED                                                \
  (((uint32_t)FIRMWARE_VERSION_MAJOR << 16) |                                  \
   ((uint32_t)FIRMWARE_VERSION_MINOR << 8) |                                   \
   ((uint32_t)FIRMWARE_VERSION_PATCH))

#define KBHE_FW_VERSION_RECORD_MAGIC 0x4B465756u

typedef struct __attribute__((packed)) {
  uint32_t magic;
  uint32_t version_packed;
  uint32_t version_xor;
} kbhe_fw_version_record_t;

#define KBHE_DECLARE_FIRMWARE_VERSION_RECORD(name)                             \
  __attribute__((used, section(".kbhe_fw_version")))                          \
  static const kbhe_fw_version_record_t name = {                               \
      .magic = KBHE_FW_VERSION_RECORD_MAGIC,                                   \
      .version_packed = FIRMWARE_VERSION_PACKED,                               \
      .version_xor = (uint32_t)(FIRMWARE_VERSION_PACKED ^ 0xFFFFFFFFu),         \
  }

#endif /* FIRMWARE_VERSION_H_ */
