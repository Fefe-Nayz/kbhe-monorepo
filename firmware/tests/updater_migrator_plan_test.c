#include "updater_migrator_plan.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {
  uint32_t bootaddr0;
  bool bootloader_installed;
  bool version_floor_ready;
} simulated_state_t;

static updater_migrator_action_t next(simulated_state_t state) {
  return updater_migrator_plan_next(
      state.bootaddr0, state.bootloader_installed,
      state.version_floor_ready);
}

static void test_all_persistent_state_combinations(void) {
  const uint32_t updater_bootaddrs[] = {
      UPDATER_MIGRATOR_BOOTADDR_ITCM_FLASH,
      UPDATER_MIGRATOR_BOOTADDR_AXIM_FLASH,
  };

  for (size_t i = 0u; i < sizeof(updater_bootaddrs) /
                                 sizeof(updater_bootaddrs[0]);
       i++) {
    for (unsigned installed = 0u; installed <= 1u; installed++) {
      for (unsigned floor = 0u; floor <= 1u; floor++) {
        simulated_state_t state = {
            .bootaddr0 = updater_bootaddrs[i],
            .bootloader_installed = installed != 0u,
            .version_floor_ready = floor != 0u,
        };
        assert(next(state) ==
               ((installed != 0u && floor != 0u)
                    ? UPDATER_MIGRATOR_ACTION_ENTER_UPDATER
                    : UPDATER_MIGRATOR_ACTION_SET_BOOT_APP));
      }
    }
  }

  assert(next((simulated_state_t){
             .bootaddr0 = UPDATER_MIGRATOR_BOOTADDR_AXIM_APP,
         }) == UPDATER_MIGRATOR_ACTION_INSTALL_BOOTLOADER);
  assert(next((simulated_state_t){
             .bootaddr0 = UPDATER_MIGRATOR_BOOTADDR_AXIM_APP,
             .version_floor_ready = true,
         }) == UPDATER_MIGRATOR_ACTION_INSTALL_BOOTLOADER);
  assert(next((simulated_state_t){
             .bootaddr0 = UPDATER_MIGRATOR_BOOTADDR_AXIM_APP,
             .bootloader_installed = true,
         }) == UPDATER_MIGRATOR_ACTION_INSTALL_VERSION_FLOOR);
  assert(next((simulated_state_t){
             .bootaddr0 = UPDATER_MIGRATOR_BOOTADDR_AXIM_APP,
             .bootloader_installed = true,
             .version_floor_ready = true,
         }) == UPDATER_MIGRATOR_ACTION_SET_BOOT_UPDATER);
}

static void test_unknown_boot_addresses_fail_closed(void) {
  const uint32_t unknown[] = {0u, 0x007Fu, 0x0081u, 0x1FFFu, 0x2001u,
                              0x2003u, 0x2005u, UINT32_MAX};
  for (size_t i = 0u; i < sizeof(unknown) / sizeof(unknown[0]); i++) {
    for (unsigned installed = 0u; installed <= 1u; installed++) {
      for (unsigned floor = 0u; floor <= 1u; floor++) {
        assert(next((simulated_state_t){
                   .bootaddr0 = unknown[i],
                   .bootloader_installed = installed != 0u,
                   .version_floor_ready = floor != 0u,
               }) == UPDATER_MIGRATOR_ACTION_FAIL_CLOSED);
      }
    }
  }
}

static void test_every_power_cut_boundary_resumes(void) {
  simulated_state_t state = {
      .bootaddr0 = UPDATER_MIGRATOR_BOOTADDR_AXIM_FLASH,
  };
  const updater_migrator_action_t expected[] = {
      UPDATER_MIGRATOR_ACTION_SET_BOOT_APP,
      UPDATER_MIGRATOR_ACTION_INSTALL_BOOTLOADER,
      UPDATER_MIGRATOR_ACTION_INSTALL_VERSION_FLOOR,
      UPDATER_MIGRATOR_ACTION_SET_BOOT_UPDATER,
      UPDATER_MIGRATOR_ACTION_ENTER_UPDATER,
  };

  for (size_t step = 0u; step < sizeof(expected) / sizeof(expected[0]);
       step++) {
    updater_migrator_action_t action = next(state);
    assert(action == expected[step]);

    /* A cut before the persistent mutation must request the identical action. */
    assert(next(state) == action);

    switch (action) {
    case UPDATER_MIGRATOR_ACTION_SET_BOOT_APP:
      state.bootaddr0 = UPDATER_MIGRATOR_BOOTADDR_AXIM_APP;
      break;
    case UPDATER_MIGRATOR_ACTION_INSTALL_BOOTLOADER:
      state.bootloader_installed = true;
      break;
    case UPDATER_MIGRATOR_ACTION_INSTALL_VERSION_FLOOR:
      state.version_floor_ready = true;
      break;
    case UPDATER_MIGRATOR_ACTION_SET_BOOT_UPDATER:
      state.bootaddr0 = UPDATER_MIGRATOR_BOOTADDR_AXIM_FLASH;
      break;
    case UPDATER_MIGRATOR_ACTION_ENTER_UPDATER:
      break;
    case UPDATER_MIGRATOR_ACTION_FAIL_CLOSED:
    default:
      assert(false);
    }
  }
}

int main(void) {
  test_all_persistent_state_combinations();
  test_unknown_boot_addresses_fail_closed();
  test_every_power_cut_boundary_resumes();
  puts("updater_migrator_plan_test: ok");
  return 0;
}
