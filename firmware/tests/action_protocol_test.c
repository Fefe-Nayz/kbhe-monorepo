#include "action_protocol.h"

#include "action_engine.h"
#include "flash_storage.h"
#include "hid_protocol.h"
#include "profile_document_store.h"
#include "settings.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static settings_profile_t settings_profile;
static action_profile_t action_profile;
static uint32_t document_generation;
static uint32_t save_calls;
static profile_document_async_result_t async_result;
static volatile uint32_t settings_revision;
static volatile uint32_t action_revision;
static uint32_t set_program_calls;
static uint32_t publish_program_calls;
static uint32_t set_profile_calls;
static uint32_t set_overlay_calls;
static uint32_t publish_overlay_calls;
static uint32_t validate_program_calls;
static uint32_t validate_profile_calls;
static uint16_t runtime_state_bits;

static uint32_t read_u32_le(const uint8_t *bytes) {
  return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8u) |
         ((uint32_t)bytes[2] << 16u) | ((uint32_t)bytes[3] << 24u);
}

static void write_u32_le(uint8_t *bytes, uint32_t value) {
  bytes[0] = (uint8_t)value;
  bytes[1] = (uint8_t)(value >> 8u);
  bytes[2] = (uint8_t)(value >> 16u);
  bytes[3] = (uint8_t)(value >> 24u);
}

const settings_profile_t *
settings_profile_snapshot_view(uint8_t profile_index) {
  return profile_index < SETTINGS_PROFILE_COUNT ? &settings_profile : NULL;
}

const volatile uint32_t *
settings_profile_snapshot_revision_source(uint8_t profile_index) {
  return profile_index < SETTINGS_PROFILE_COUNT ? &settings_revision : NULL;
}

const action_profile_t *action_engine_profile_view(uint8_t profile_index) {
  return profile_index < ACTION_PROFILE_COUNT ? &action_profile : NULL;
}

const volatile uint32_t *
action_engine_profile_revision_source(uint8_t profile_index) {
  return profile_index < ACTION_PROFILE_COUNT ? &action_revision : NULL;
}

bool profile_document_store_get_generation(uint8_t profile_index,
                                           uint32_t *generation) {
  if (profile_index >= SETTINGS_PROFILE_COUNT || generation == NULL) {
    return false;
  }
  *generation = document_generation;
  return true;
}

bool profile_document_store_matches(uint8_t profile_index,
                                    const settings_profile_t *settings,
                                    const action_profile_t *actions,
                                    uint32_t *generation) {
  if (profile_index >= SETTINGS_PROFILE_COUNT || document_generation == 0u ||
      settings != &settings_profile || actions != &action_profile) {
    return false;
  }
  if (generation != NULL) {
    *generation = document_generation;
  }
  return true;
}

bool profile_document_store_save(uint8_t profile_index,
                                 const settings_profile_t *settings,
                                 const action_profile_t *actions,
                                 uint32_t expected_generation,
                                 uint32_t *new_generation) {
  save_calls++;
  if (profile_index >= SETTINGS_PROFILE_COUNT || settings != &settings_profile ||
      actions != &action_profile ||
      (expected_generation != FLASH_STORAGE_GENERATION_ANY &&
       expected_generation != document_generation)) {
    return false;
  }
  document_generation++;
  if (document_generation == 0u) {
    document_generation = 1u;
  }
  if (new_generation != NULL) {
    *new_generation = document_generation;
  }
  return true;
}

bool profile_document_store_save_async_begin(
    uint8_t profile_index, const settings_profile_t *settings,
    const action_profile_t *actions, uint32_t expected_generation) {
  uint32_t next_expected = expected_generation + 1u;
  if (profile_index >= SETTINGS_PROFILE_COUNT || settings != &settings_profile ||
      actions == NULL) {
    return false;
  }
  if (next_expected == 0u) next_expected = 1u;
  if (expected_generation != FLASH_STORAGE_GENERATION_ANY &&
      expected_generation != document_generation) {
    if (document_generation == next_expected) {
      /* Model the production state machine's incremental lost-response match. */
      async_result = PROFILE_DOCUMENT_ASYNC_DONE;
      return true;
    }
    save_calls++;
    async_result = PROFILE_DOCUMENT_ASYNC_ERROR;
    return true;
  }
  save_calls++;
  document_generation++;
  if (document_generation == 0u) document_generation = 1u;
  async_result = PROFILE_DOCUMENT_ASYNC_DONE;
  return true;
}

bool profile_document_store_save_async_begin_tracked(
    uint8_t profile_index, const settings_profile_t *settings,
    const action_profile_t *actions,
    const volatile uint32_t *settings_source_revision,
    const volatile uint32_t *actions_source_revision,
    uint32_t expected_generation) {
  (void)settings_source_revision;
  (void)actions_source_revision;
  return profile_document_store_save_async_begin(
      profile_index, settings, actions, expected_generation);
}

profile_document_async_result_t
profile_document_store_async_result(uint32_t *new_generation) {
  if (async_result == PROFILE_DOCUMENT_ASYNC_DONE && new_generation != NULL) {
    *new_generation = document_generation;
  }
  return async_result;
}

void profile_document_store_async_consume(void) {
  async_result = PROFILE_DOCUMENT_ASYNC_IDLE;
}

/* Unused command dependencies retained so the real protocol unit links. */
bool action_engine_get_program(uint8_t p, uint8_t s, action_program_t *out) {
  (void)p;
  (void)s;
  if (out != NULL) memset(out, 0, sizeof(*out));
  return false;
}
bool action_engine_set_program(uint8_t p, uint8_t s,
                               const action_program_t *program, bool persist) {
  if (p >= ACTION_PROFILE_COUNT || s >= ACTION_PROGRAM_COUNT ||
      program == NULL || persist) {
    return false;
  }
  action_profile.programs[s] = *program;
  set_program_calls++;
  return true;
}
bool action_engine_publish_validated_program(
    uint8_t p, uint8_t s, const action_program_t *program) {
  if (p >= ACTION_PROFILE_COUNT || s >= ACTION_PROGRAM_COUNT ||
      program == NULL) {
    return false;
  }
  action_profile.programs[s] = *program;
  publish_program_calls++;
  action_revision++;
  return true;
}
bool action_engine_get_profile(uint8_t p, action_profile_t *out) {
  if (p >= ACTION_PROFILE_COUNT || out == NULL) return false;
  *out = action_profile;
  return true;
}
bool action_engine_set_profile(uint8_t p, const action_profile_t *profile,
                               bool persist) {
  (void)p;
  (void)persist;
  if (profile == NULL) return false;
  action_profile = *profile;
  runtime_state_bits = profile->initial_state_bits;
  set_profile_calls++;
  return true;
}
uint32_t action_engine_program_hash(const action_program_t *program) {
  (void)program; return 0u;
}
action_validation_result_t
action_engine_validate_program(const action_program_t *program) {
  (void)program;
  validate_program_calls++;
  return ACTION_VALIDATE_OK;
}
action_validation_result_t
action_engine_validate_profile(const action_profile_t *profile) {
  validate_profile_calls++;
  return profile != NULL ? ACTION_VALIDATE_OK : ACTION_VALIDATE_BAD_ARGUMENT;
}
bool action_engine_get_overlay_binding(uint8_t p, uint8_t o,
                                       action_overlay_binding_t *out) {
  (void)p; (void)o; (void)out; return false;
}
bool action_engine_set_overlay_binding(uint8_t p, uint8_t o,
                                       const action_overlay_binding_t *binding,
                                       bool persist) {
  if (p >= ACTION_PROFILE_COUNT || o >= LED_STATE_OVERLAY_COUNT ||
      binding == NULL || persist) {
    return false;
  }
  action_profile.overlays[o] = *binding;
  set_overlay_calls++;
  return true;
}
bool action_engine_publish_validated_overlay_binding(
    uint8_t p, uint8_t o, const action_overlay_binding_t *binding) {
  if (p >= ACTION_PROFILE_COUNT || o >= LED_STATE_OVERLAY_COUNT ||
      binding == NULL) {
    return false;
  }
  action_profile.overlays[o] = *binding;
  publish_overlay_calls++;
  action_revision++;
  return true;
}
uint8_t action_engine_active_profile(void) { return 0u; }
uint16_t action_engine_state_bits(void) { return runtime_state_bits; }
bool action_engine_get_state(uint8_t state) {
  return state < ACTION_STATE_COUNT &&
         (runtime_state_bits & (uint16_t)(1u << state)) != 0u;
}
bool action_engine_set_state(uint8_t state, bool value) {
  if (state >= ACTION_STATE_COUNT) return false;
  if (value) {
    runtime_state_bits |= (uint16_t)(1u << state);
    action_profile.initial_state_bits |= (uint16_t)(1u << state);
  } else {
    runtime_state_bits &= (uint16_t)~(uint16_t)(1u << state);
    action_profile.initial_state_bits &= (uint16_t)~(uint16_t)(1u << state);
  }
  action_revision++;
  return true;
}

static void reset_fixture(void) {
  memset(&settings_profile, 0, sizeof(settings_profile));
  memset(&action_profile, 0, sizeof(action_profile));
  document_generation = 0u;
  save_calls = 0u;
  async_result = PROFILE_DOCUMENT_ASYNC_IDLE;
  set_program_calls = 0u;
  publish_program_calls = 0u;
  set_profile_calls = 0u;
  set_overlay_calls = 0u;
  publish_overlay_calls = 0u;
  validate_program_calls = 0u;
  validate_profile_calls = 0u;
  runtime_state_bits = 0u;
}

static void test_deferred_program_apply_preserves_concurrent_mode_state(void) {
  uint8_t request[HID_PROTOCOL_PACKET_SIZE] = {0};
  uint8_t response[HID_PROTOCOL_PACKET_SIZE] = {0};
  action_step_t end = {.opcode = ACTION_OP_END};
  uint32_t validate_program_before_poll = 0u;
  uint32_t validate_profile_before_poll = 0u;

  reset_fixture();
  request[0] = CMD_BEGIN_SET_ACTION_PROGRAM;
  request[1] = 9u;
  request[2] = 0u;
  request[3] = 2u;
  request[4] = ACTION_PROGRAM_VERSION;
  request[5] = 0u;
  request[6] = 1u;
  assert(action_protocol_handle(request[0], request, response));
  assert(response[1] == HID_RESP_OK);

  memset(request, 0, sizeof(request));
  request[0] = CMD_SET_ACTION_PROGRAM_CHUNK;
  request[1] = 8u;
  request[2] = 0u;
  request[3] = 2u;
  request[4] = 0u;
  request[5] = 1u;
  memcpy(&request[6], &end, sizeof(end));
  assert(action_protocol_handle(request[0], request, response));
  assert(response[1] == HID_RESP_OK);

  memset(request, 0, sizeof(request));
  request[0] = CMD_COMMIT_ACTION_PROGRAM;
  request[1] = 3u;
  request[2] = 0u;
  request[3] = 2u;
  request[4] = 1u;
  assert(action_protocol_handle(request[0], request, response));
  assert(action_protocol_response_is_deferred());

  /* A physical macro can toggle a mode while Flash is completing. Applying
   * only the staged slot must not reset the live profile/state snapshot. */
  assert(action_engine_set_state(5u, true));
  validate_program_before_poll = validate_program_calls;
  validate_profile_before_poll = validate_profile_calls;
  assert(action_protocol_poll_deferred_response(response));
  assert(response[1] == HID_RESP_OK);
  assert(action_engine_get_state(5u));
  assert((action_profile.initial_state_bits & (uint16_t)(1u << 5u)) != 0u);
  assert(set_program_calls == 0u);
  assert(publish_program_calls == 1u);
  assert(validate_program_calls == validate_program_before_poll);
  assert(validate_profile_calls == validate_profile_before_poll);
  assert(set_profile_calls == 0u);
}

static void test_deferred_overlay_apply_preserves_concurrent_mode_state(void) {
  uint8_t request[HID_PROTOCOL_PACKET_SIZE] = {0};
  uint8_t response[HID_PROTOCOL_PACKET_SIZE] = {0};
  action_overlay_binding_t binding = {0};
  uint32_t validate_program_before_poll = 0u;
  uint32_t validate_profile_before_poll = 0u;

  reset_fixture();
  request[0] = CMD_SET_ACTION_OVERLAY;
  request[1] = (uint8_t)(4u + sizeof(binding));
  request[2] = 0u;
  request[3] = 1u;
  request[4] = sizeof(binding);
  request[5] = 1u;
  memcpy(&request[6], &binding, sizeof(binding));
  assert(action_protocol_handle(request[0], request, response));
  assert(action_protocol_response_is_deferred());

  assert(action_engine_set_state(6u, true));
  validate_program_before_poll = validate_program_calls;
  validate_profile_before_poll = validate_profile_calls;
  assert(action_protocol_poll_deferred_response(response));
  assert(response[1] == HID_RESP_OK);
  assert(action_engine_get_state(6u));
  assert(set_overlay_calls == 0u);
  assert(publish_overlay_calls == 1u);
  assert(validate_program_calls == validate_program_before_poll);
  assert(validate_profile_calls == validate_profile_before_poll);
  assert(set_profile_calls == 0u);
}

static void test_meta_reports_absent_and_committed_generation(void) {
  uint8_t request[HID_PROTOCOL_PACKET_SIZE] = {0};
  uint8_t response[HID_PROTOCOL_PACKET_SIZE] = {0};

  reset_fixture();
  request[0] = CMD_GET_PROFILE_DOCUMENT_META;
  request[1] = 1u;
  request[2] = 2u;
  assert(action_protocol_handle(request[0], request, response));
  assert(response[1] == HID_RESP_OK);
  assert(response[2] == 2u);
  assert(response[3] == PROFILE_DOCUMENT_SCHEMA_VERSION);
  assert(read_u32_le(&response[4]) == 0u);

  document_generation = 7u;
  assert(action_protocol_handle(request[0], request, response));
  assert(read_u32_le(&response[4]) == 7u);
}

static void test_capabilities_report_bounded_runtime_instance_pool(void) {
  uint8_t request[HID_PROTOCOL_PACKET_SIZE] = {0};
  uint8_t response[HID_PROTOCOL_PACKET_SIZE] = {0};

  request[0] = CMD_GET_ACTION_CAPABILITIES;
  assert(action_protocol_handle(request[0], request, response));
  assert(response[1] == HID_RESP_OK);
  assert(response[4] == ACTION_PROGRAM_COUNT);
  assert(response[9] == ACTION_ENGINE_MAX_INSTANCES);
  assert(ACTION_ENGINE_MAX_INSTANCES < ACTION_PROGRAM_COUNT);
}

static void test_commit_retry_is_idempotent(void) {
  uint8_t request[HID_PROTOCOL_PACKET_SIZE] = {0};
  uint8_t response[HID_PROTOCOL_PACKET_SIZE] = {0};

  reset_fixture();
  request[0] = CMD_COMMIT_PROFILE_DOCUMENT;
  request[1] = 5u;
  request[2] = 1u;
  write_u32_le(&request[3], 0u);

  assert(action_protocol_handle(request[0], request, response));
  assert(action_protocol_response_is_deferred());
  assert(action_protocol_poll_deferred_response(response));
  assert(response[1] == HID_RESP_OK);
  assert(read_u32_le(&response[4]) == 1u);
  assert(save_calls == 1u);

  memset(response, 0, sizeof(response));
  assert(action_protocol_handle(request[0], request, response));
  assert(action_protocol_response_is_deferred());
  assert(action_protocol_poll_deferred_response(response));
  assert(response[1] == HID_RESP_OK);
  assert(read_u32_le(&response[4]) == 1u);
  assert(save_calls == 1u); /* lost-response retry did not consume flash */

  write_u32_le(&request[3], 9u);
  assert(action_protocol_handle(request[0], request, response));
  assert(action_protocol_response_is_deferred());
  assert(action_protocol_poll_deferred_response(response));
  assert(response[1] == HID_RESP_ERROR);
  assert(save_calls == 2u);
}

static void test_rejects_declared_payload_beyond_report(void) {
  uint8_t request[HID_PROTOCOL_PACKET_SIZE] = {0};
  uint8_t response[HID_PROTOCOL_PACKET_SIZE] = {0};

  request[0] = CMD_GET_PROFILE_DOCUMENT_META;
  request[1] = 0xFFu;
  request[2] = 0u;
  assert(action_protocol_handle(request[0], request, response));
  assert(response[1] == HID_RESP_INVALID_PARAM);
}

int main(void) {
  test_capabilities_report_bounded_runtime_instance_pool();
  test_meta_reports_absent_and_committed_generation();
  test_commit_retry_is_idempotent();
  test_deferred_program_apply_preserves_concurrent_mode_state();
  test_deferred_overlay_apply_preserves_concurrent_mode_state();
  test_rejects_declared_payload_beyond_report();
  puts("action_protocol_test: ok");
  return 0;
}
