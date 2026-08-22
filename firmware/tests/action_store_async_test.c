#include "action_engine.h"
#include "flash_storage.h"
#include "profile_document_store.h"
#include "settings.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static settings_profile_t fake_settings;
static profile_document_async_result_t fake_result;
static const action_profile_t *queued_actions;
static uint32_t begin_calls;
static uint32_t consume_calls;
static volatile uint32_t settings_revision;

action_validation_result_t
action_engine_validate_program(const action_program_t *program) {
  return program != NULL && program->version == ACTION_PROGRAM_VERSION &&
                 program->step_count == 1u &&
                 program->steps[0].opcode == ACTION_OP_END
             ? ACTION_VALIDATE_OK
             : ACTION_VALIDATE_BAD_VERSION;
}

action_validation_result_t
action_engine_validate_profile(const action_profile_t *profile) {
  if (profile == NULL) {
    return ACTION_VALIDATE_BAD_ARGUMENT;
  }
  for (uint8_t i = 0u; i < ACTION_PROGRAM_COUNT; i++) {
    if (action_engine_validate_program(&profile->programs[i]) !=
        ACTION_VALIDATE_OK) {
      return ACTION_VALIDATE_BAD_VERSION;
    }
  }
  return ACTION_VALIDATE_OK;
}

const settings_profile_t *
settings_profile_snapshot_view(uint8_t profile_index) {
  return profile_index < SETTINGS_PROFILE_COUNT ? &fake_settings : NULL;
}

const volatile uint32_t *
settings_profile_snapshot_revision_source(uint8_t profile_index) {
  return profile_index < SETTINGS_PROFILE_COUNT ? &settings_revision : NULL;
}

bool profile_document_store_save_async_begin(
    uint8_t profile_index, const settings_profile_t *settings,
    const action_profile_t *actions, uint32_t expected_generation) {
  assert(profile_index < SETTINGS_PROFILE_COUNT);
  assert(settings == &fake_settings);
  assert(actions != NULL);
  assert(expected_generation == FLASH_STORAGE_GENERATION_ANY);
  assert(fake_result == PROFILE_DOCUMENT_ASYNC_IDLE);
  begin_calls++;
  queued_actions = actions;
  fake_result = PROFILE_DOCUMENT_ASYNC_IN_PROGRESS;
  return true;
}

bool profile_document_store_save_async_begin_tracked(
    uint8_t profile_index, const settings_profile_t *settings,
    const action_profile_t *actions,
    const volatile uint32_t *settings_source_revision,
    const volatile uint32_t *actions_source_revision,
    uint32_t expected_generation) {
  assert(settings_source_revision == &settings_revision);
  assert(actions_source_revision == NULL);
  return profile_document_store_save_async_begin(
      profile_index, settings, actions, expected_generation);
}

profile_document_async_result_t
profile_document_store_async_result(uint32_t *new_generation) {
  (void)new_generation;
  return fake_result;
}

void profile_document_store_async_consume(void) {
  assert(fake_result == PROFILE_DOCUMENT_ASYNC_DONE ||
         fake_result == PROFILE_DOCUMENT_ASYNC_ERROR);
  consume_calls++;
  fake_result = PROFILE_DOCUMENT_ASYNC_IDLE;
  queued_actions = NULL;
}

bool profile_document_store_load(uint8_t profile_index,
                                 settings_profile_t *settings_out,
                                 action_profile_t *actions_out,
                                 uint32_t *generation) {
  (void)profile_index;
  (void)settings_out;
  (void)actions_out;
  (void)generation;
  return false;
}

flash_storage_status_t flash_storage_object_read_range(
    uint16_t object_namespace, uint16_t object_id, uint32_t offset, void *buf,
    uint32_t len, uint32_t *actual_len, uint16_t *schema_version,
    uint32_t *generation) {
  (void)object_namespace;
  (void)object_id;
  (void)offset;
  (void)buf;
  (void)len;
  (void)actual_len;
  (void)schema_version;
  (void)generation;
  return FLASH_STORAGE_STATUS_NOT_FOUND;
}

flash_storage_status_t flash_storage_object_read(
    uint16_t object_namespace, uint16_t object_id, void *buf,
    uint32_t capacity, uint32_t *actual_len, uint16_t *schema_version,
    uint32_t *generation) {
  (void)object_namespace;
  (void)object_id;
  (void)buf;
  (void)capacity;
  (void)actual_len;
  (void)schema_version;
  (void)generation;
  return FLASH_STORAGE_STATUS_NOT_FOUND;
}

static void make_valid_profile(action_profile_t *profile,
                               uint16_t initial_state_bits) {
  memset(profile, 0, sizeof(*profile));
  profile->initial_state_bits = initial_state_bits;
  for (uint8_t i = 0u; i < ACTION_PROGRAM_COUNT; i++) {
    profile->programs[i].version = ACTION_PROGRAM_VERSION;
    profile->programs[i].step_count = 1u;
    profile->programs[i].steps[0].opcode = ACTION_OP_END;
  }
}

int main(void) {
  action_profile_t first;
  action_profile_t second;

  fake_result = PROFILE_DOCUMENT_ASYNC_IDLE;
  make_valid_profile(&first, 0x1234u);
  make_valid_profile(&second, 0x5678u);

  assert(action_store_save_profile(2u, &first));
  assert(begin_calls == 1u);
  assert(queued_actions != &first);
  first.initial_state_bits = 0u;
  assert(queued_actions->initial_state_bits == 0x1234u);

  /* The immutable source cannot be overwritten while its async copy is live. */
  assert(!action_store_save_profile(2u, &second));
  assert(begin_calls == 1u);
  action_store_async_task();
  assert(consume_calls == 0u);

  fake_result = PROFILE_DOCUMENT_ASYNC_DONE;
  action_store_async_task();
  assert(consume_calls == 1u);
  assert(action_store_save_profile(2u, &second));
  assert(begin_calls == 2u);
  assert(queued_actions->initial_state_bits == 0x5678u);

  fake_result = PROFILE_DOCUMENT_ASYNC_ERROR;
  action_store_async_task();
  assert(consume_calls == 2u);
  puts("action_store_async_test: ok");
  return 0;
}
