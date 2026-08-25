#include "updater_migrator_plan.h"

updater_migrator_action_t updater_migrator_plan_next(
    uint32_t bootaddr0, bool bootloader_installed, bool version_floor_ready) {
  bool bootaddr_is_updater =
      bootaddr0 == UPDATER_MIGRATOR_BOOTADDR_ITCM_FLASH ||
      bootaddr0 == UPDATER_MIGRATOR_BOOTADDR_AXIM_FLASH;

  if (!bootaddr_is_updater &&
      bootaddr0 != UPDATER_MIGRATOR_BOOTADDR_AXIM_APP) {
    return UPDATER_MIGRATOR_ACTION_FAIL_CLOSED;
  }

  if (bootaddr_is_updater) {
    if (bootloader_installed && version_floor_ready) {
      return UPDATER_MIGRATOR_ACTION_ENTER_UPDATER;
    }
    return UPDATER_MIGRATOR_ACTION_SET_BOOT_APP;
  }

  if (!bootloader_installed) {
    return UPDATER_MIGRATOR_ACTION_INSTALL_BOOTLOADER;
  }
  if (!version_floor_ready) {
    return UPDATER_MIGRATOR_ACTION_INSTALL_VERSION_FLOOR;
  }
  return UPDATER_MIGRATOR_ACTION_SET_BOOT_UPDATER;
}
