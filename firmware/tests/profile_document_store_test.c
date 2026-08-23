#include "profile_document_store.h"

#include "flash_storage.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define TEST_DOCUMENT_HEADER_SIZE 16u

#define TEST_RAW_SIZE (sizeof(settings_profile_t) + sizeof(action_profile_t))
#define TEST_ENCODED_MAX (TEST_RAW_SIZE + ((TEST_RAW_SIZE + 127u) / 128u))
static uint8_t fake_payload[TEST_DOCUMENT_HEADER_SIZE + TEST_ENCODED_MAX];
static bool fake_present;
static uint16_t fake_schema;
static uint32_t fake_generation;
static uint32_t fake_length;
static bool fake_async_busy;
static uint16_t fake_async_namespace;
static uint16_t fake_async_object_id;
static uint32_t fake_async_begin_calls;
static uint32_t fake_async_step_calls;
static uint32_t fake_max_read_length;

action_validation_result_t
action_engine_validate_program(const action_program_t *program) {
  if (program == NULL || program->version != ACTION_PROGRAM_VERSION) {
    return ACTION_VALIDATE_BAD_VERSION;
  }
  if (program->step_count == 0u ||
      program->step_count > ACTION_PROGRAM_MAX_STEPS) {
    return ACTION_VALIDATE_TOO_MANY_STEPS;
  }
  return ACTION_VALIDATE_OK;
}

uint16_t
action_engine_program_macro_dependencies(const action_program_t *program) {
  uint64_t reachable = UINT64_C(1);
  uint16_t dependencies = 0u;
  if (action_engine_validate_program(program) != ACTION_VALIDATE_OK) {
    return 0u;
  }
  for (uint8_t i = 0u; i < program->step_count; i++) {
    const action_step_t *step = &program->steps[i];
    if ((reachable & (UINT64_C(1) << i)) == 0u) {
      continue;
    }
    if ((step->opcode == ACTION_OP_KEY_DOWN ||
         step->opcode == ACTION_OP_KEY_TAP) &&
        step->arg16 >= 0xF400u && step->arg16 <= 0xF40Fu) {
      dependencies |= (uint16_t)(1u << (step->arg16 - 0xF400u));
    }
    if (step->opcode == ACTION_OP_END) {
      continue;
    }
    if ((uint16_t)i + 1u < program->step_count) {
      reachable |= UINT64_C(1) << (i + 1u);
    }
    if (step->opcode == ACTION_OP_IF_STATE_SKIP &&
        (uint16_t)i + 1u + step->arg16 < program->step_count) {
      reachable |= UINT64_C(1) << (i + 1u + step->arg16);
    }
  }
  return dependencies;
}

action_validation_result_t
action_engine_validate_profile(const action_profile_t *profile) {
  uint16_t reach[ACTION_PROGRAM_COUNT] = {0};
  if (profile == NULL) {
    return ACTION_VALIDATE_BAD_ARGUMENT;
  }
  for (uint8_t i = 0u; i < ACTION_PROGRAM_COUNT; i++) {
    action_validation_result_t result =
        action_engine_validate_program(&profile->programs[i]);
    if (result != ACTION_VALIDATE_OK) {
      return result;
    }
    reach[i] = action_engine_program_macro_dependencies(&profile->programs[i]);
  }
  for (uint8_t via = 0u; via < ACTION_PROGRAM_COUNT; via++) {
    for (uint8_t i = 0u; i < ACTION_PROGRAM_COUNT; i++) {
      if ((reach[i] & (uint16_t)(1u << via)) != 0u) {
        reach[i] |= reach[via];
      }
    }
  }
  for (uint8_t i = 0u; i < ACTION_PROGRAM_COUNT; i++) {
    if ((reach[i] & (uint16_t)(1u << i)) != 0u) {
      return ACTION_VALIDATE_MACRO_CYCLE;
    }
  }
  for (uint8_t i = 0u; i < LED_STATE_OVERLAY_COUNT; i++) {
    const action_overlay_binding_t *binding = &profile->overlays[i];
    if (binding->state_index >= ACTION_STATE_COUNT ||
        binding->active_value > 1u || binding->follows_state > 1u ||
        binding->config.blend_mode >= (uint8_t)LED_OVERLAY_BLEND_MAX) {
      return ACTION_VALIDATE_BAD_ARGUMENT;
    }
  }
  return ACTION_VALIDATE_OK;
}

flash_storage_status_t flash_storage_object_read_range(
    uint16_t object_namespace, uint16_t object_id, uint32_t offset, void *buf,
    uint32_t len, uint32_t *actual_len, uint16_t *schema_version,
    uint32_t *generation) {
  if (object_namespace != FLASH_STORAGE_NAMESPACE_PROFILE_DOCUMENT ||
      object_id >= SETTINGS_PROFILE_COUNT || !fake_present) {
    return FLASH_STORAGE_STATUS_NOT_FOUND;
  }
  if (buf == NULL || offset > fake_length || len > fake_length - offset) {
    return FLASH_STORAGE_STATUS_BUFFER_TOO_SMALL;
  }
  if (actual_len != NULL) {
    *actual_len = fake_length;
  }
  if (schema_version != NULL) {
    *schema_version = fake_schema;
  }
  if (generation != NULL) {
    *generation = fake_generation;
  }
  if (len > fake_max_read_length) {
    fake_max_read_length = len;
  }
  memcpy(buf, fake_payload + offset, len);
  return FLASH_STORAGE_STATUS_OK;
}

flash_storage_status_t flash_storage_object_write_segments(
    uint16_t object_namespace, uint16_t object_id, uint16_t schema_version,
    const flash_storage_segment_t *segments, uint8_t segment_count,
    uint32_t expected_generation, uint32_t *new_generation) {
  uint32_t offset = 0u;

  if (object_namespace != FLASH_STORAGE_NAMESPACE_PROFILE_DOCUMENT ||
      object_id >= SETTINGS_PROFILE_COUNT || segments == NULL ||
      segment_count == 0u) {
    return FLASH_STORAGE_STATUS_INVALID_ARGUMENT;
  }
  if (expected_generation != FLASH_STORAGE_GENERATION_ANY &&
      expected_generation != (fake_present ? fake_generation : 0u)) {
    return FLASH_STORAGE_STATUS_GENERATION_MISMATCH;
  }
  for (uint8_t i = 0u; i < segment_count; i++) {
    assert(segments[i].data != NULL);
    assert(segments[i].length <= sizeof(fake_payload) - offset);
    memcpy(fake_payload + offset, segments[i].data, segments[i].length);
    offset += segments[i].length;
  }
  fake_present = true;
  fake_schema = schema_version;
  fake_length = offset;
  fake_generation++;
  if (fake_generation == 0u) {
    fake_generation = 1u;
  }
  if (new_generation != NULL) {
    *new_generation = fake_generation;
  }
  return FLASH_STORAGE_STATUS_OK;
}

flash_storage_status_t flash_storage_object_write_segments_async_begin(
    uint16_t object_namespace, uint16_t object_id, uint16_t schema_version,
    const flash_storage_segment_t *segments, uint8_t segment_count,
    uint32_t expected_generation, uint32_t *new_generation) {
  flash_storage_status_t status;

  fake_async_begin_calls++;
  if (fake_async_busy) {
    return FLASH_STORAGE_STATUS_BUSY;
  }
  status = flash_storage_object_write_segments(
      object_namespace, object_id, schema_version, segments, segment_count,
      expected_generation, new_generation);
  if (status == FLASH_STORAGE_STATUS_OK) {
    fake_async_busy = true;
    fake_async_namespace = object_namespace;
    fake_async_object_id = object_id;
  }
  return status;
}

flash_storage_async_result_t flash_storage_write_async_step(uint16_t max_words) {
  if (!fake_async_busy) {
    return FLASH_STORAGE_ASYNC_DONE;
  }
  if (max_words == 0u) {
    return FLASH_STORAGE_ASYNC_IN_PROGRESS;
  }
  fake_async_step_calls++;
  fake_async_busy = false;
  return FLASH_STORAGE_ASYNC_DONE;
}

bool flash_storage_write_async_is_busy(void) { return fake_async_busy; }

bool flash_storage_write_async_is_owner(uint16_t object_namespace,
                                        uint16_t object_id) {
  return fake_async_busy && fake_async_namespace == object_namespace &&
         fake_async_object_id == object_id;
}

static void valid_actions(action_profile_t *actions) {
  memset(actions, 0, sizeof(*actions));
  for (uint8_t i = 0u; i < ACTION_PROGRAM_COUNT; i++) {
    actions->programs[i].version = ACTION_PROGRAM_VERSION;
    actions->programs[i].step_count = 1u;
    actions->programs[i].steps[0].opcode = ACTION_OP_END;
  }
}

static void reset_fake_store(void) {
  memset(fake_payload, 0, sizeof(fake_payload));
  fake_present = false;
  fake_schema = 0u;
  fake_generation = 0u;
  fake_length = 0u;
  fake_async_busy = false;
  fake_async_namespace = 0u;
  fake_async_object_id = 0u;
  fake_async_begin_calls = 0u;
  fake_async_step_calls = 0u;
  fake_max_read_length = 0u;
}

static void test_absent_document_has_zero_generation(void) {
  uint32_t generation = UINT32_MAX;
  reset_fake_store();
  assert(profile_document_store_get_generation(0u, &generation));
  assert(generation == 0u);
  assert(!profile_document_store_get_generation(SETTINGS_PROFILE_COUNT,
                                                &generation));
}

static void test_segmented_round_trip_and_cas(void) {
  static settings_profile_t settings_in;
  static settings_profile_t settings_out;
  static action_profile_t actions_in;
  static action_profile_t actions_out;
  uint32_t generation = 0u;

  reset_fake_store();
  memset(&settings_in, 0x5Au, sizeof(settings_in));
  valid_actions(&actions_in);
  actions_in.initial_state_bits = 0x1201u;

  assert(profile_document_store_save(2u, &settings_in, &actions_in, 0u,
                                     &generation));
  assert(generation == 1u);
  assert(fake_length > TEST_DOCUMENT_HEADER_SIZE);
  assert(fake_length < TEST_DOCUMENT_HEADER_SIZE + TEST_RAW_SIZE);
  assert(profile_document_store_get_generation(2u, &generation));
  assert(generation == 1u);
  assert(profile_document_store_load(2u, &settings_out, &actions_out,
                                     &generation));
  assert(generation == 1u);
  assert(memcmp(&settings_in, &settings_out, sizeof(settings_in)) == 0);
  assert(memcmp(&actions_in, &actions_out, sizeof(actions_in)) == 0);
  assert(profile_document_store_matches(2u, &settings_in, &actions_in,
                                        &generation));
  actions_in.initial_state_bits ^= 1u;
  assert(!profile_document_store_matches(2u, &settings_in, &actions_in,
                                         NULL));
  actions_in.initial_state_bits ^= 1u;

  assert(!profile_document_store_save(2u, &settings_in, &actions_in, 0u,
                                      NULL));
  assert(profile_document_store_save(2u, &settings_in, &actions_in, 1u,
                                     &generation));
  assert(generation == 2u);
}

static void test_malformed_document_or_actions_are_rejected(void) {
  static settings_profile_t settings;
  static action_profile_t actions;
  static action_profile_t output;
  static action_profile_t sentinel;
  uint32_t generation = 0u;

  reset_fake_store();
  memset(&settings, 0, sizeof(settings));
  valid_actions(&actions);
  actions.programs[3].version = 0u;
  assert(!profile_document_store_save(0u, &settings, &actions, 0u, NULL));

  valid_actions(&actions);
  actions.programs[3].step_count = 2u;
  actions.programs[3].steps[0].opcode = ACTION_OP_KEY_TAP;
  actions.programs[3].steps[0].arg16 = 0xF403u;
  actions.programs[3].steps[1].opcode = ACTION_OP_END;
  assert(!profile_document_store_save(0u, &settings, &actions, 0u, NULL));

  valid_actions(&actions);
  assert(profile_document_store_save(0u, &settings, &actions, 0u, NULL));
  memset(&sentinel, 0xA5, sizeof(sentinel));
  memcpy(&output, &sentinel, sizeof(output));
  /* A match with offset zero is never a valid compressed stream. */
  fake_payload[TEST_DOCUMENT_HEADER_SIZE] = 0x80u;
  fake_payload[TEST_DOCUMENT_HEADER_SIZE + 1u] = 0u;
  fake_payload[TEST_DOCUMENT_HEADER_SIZE + 2u] = 0u;
  assert(!profile_document_store_load(0u, NULL, &output, NULL));
  assert(memcmp(&output, &sentinel, sizeof(output)) == 0);

  /* Restore the valid document, then corrupt its metadata header. */
  assert(profile_document_store_save(
      0u, &settings, &actions, FLASH_STORAGE_GENERATION_ANY, NULL));
  fake_payload[0] ^= 0xFFu;
  assert(!profile_document_store_get_generation(0u, &generation));
  assert(!profile_document_store_load(0u, &settings, NULL, NULL));
}

static void test_async_live_source_mutation_restarts_without_torn_snapshot(void) {
  static settings_profile_t settings;
  static settings_profile_t loaded_settings;
  static action_profile_t actions;
  static action_profile_t loaded_actions;
  volatile uint32_t settings_revision = 7u;
  volatile uint32_t actions_revision = 11u;

  reset_fake_store();
  memset(&settings, 0x31, sizeof(settings));
  valid_actions(&actions);
  assert(profile_document_store_save_async_begin_tracked(
      0u, &settings, &actions, &settings_revision, &actions_revision, 0u));

  /* Mutate bytes already copied by the first bounded slice. The revision
   * change forces a complete async restart instead of mixing old/new bytes. */
  profile_document_store_async_task(32u, 1u);
  ((uint8_t *)&settings)[0] = 0xC7u;
  ((uint8_t *)&settings)[1] = 0x5Eu;
  settings_revision++;
  for (uint32_t i = 0u;
       i < (sizeof(settings) - 32u + 31u) / 32u; i++) {
    profile_document_store_async_task(32u, 1u);
  }
  profile_document_store_async_task(32u, 1u); /* first actions slice */
  actions.programs[0].flags = ACTION_PROGRAM_FLAG_RESTART_ON_TRIGGER;
  actions_revision++;
  for (uint32_t i = 0u; i < 20000u &&
                         profile_document_store_async_result(NULL) ==
                             PROFILE_DOCUMENT_ASYNC_IN_PROGRESS;
       i++) {
    profile_document_store_async_task(32u, 1u);
  }
  assert(profile_document_store_async_result(NULL) ==
         PROFILE_DOCUMENT_ASYNC_DONE);
  profile_document_store_async_consume();
  assert(profile_document_store_load(0u, &loaded_settings, &loaded_actions,
                                     NULL));
  assert(memcmp(&loaded_settings, &settings, sizeof(settings)) == 0);
  assert(memcmp(&loaded_actions, &actions, sizeof(actions)) == 0);
}

static void test_async_live_source_restart_is_bounded(void) {
  static settings_profile_t settings;
  static action_profile_t actions;
  volatile uint32_t settings_revision = 1u;

  reset_fake_store();
  memset(&settings, 0x42, sizeof(settings));
  valid_actions(&actions);
  assert(profile_document_store_save_async_begin_tracked(
      0u, &settings, &actions, &settings_revision, NULL, 0u));
  for (uint32_t i = 0u; i < 5000u &&
                         profile_document_store_async_result(NULL) ==
                             PROFILE_DOCUMENT_ASYNC_IN_PROGRESS;
       i++) {
    profile_document_store_async_task(128u, 1u);
    ((uint8_t *)&settings)[0]++;
    settings_revision++;
  }
  assert(profile_document_store_async_result(NULL) ==
         PROFILE_DOCUMENT_ASYNC_ERROR);
  assert(fake_async_begin_calls == 0u);
  profile_document_store_async_consume();
}

static void test_async_validates_macro_depth_fanout_and_cycle(void) {
  static settings_profile_t settings;
  static action_profile_t actions;

  reset_fake_store();
  memset(&settings, 0, sizeof(settings));
  valid_actions(&actions);
  for (uint8_t program = 0u; program < ACTION_ENGINE_MAX_INSTANCES - 1u;
       program++) {
    actions.programs[program].step_count = 2u;
    actions.programs[program].steps[0].opcode = ACTION_OP_KEY_TAP;
    actions.programs[program].steps[0].arg16 =
        (uint16_t)(0xF400u + program + 1u);
    actions.programs[program].steps[1].opcode = ACTION_OP_END;
  }
  assert(profile_document_store_save_async_begin(0u, &settings, &actions,
                                                 0u));
  for (uint32_t i = 0u;
       i < 20000u && profile_document_store_async_result(NULL) ==
                           PROFILE_DOCUMENT_ASYNC_IN_PROGRESS;
       i++) {
    profile_document_store_async_task(128u, 1u);
  }
  assert(profile_document_store_async_result(NULL) ==
         PROFILE_DOCUMENT_ASYNC_DONE);
  profile_document_store_async_consume();

  /* The same chain with a fifth nesting level must fail before compression or
   * Flash ownership is attempted. */
  reset_fake_store();
  valid_actions(&actions);
  for (uint8_t program = 0u; program < ACTION_ENGINE_MAX_INSTANCES;
       program++) {
    actions.programs[program].step_count = 2u;
    actions.programs[program].steps[0].opcode = ACTION_OP_KEY_TAP;
    actions.programs[program].steps[0].arg16 =
        (uint16_t)(0xF400u + program + 1u);
    actions.programs[program].steps[1].opcode = ACTION_OP_END;
  }
  assert(profile_document_store_save_async_begin(0u, &settings, &actions,
                                                 0u));
  for (uint32_t i = 0u;
       i < 20000u && profile_document_store_async_result(NULL) ==
                           PROFILE_DOCUMENT_ASYNC_IN_PROGRESS;
       i++) {
    profile_document_store_async_task(128u, 1u);
  }
  assert(profile_document_store_async_result(NULL) ==
         PROFILE_DOCUMENT_ASYNC_ERROR);
  assert(fake_async_begin_calls == 0u);
  profile_document_store_async_consume();

  reset_fake_store();
  valid_actions(&actions);
  actions.programs[0].step_count = 3u;
  actions.programs[0].steps[0].opcode = ACTION_OP_KEY_TAP;
  actions.programs[0].steps[0].arg16 = 0xF401u;
  actions.programs[0].steps[1].opcode = ACTION_OP_KEY_TAP;
  actions.programs[0].steps[1].arg16 = 0xF402u;
  actions.programs[0].steps[2].opcode = ACTION_OP_END;
  for (uint8_t program = 1u; program <= 2u; program++) {
    actions.programs[program].step_count = 2u;
    actions.programs[program].steps[0].opcode = ACTION_OP_KEY_TAP;
    actions.programs[program].steps[0].arg16 = 0xF403u;
    actions.programs[program].steps[1].opcode = ACTION_OP_END;
  }
  assert(profile_document_store_save_async_begin(0u, &settings, &actions,
                                                 0u));
  for (uint32_t i = 0u;
       i < 20000u && profile_document_store_async_result(NULL) ==
                           PROFILE_DOCUMENT_ASYNC_IN_PROGRESS;
       i++) {
    profile_document_store_async_task(128u, 1u);
  }
  assert(profile_document_store_async_result(NULL) ==
         PROFILE_DOCUMENT_ASYNC_DONE);
  profile_document_store_async_consume();

  reset_fake_store();
  valid_actions(&actions);
  actions.programs[0].step_count = 2u;
  actions.programs[0].steps[0].opcode = ACTION_OP_KEY_TAP;
  actions.programs[0].steps[0].arg16 = 0xF401u;
  actions.programs[0].steps[1].opcode = ACTION_OP_END;
  actions.programs[1].step_count = 2u;
  actions.programs[1].steps[0].opcode = ACTION_OP_KEY_TAP;
  actions.programs[1].steps[0].arg16 = 0xF400u;
  actions.programs[1].steps[1].opcode = ACTION_OP_END;
  assert(profile_document_store_save_async_begin(0u, &settings, &actions,
                                                 0u));
  for (uint32_t i = 0u;
       i < 20000u && profile_document_store_async_result(NULL) ==
                           PROFILE_DOCUMENT_ASYNC_IN_PROGRESS;
       i++) {
    profile_document_store_async_task(128u, 1u);
  }
  assert(profile_document_store_async_result(NULL) ==
         PROFILE_DOCUMENT_ASYNC_ERROR);
  assert(fake_async_begin_calls == 0u);
  profile_document_store_async_consume();
}

static void test_async_profile_waits_for_settings_writer_and_owns_its_step(void) {
  static settings_profile_t settings;
  static action_profile_t actions;
  /* Includes snapshot copy, validation, incremental hash-table clear and
   * bounded compression. The exact encoded size is intentionally opaque. */
  const uint32_t copy_and_validation_steps = 2500u;
  uint32_t settings_step_calls = 0u;

  reset_fake_store();
  memset(&settings, 0x35, sizeof(settings));
  valid_actions(&actions);

  /* Model settings already owning the singleton journal when a profile save
   * reaches its Flash phase. The profile may prepare its immutable snapshot,
   * but it must not advance or consume the settings transaction. */
  fake_async_busy = true;
  fake_async_namespace = FLASH_STORAGE_NAMESPACE_SETTINGS;
  fake_async_object_id = FLASH_STORAGE_SETTINGS_OBJECT_ID;
  assert(profile_document_store_save_async_begin(1u, &settings, &actions,
                                                 0u));
  for (uint32_t i = 0u; i < copy_and_validation_steps; i++) {
    profile_document_store_async_task(32u, 1u);
  }
  assert(fake_async_begin_calls != 0u);
  assert(fake_async_step_calls == 0u);
  assert(flash_storage_write_async_is_owner(
      FLASH_STORAGE_NAMESPACE_SETTINGS, FLASH_STORAGE_SETTINGS_OBJECT_ID));
  assert(profile_document_store_async_result(NULL) ==
         PROFILE_DOCUMENT_ASYNC_IN_PROGRESS);

  /* Let settings consume its own word budget, then the queued profile begins
   * and consumes only its own transaction. A zero budget is a strict no-op. */
  assert(flash_storage_write_async_step(1u) == FLASH_STORAGE_ASYNC_DONE);
  settings_step_calls = fake_async_step_calls;
  profile_document_store_async_task(32u, 1u);
  assert(flash_storage_write_async_is_owner(
      FLASH_STORAGE_NAMESPACE_PROFILE_DOCUMENT, 1u));
  profile_document_store_async_task(32u, 0u);
  assert(fake_async_step_calls == settings_step_calls);
  profile_document_store_async_task(32u, 1u);
  assert(fake_async_step_calls == settings_step_calls + 1u);
  assert(profile_document_store_async_result(NULL) ==
         PROFILE_DOCUMENT_ASYNC_DONE);
  profile_document_store_async_consume();
}

static void test_async_retry_match_is_incremental_and_avoids_flash(void) {
  static settings_profile_t settings;
  static action_profile_t actions;
  uint32_t generation = 0u;

  reset_fake_store();
  memset(&settings, 0x5Au, sizeof(settings));
  valid_actions(&actions);
  assert(profile_document_store_save(0u, &settings, &actions, 0u,
                                     &generation));
  assert(generation == 1u);
  fake_async_begin_calls = 0u;
  fake_max_read_length = 0u;

  /* Retry the generation-0 CAS after generation 1 was already committed. The
   * state machine compares at most 32 stored bytes per task invocation and
   * returns the committed generation without starting a Flash transaction. */
  assert(profile_document_store_save_async_begin(0u, &settings, &actions,
                                                 0u));
  for (uint32_t i = 0u; i < 6000u &&
                         profile_document_store_async_result(NULL) ==
                             PROFILE_DOCUMENT_ASYNC_IN_PROGRESS;
       i++) {
    profile_document_store_async_task(32u, 1u);
  }
  assert(profile_document_store_async_result(&generation) ==
         PROFILE_DOCUMENT_ASYNC_DONE);
  assert(generation == 1u);
  assert(fake_async_begin_calls == 0u);
  assert(fake_async_step_calls == 0u);
  assert(fake_max_read_length <= 32u);
  profile_document_store_async_consume();
}

int main(void) {
  test_absent_document_has_zero_generation();
  test_segmented_round_trip_and_cas();
  test_malformed_document_or_actions_are_rejected();
  test_async_live_source_mutation_restarts_without_torn_snapshot();
  test_async_live_source_restart_is_bounded();
  test_async_validates_macro_depth_fanout_and_cycle();
  test_async_profile_waits_for_settings_writer_and_owns_its_step();
  test_async_retry_match_is_incremental_and_avoids_flash();
  puts("profile_document_store_test: ok");
  return 0;
}
