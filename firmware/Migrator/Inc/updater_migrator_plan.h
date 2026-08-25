#ifndef UPDATER_MIGRATOR_PLAN_H_
#define UPDATER_MIGRATOR_PLAN_H_

#include <stdbool.h>
#include <stdint.h>

/* STM32 BOOT_ADD0 stores the reset address shifted right by 14. */
#define UPDATER_MIGRATOR_BOOTADDR_ITCM_FLASH 0x0080u
#define UPDATER_MIGRATOR_BOOTADDR_AXIM_FLASH 0x2000u
#define UPDATER_MIGRATOR_BOOTADDR_AXIM_APP 0x2004u

typedef enum {
  UPDATER_MIGRATOR_ACTION_FAIL_CLOSED = 0,
  UPDATER_MIGRATOR_ACTION_SET_BOOT_APP,
  UPDATER_MIGRATOR_ACTION_INSTALL_BOOTLOADER,
  UPDATER_MIGRATOR_ACTION_INSTALL_VERSION_FLOOR,
  UPDATER_MIGRATOR_ACTION_SET_BOOT_UPDATER,
  UPDATER_MIGRATOR_ACTION_ENTER_UPDATER,
} updater_migrator_action_t;

/* Pure, restart-safe migration planner. Persistent flash/option-byte state is
 * its only input, so a reset before or after any action simply asks for the
 * same action again or advances to the next verified phase. */
updater_migrator_action_t updater_migrator_plan_next(
    uint32_t bootaddr0, bool bootloader_installed, bool version_floor_ready);

#endif /* UPDATER_MIGRATOR_PLAN_H_ */
