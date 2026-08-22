#ifndef PROFILE_DOCUMENT_STORE_H_
#define PROFILE_DOCUMENT_STORE_H_

#include "action_engine.h"
#include "settings.h"

#include <stdbool.h>
#include <stdint.h>

#define PROFILE_DOCUMENT_SCHEMA_VERSION 3u
#define PROFILE_DOCUMENT_ASYNC_SOURCE_RESTART_LIMIT 4u

/** Read the committed document generation; absence is reported as generation 0. */
bool profile_document_store_get_generation(uint8_t profile_index,
                                           uint32_t *generation);

/** Boot/test helper; runtime HID paths use the incremental async retry match. */
bool profile_document_store_matches(uint8_t profile_index,
                                    const settings_profile_t *settings,
                                    const action_profile_t *actions,
                                    uint32_t *generation);

/** Load either or both canonical profile components from one committed object. */
bool profile_document_store_load(uint8_t profile_index,
                                 settings_profile_t *settings_out,
                                 action_profile_t *actions_out,
                                 uint32_t *generation);

/** Save both components under one journal commit, with optional generation CAS. */
bool profile_document_store_save(uint8_t profile_index,
                                 const settings_profile_t *settings,
                                 const action_profile_t *actions,
                                 uint32_t expected_generation,
                                 uint32_t *new_generation);

typedef enum {
  PROFILE_DOCUMENT_ASYNC_IDLE = 0,
  PROFILE_DOCUMENT_ASYNC_IN_PROGRESS,
  PROFILE_DOCUMENT_ASYNC_DONE,
  PROFILE_DOCUMENT_ASYNC_ERROR,
} profile_document_async_result_t;

/** Queue a canonical profile commit without doing Flash work in HID context. */
bool profile_document_store_save_async_begin(
    uint8_t profile_index, const settings_profile_t *settings,
    const action_profile_t *actions, uint32_t expected_generation);

/** Queue an async commit from live sources. If either optional revision source
 * changes while the two components are copied, the bounded state machine
 * restarts the copy instead of publishing a torn document. */
bool profile_document_store_save_async_begin_tracked(
    uint8_t profile_index, const settings_profile_t *settings,
    const action_profile_t *actions,
    const volatile uint32_t *settings_source_revision,
    const volatile uint32_t *actions_source_revision,
    uint32_t expected_generation);

/** Advance at most one bounded copy/validation/Flash budget unit. */
void profile_document_store_async_task(uint32_t copy_byte_budget,
                                       uint16_t flash_word_budget);

/** Observe the queued commit; generation is valid when the result is DONE. */
profile_document_async_result_t
profile_document_store_async_result(uint32_t *new_generation);

/** Release a terminal DONE/ERROR result after its protocol response is built. */
void profile_document_store_async_consume(void);

#endif /* PROFILE_DOCUMENT_STORE_H_ */
