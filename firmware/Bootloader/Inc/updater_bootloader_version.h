#ifndef UPDATER_BOOTLOADER_VERSION_H_
#define UPDATER_BOOTLOADER_VERSION_H_

#include "updater_shared.h"

/*
 * Version of the resident updater itself, independent from the application
 * release version. Bump this only when the bootloader binary changes in a way
 * that deployed keyboards must receive.
 */
#define UPDATER_BOOTLOADER_VERSION_MAJOR 1u
#define UPDATER_BOOTLOADER_VERSION_MINOR 0u
#define UPDATER_BOOTLOADER_VERSION_PATCH 0u
#define UPDATER_BOOTLOADER_VERSION_PACKED                                  \
  (((uint32_t)UPDATER_BOOTLOADER_VERSION_MAJOR << 16) |                    \
   ((uint32_t)UPDATER_BOOTLOADER_VERSION_MINOR << 8) |                     \
   ((uint32_t)UPDATER_BOOTLOADER_VERSION_PATCH))

#define UPDATER_DECLARE_BOOTLOADER_VERSION_RECORD(name)                    \
  __attribute__((used, section(".kbhe_bl_version")))                       \
  static const updater_bootloader_version_record_t name = {                \
      .magic = UPDATER_BOOTLOADER_VERSION_RECORD_MAGIC,                    \
      .version_packed = UPDATER_BOOTLOADER_VERSION_PACKED,                 \
      .version_xor =                                                       \
          (uint32_t)(UPDATER_BOOTLOADER_VERSION_PACKED ^ 0xFFFFFFFFu),      \
  }

#endif /* UPDATER_BOOTLOADER_VERSION_H_ */
