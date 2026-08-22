#ifndef UPDATER_VERSION_FLOOR_H_
#define UPDATER_VERSION_FLOOR_H_

#include "updater_shared.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Sector 3 is deliberately outside both the executable bootloader image and
 * the application/profile regions.  Records are append-only and this sector
 * is never erased in the field, so a power failure cannot lower the floor. */
#define UPDATER_VERSION_FLOOR_MAGIC 0x4B46564CUL /* "KFVL" */
#define UPDATER_VERSION_FLOOR_COMMIT_MAGIC 0x434F4D54UL /* "COMT" */
#define UPDATER_VERSION_FLOOR_ENTRY_SIZE 16UL

typedef enum {
  UPDATER_VERSION_FLOOR_OK = 0,
  UPDATER_VERSION_FLOOR_ROLLBACK,
  UPDATER_VERSION_FLOOR_STORAGE_ERROR,
} updater_version_floor_result_t;

/** Read the greatest committed firmware version floor, if one exists. */
bool updater_version_floor_read(updater_fw_version_t *version_out);

/**
 * Return true when candidate is at least the greatest committed floor.
 * An entirely uninitialized journal has no floor and therefore allows the
 * first signed image. A programmed journal with no valid committed record is
 * treated as corrupt and fails closed. This check must gate every jump to the
 * application, not just BEGIN, because power can fail after the floor is
 * committed but before the previous application slot is erased.
 */
bool updater_version_floor_allows(updater_fw_version_t candidate);

/**
 * Atomically raise the durable floor before erasing the application.
 * Equal versions are accepted to permit recovery after an interrupted update;
 * the caller separately rejects equality while a valid application exists.
 * A programmed journal with no valid committed record fails closed and
 * requires an explicit physical factory recovery.
 */
updater_version_floor_result_t updater_version_floor_prepare(
    updater_fw_version_t candidate);

#ifdef __cplusplus
}
#endif

#endif /* UPDATER_VERSION_FLOOR_H_ */
