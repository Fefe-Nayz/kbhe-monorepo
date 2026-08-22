#ifndef SETTINGS_SAVE_POLICY_H
#define SETTINGS_SAVE_POLICY_H

#include <stdbool.h>

typedef enum {
  SETTINGS_SAVE_PHASE_IDLE = 0,
  SETTINGS_SAVE_PHASE_CRC,
  SETTINGS_SAVE_PHASE_PROFILE_BEGIN,
  SETTINGS_SAVE_PHASE_PROFILE_COPY,
  SETTINGS_SAVE_PHASE_PROFILE_WAIT,
  SETTINGS_SAVE_PHASE_WAIT_FLASH,
  SETTINGS_SAVE_PHASE_FLASH,
  SETTINGS_SAVE_PHASE_COUNT,
} settings_save_phase_t;

typedef enum {
  SETTINGS_RAM_ONLY_SAVE_NOOP = 0,
  SETTINGS_RAM_ONLY_SAVE_CANCEL,
  SETTINGS_RAM_ONLY_SAVE_DRAIN_OWNED,
} settings_ram_only_save_transition_t;

static inline bool settings_persistence_busy_from_sources(
    bool settings_save_busy, bool profile_document_busy,
    bool flash_writer_busy) {
  return settings_save_busy || profile_document_busy || flash_writer_busy;
}

static inline bool settings_save_owns_profile_document_transaction(
    bool save_in_progress, settings_save_phase_t phase) {
  return save_in_progress && phase == SETTINGS_SAVE_PHASE_PROFILE_WAIT;
}

static inline bool settings_save_owns_flash_transaction(
    bool save_in_progress, settings_save_phase_t phase) {
  return save_in_progress &&
         (phase == SETTINGS_SAVE_PHASE_PROFILE_WAIT ||
          phase == SETTINGS_SAVE_PHASE_FLASH);
}

/* External ProfileDocument/action terminal results must remain available to
 * their protocol owner. Likewise, a raw Flash writer is drainable here only
 * when the settings state machine owns the corresponding transaction. */
static inline bool settings_ram_only_entry_blocked(
    bool save_in_progress, settings_save_phase_t phase,
    bool profile_document_busy, bool flash_writer_busy) {
  return (profile_document_busy &&
          !settings_save_owns_profile_document_transaction(save_in_progress,
                                                            phase)) ||
         (flash_writer_busy &&
          !settings_save_owns_flash_transaction(save_in_progress, phase));
}

/* Once a journal writer is owned, its atomic transaction must reach a terminal
 * state.  Every earlier phase is local staging and can be cancelled without
 * publishing data. */
static inline settings_ram_only_save_transition_t
settings_ram_only_save_transition(bool save_in_progress,
                                  settings_save_phase_t phase) {
  if (!save_in_progress) {
    return SETTINGS_RAM_ONLY_SAVE_NOOP;
  }
  if (phase == SETTINGS_SAVE_PHASE_PROFILE_WAIT ||
      phase == SETTINGS_SAVE_PHASE_FLASH) {
    return SETTINGS_RAM_ONLY_SAVE_DRAIN_OWNED;
  }
  return SETTINGS_RAM_ONLY_SAVE_CANCEL;
}

#endif /* SETTINGS_SAVE_POLICY_H */
