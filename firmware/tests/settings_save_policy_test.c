#include "settings_save_policy.h"

#include <assert.h>
#include <stdio.h>

int main(void) {
  assert(!settings_persistence_busy_from_sources(false, false, false));
  assert(settings_persistence_busy_from_sources(true, false, false));
  assert(settings_persistence_busy_from_sources(false, true, false));
  assert(settings_persistence_busy_from_sources(false, false, true));
  assert(settings_persistence_busy_from_sources(true, true, true));

  assert(!settings_ram_only_entry_blocked(
      false, SETTINGS_SAVE_PHASE_IDLE, false, false));
  assert(settings_ram_only_entry_blocked(
      false, SETTINGS_SAVE_PHASE_IDLE, true, false));
  assert(settings_ram_only_entry_blocked(
      false, SETTINGS_SAVE_PHASE_IDLE, false, true));
  /* A settings-owned profile transaction can drain to its terminal result. */
  assert(!settings_ram_only_entry_blocked(
      true, SETTINGS_SAVE_PHASE_PROFILE_WAIT, true, true));
  /* The aggregate Flash phase owns only the raw writer, never a foreign
   * ProfileDocument/action result. */
  assert(!settings_ram_only_entry_blocked(
      true, SETTINGS_SAVE_PHASE_FLASH, false, true));
  assert(settings_ram_only_entry_blocked(
      true, SETTINGS_SAVE_PHASE_FLASH, true, true));
  /* Local staging does not confer ownership of an unrelated writer. */
  assert(settings_ram_only_entry_blocked(
      true, SETTINGS_SAVE_PHASE_PROFILE_COPY, true, false));
  assert(settings_ram_only_entry_blocked(
      true, SETTINGS_SAVE_PHASE_WAIT_FLASH, false, true));

  for (int phase = SETTINGS_SAVE_PHASE_IDLE;
       phase < SETTINGS_SAVE_PHASE_COUNT; phase++) {
    assert(settings_ram_only_save_transition(
               false, (settings_save_phase_t)phase) ==
           SETTINGS_RAM_ONLY_SAVE_NOOP);
  }

  assert(settings_ram_only_save_transition(
             true, SETTINGS_SAVE_PHASE_PROFILE_WAIT) ==
         SETTINGS_RAM_ONLY_SAVE_DRAIN_OWNED);
  assert(settings_ram_only_save_transition(true, SETTINGS_SAVE_PHASE_FLASH) ==
         SETTINGS_RAM_ONLY_SAVE_DRAIN_OWNED);

  const settings_save_phase_t cancellable[] = {
      SETTINGS_SAVE_PHASE_IDLE,
      SETTINGS_SAVE_PHASE_CRC,
      SETTINGS_SAVE_PHASE_PROFILE_BEGIN,
      SETTINGS_SAVE_PHASE_PROFILE_COPY,
      SETTINGS_SAVE_PHASE_WAIT_FLASH,
  };
  for (unsigned int index = 0u;
       index < sizeof(cancellable) / sizeof(cancellable[0]); index++) {
    assert(settings_ram_only_save_transition(true, cancellable[index]) ==
           SETTINGS_RAM_ONLY_SAVE_CANCEL);
  }

  puts("settings_save_policy_test: all tests passed");
  return 0;
}
