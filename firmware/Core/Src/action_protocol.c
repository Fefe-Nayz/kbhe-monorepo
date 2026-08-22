#include "action_protocol.h"

#include "action_engine.h"
#include "flash_storage.h"
#include "hid_protocol.h"
#include "profile_document_store.h"
#include "settings.h"

#include <string.h>

#define ACTION_STEPS_PER_PACKET 14u

typedef struct {
  bool active;
  uint8_t profile_index;
  uint8_t program_index;
  uint32_t expected_hash;
  uint32_t received_mask;
  action_program_t program;
} action_program_staging_t;

static action_program_staging_t staging;

typedef struct {
  uint8_t command_id;
  bool active;
  uint8_t profile_index;
  uint8_t item_index;
  uint32_t program_hash;
  action_profile_t action_profile;
} action_deferred_response_t;

static action_deferred_response_t deferred_response;

static void response_init(uint8_t *output, uint8_t command_id) {
  memset(output, 0, HID_PROTOCOL_PACKET_SIZE);
  output[0] = command_id;
  output[1] = HID_RESP_ERROR;
}

static bool request_has(const uint8_t *input, uint8_t data_bytes) {
  const uint8_t max_data_bytes = HID_PROTOCOL_PACKET_SIZE - 2u;
  return input != NULL && data_bytes <= max_data_bytes &&
         input[1] <= max_data_bytes && input[1] >= data_bytes;
}

static uint32_t read_u32_le(const uint8_t *bytes) {
  return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8u) |
         ((uint32_t)bytes[2] << 16u) | ((uint32_t)bytes[3] << 24u);
}

static void write_u16_le(uint8_t *bytes, uint16_t value) {
  bytes[0] = (uint8_t)(value & 0xFFu);
  bytes[1] = (uint8_t)(value >> 8u);
}

static void write_u32_le(uint8_t *bytes, uint32_t value) {
  bytes[0] = (uint8_t)(value & 0xFFu);
  bytes[1] = (uint8_t)((value >> 8u) & 0xFFu);
  bytes[2] = (uint8_t)((value >> 16u) & 0xFFu);
  bytes[3] = (uint8_t)((value >> 24u) & 0xFFu);
}

static bool command_get_capabilities(uint8_t *output) {
  output[1] = HID_RESP_OK;
  output[2] = ACTION_PROGRAM_VERSION;
  output[3] = ACTION_PROFILE_COUNT;
  output[4] = ACTION_PROGRAM_COUNT;
  output[5] = ACTION_PROGRAM_MAX_STEPS;
  output[6] = ACTION_STATE_COUNT;
  output[7] = LED_STATE_OVERLAY_COUNT;
  output[8] = sizeof(action_step_t);
  output[9] = ACTION_ENGINE_MAX_INSTANCES;
  output[10] = PROFILE_DOCUMENT_SCHEMA_VERSION;
  output[11] = 1u; /* Atomic ProfileDocument commit supported. */
  return true;
}

static bool command_get_program_meta(const uint8_t *input, uint8_t *output) {
  action_program_t program = {0};
  uint8_t profile = 0u;
  uint8_t slot = 0u;

  if (!request_has(input, 2u)) {
    output[1] = HID_RESP_INVALID_PARAM;
    return true;
  }
  profile = input[2];
  slot = input[3];
  if (!action_engine_get_program(profile, slot, &program)) {
    output[1] = HID_RESP_INVALID_PARAM;
    return true;
  }
  output[1] = HID_RESP_OK;
  output[2] = profile;
  output[3] = slot;
  output[4] = program.version;
  output[5] = program.flags;
  output[6] = program.step_count;
  output[7] = (uint8_t)action_engine_validate_program(&program);
  write_u32_le(&output[8], action_engine_program_hash(&program));
  return true;
}

static bool command_get_program_chunk(const uint8_t *input, uint8_t *output) {
  action_program_t program = {0};
  uint8_t profile = 0u;
  uint8_t slot = 0u;
  uint8_t start = 0u;
  uint8_t count = 0u;

  if (!request_has(input, 4u)) {
    output[1] = HID_RESP_INVALID_PARAM;
    return true;
  }
  profile = input[2];
  slot = input[3];
  start = input[4];
  count = input[5];
  if (!action_engine_get_program(profile, slot, &program) ||
      start >= program.step_count || count == 0u) {
    output[1] = HID_RESP_INVALID_PARAM;
    return true;
  }
  if (count > ACTION_STEPS_PER_PACKET) {
    count = ACTION_STEPS_PER_PACKET;
  }
  if ((uint16_t)start + count > program.step_count) {
    count = (uint8_t)(program.step_count - start);
  }
  output[1] = HID_RESP_OK;
  output[2] = profile;
  output[3] = slot;
  output[4] = start;
  output[5] = count;
  memcpy(&output[6], &program.steps[start], (size_t)count * sizeof(action_step_t));
  return true;
}

static bool command_begin_program(const uint8_t *input, uint8_t *output) {
  uint8_t profile = 0u;
  uint8_t slot = 0u;
  uint8_t version = 0u;
  uint8_t flags = 0u;
  uint8_t step_count = 0u;

  if (!request_has(input, 9u)) {
    output[1] = HID_RESP_INVALID_PARAM;
    return true;
  }
  profile = input[2];
  slot = input[3];
  version = input[4];
  flags = input[5];
  step_count = input[6];
  if (profile >= ACTION_PROFILE_COUNT || slot >= ACTION_PROGRAM_COUNT ||
      version != ACTION_PROGRAM_VERSION || step_count == 0u ||
      step_count > ACTION_PROGRAM_MAX_STEPS) {
    output[1] = HID_RESP_INVALID_PARAM;
    return true;
  }

  memset(&staging, 0, sizeof(staging));
  staging.active = true;
  staging.profile_index = profile;
  staging.program_index = slot;
  staging.expected_hash = read_u32_le(&input[7]);
  staging.program.version = version;
  staging.program.flags = flags;
  staging.program.step_count = step_count;
  output[1] = HID_RESP_OK;
  output[2] = profile;
  output[3] = slot;
  return true;
}

static bool command_set_program_chunk(const uint8_t *input, uint8_t *output) {
  uint8_t profile = 0u;
  uint8_t slot = 0u;
  uint8_t start = 0u;
  uint8_t count = 0u;
  uint8_t required_bytes = 0u;

  if (!request_has(input, 4u)) {
    output[1] = HID_RESP_INVALID_PARAM;
    return true;
  }
  profile = input[2];
  slot = input[3];
  start = input[4];
  count = input[5];
  required_bytes = (uint8_t)(4u + count * sizeof(action_step_t));
  if (!staging.active || profile != staging.profile_index ||
      slot != staging.program_index || count == 0u ||
      count > ACTION_STEPS_PER_PACKET || !request_has(input, required_bytes) ||
      (uint16_t)start + count > staging.program.step_count) {
    output[1] = HID_RESP_INVALID_PARAM;
    return true;
  }

  memcpy(&staging.program.steps[start], &input[6],
         (size_t)count * sizeof(action_step_t));
  for (uint8_t i = 0u; i < count; i++) {
    staging.received_mask |= (uint32_t)1u << (start + i);
  }
  output[1] = HID_RESP_OK;
  output[2] = profile;
  output[3] = slot;
  output[4] = start;
  output[5] = count;
  return true;
}

static bool command_commit_program(const uint8_t *input, uint8_t *output) {
  uint32_t required_mask = 0u;
  bool persist = true;
  uint32_t hash = 0u;

  if (!request_has(input, 3u) || !staging.active ||
      input[2] != staging.profile_index || input[3] != staging.program_index) {
    output[1] = HID_RESP_INVALID_PARAM;
    return true;
  }
  persist = input[4] != 0u;
  required_mask = staging.program.step_count == 32u
                      ? UINT32_MAX
                      : (((uint32_t)1u << staging.program.step_count) - 1u);
  hash = action_engine_program_hash(&staging.program);
  if ((staging.received_mask & required_mask) != required_mask ||
      action_engine_validate_program(&staging.program) != ACTION_VALIDATE_OK ||
      (staging.expected_hash != 0u && hash != staging.expected_hash)) {
    output[1] = HID_RESP_INVALID_PARAM;
    return true;
  }

  if (persist) {
    const settings_profile_t *settings_profile =
        settings_profile_snapshot_view(staging.profile_index);
    if (settings_profile == NULL ||
        !action_engine_get_profile(staging.profile_index,
                                   &deferred_response.action_profile)) {
      output[1] = HID_RESP_ERROR;
      return true;
    }
    memcpy(&deferred_response.action_profile.programs[staging.program_index],
           &staging.program, sizeof(staging.program));
    if (action_engine_validate_profile(&deferred_response.action_profile) !=
        ACTION_VALIDATE_OK) {
      output[1] = HID_RESP_INVALID_PARAM;
      return true;
    }
    if (!profile_document_store_save_async_begin_tracked(
            staging.profile_index, settings_profile,
            &deferred_response.action_profile,
            settings_profile_snapshot_revision_source(staging.profile_index),
            NULL,
            FLASH_STORAGE_GENERATION_ANY)) {
      output[1] = HID_RESP_ERROR;
      return true;
    }
    deferred_response.active = true;
    deferred_response.command_id = CMD_COMMIT_ACTION_PROGRAM;
    deferred_response.profile_index = staging.profile_index;
    deferred_response.item_index = staging.program_index;
    deferred_response.program_hash = hash;
    return true;
  }

  if (!action_engine_set_program(staging.profile_index, staging.program_index,
                                 &staging.program, false)) {
    output[1] = HID_RESP_ERROR;
    return true;
  }
  output[1] = HID_RESP_OK;
  output[2] = staging.profile_index;
  output[3] = staging.program_index;
  write_u32_le(&output[4], hash);
  memset(&staging, 0, sizeof(staging));
  return true;
}

static bool command_abort_program(uint8_t *output) {
  memset(&staging, 0, sizeof(staging));
  output[1] = HID_RESP_OK;
  return true;
}

static bool command_get_overlay(const uint8_t *input, uint8_t *output) {
  action_overlay_binding_t binding = {0};
  if (!request_has(input, 2u) ||
      !action_engine_get_overlay_binding(input[2], input[3], &binding)) {
    output[1] = HID_RESP_INVALID_PARAM;
    return true;
  }
  output[1] = HID_RESP_OK;
  output[2] = input[2];
  output[3] = input[3];
  output[4] = sizeof(binding);
  memcpy(&output[5], &binding, sizeof(binding));
  return true;
}

static bool command_set_overlay(const uint8_t *input, uint8_t *output) {
  action_overlay_binding_t binding = {0};
  uint8_t payload_size = sizeof(binding);
  if (!request_has(input, (uint8_t)(4u + payload_size)) ||
      input[4] != payload_size) {
    output[1] = HID_RESP_INVALID_PARAM;
    return true;
  }
  memcpy(&binding, &input[6], sizeof(binding));
  if (input[5] != 0u) {
    const settings_profile_t *settings_profile =
        settings_profile_snapshot_view(input[2]);
    binding.follows_state = binding.follows_state ? 1u : 0u;
    binding.active_value = binding.active_value ? 1u : 0u;
    if (input[2] >= ACTION_PROFILE_COUNT ||
        input[3] >= LED_STATE_OVERLAY_COUNT || settings_profile == NULL ||
        binding.state_index >= ACTION_STATE_COUNT ||
        binding.config.blend_mode >= (uint8_t)LED_OVERLAY_BLEND_MAX ||
        !action_engine_get_profile(input[2],
                                   &deferred_response.action_profile)) {
      output[1] = HID_RESP_INVALID_PARAM;
      return true;
    }
    deferred_response.action_profile.overlays[input[3]] = binding;
    if (!profile_document_store_save_async_begin_tracked(
            input[2], settings_profile, &deferred_response.action_profile,
            settings_profile_snapshot_revision_source(input[2]), NULL,
            FLASH_STORAGE_GENERATION_ANY)) {
      output[1] = HID_RESP_ERROR;
      return true;
    }
    deferred_response.active = true;
    deferred_response.command_id = CMD_SET_ACTION_OVERLAY;
    deferred_response.profile_index = input[2];
    deferred_response.item_index = input[3];
    return true;
  }

  if (!action_engine_set_overlay_binding(input[2], input[3], &binding,
                                         false)) {
    output[1] = HID_RESP_INVALID_PARAM;
    return true;
  }
  output[1] = HID_RESP_OK;
  output[2] = input[2];
  output[3] = input[3];
  return true;
}

static bool command_get_states(uint8_t *output) {
  output[1] = HID_RESP_OK;
  write_u16_le(&output[2], action_engine_state_bits());
  output[4] = action_engine_active_profile();
  return true;
}

static bool command_set_state(const uint8_t *input, uint8_t *output) {
  if (!request_has(input, 2u) ||
      !action_engine_set_state(input[2], input[3] != 0u)) {
    output[1] = HID_RESP_INVALID_PARAM;
    return true;
  }
  output[1] = HID_RESP_OK;
  output[2] = input[2];
  output[3] = action_engine_get_state(input[2]) ? 1u : 0u;
  return true;
}

static bool command_commit_profile_document(const uint8_t *input,
                                            uint8_t *output) {
  uint8_t profile_index = 0u;
  uint32_t expected_generation = 0u;
  const settings_profile_t *settings_profile = NULL;
  const action_profile_t *action_profile = NULL;

  if (!request_has(input, 5u)) {
    output[1] = HID_RESP_INVALID_PARAM;
    return true;
  }
  profile_index = input[2];
  expected_generation = read_u32_le(&input[3]);
  settings_profile = settings_profile_snapshot_view(profile_index);
  action_profile = action_engine_profile_view(profile_index);
  if (settings_profile == NULL || action_profile == NULL) {
    output[1] = HID_RESP_INVALID_PARAM;
    return true;
  }

  if (!profile_document_store_save_async_begin_tracked(
          profile_index, settings_profile, action_profile,
          settings_profile_snapshot_revision_source(profile_index),
          action_engine_profile_revision_source(profile_index),
          expected_generation)) {
    output[1] = HID_RESP_ERROR;
    return true;
  }
  deferred_response.active = true;
  deferred_response.command_id = CMD_COMMIT_PROFILE_DOCUMENT;
  deferred_response.profile_index = profile_index;
  return true;
}

static bool command_get_profile_document_meta(const uint8_t *input,
                                              uint8_t *output) {
  uint8_t profile_index = 0u;
  uint32_t generation = 0u;

  if (!request_has(input, 1u)) {
    output[1] = HID_RESP_INVALID_PARAM;
    return true;
  }
  profile_index = input[2];
  if (profile_index >= SETTINGS_PROFILE_COUNT ||
      !profile_document_store_get_generation(profile_index, &generation)) {
    output[1] = HID_RESP_INVALID_PARAM;
    return true;
  }
  output[1] = HID_RESP_OK;
  output[2] = profile_index;
  output[3] = PROFILE_DOCUMENT_SCHEMA_VERSION;
  write_u32_le(&output[4], generation);
  return true;
}

bool action_protocol_handle(uint8_t command_id, const uint8_t *input,
                            uint8_t *output) {
  if (input == NULL || output == NULL || command_id < 0x90u ||
      command_id > CMD_GET_PROFILE_DOCUMENT_META) {
    return false;
  }
  response_init(output, command_id);
  switch (command_id) {
  case CMD_GET_ACTION_CAPABILITIES:
    return command_get_capabilities(output);
  case CMD_GET_ACTION_PROGRAM_META:
    return command_get_program_meta(input, output);
  case CMD_GET_ACTION_PROGRAM_CHUNK:
    return command_get_program_chunk(input, output);
  case CMD_BEGIN_SET_ACTION_PROGRAM:
    return command_begin_program(input, output);
  case CMD_SET_ACTION_PROGRAM_CHUNK:
    return command_set_program_chunk(input, output);
  case CMD_COMMIT_ACTION_PROGRAM:
    return command_commit_program(input, output);
  case CMD_ABORT_ACTION_PROGRAM:
    return command_abort_program(output);
  case CMD_GET_ACTION_OVERLAY:
    return command_get_overlay(input, output);
  case CMD_SET_ACTION_OVERLAY:
    return command_set_overlay(input, output);
  case CMD_GET_ACTION_STATES:
    return command_get_states(output);
  case CMD_SET_ACTION_STATE:
    return command_set_state(input, output);
  case CMD_COMMIT_PROFILE_DOCUMENT:
    return command_commit_profile_document(input, output);
  case CMD_GET_PROFILE_DOCUMENT_META:
    return command_get_profile_document_meta(input, output);
  default:
    return false;
  }
}

bool action_protocol_response_is_deferred(void) {
  return deferred_response.active;
}

bool action_protocol_poll_deferred_response(uint8_t *output) {
  uint32_t generation = 0u;
  bool applied = true;
  profile_document_async_result_t result =
      profile_document_store_async_result(&generation);

  if (!deferred_response.active || output == NULL ||
      result == PROFILE_DOCUMENT_ASYNC_IDLE ||
      result == PROFILE_DOCUMENT_ASYNC_IN_PROGRESS) {
    return false;
  }

  if (result == PROFILE_DOCUMENT_ASYNC_DONE &&
      deferred_response.command_id != CMD_COMMIT_PROFILE_DOCUMENT) {
    if (deferred_response.command_id == CMD_COMMIT_ACTION_PROGRAM) {
      applied = action_engine_publish_validated_program(
          deferred_response.profile_index, deferred_response.item_index,
          &deferred_response.action_profile
               .programs[deferred_response.item_index]);
    } else if (deferred_response.command_id == CMD_SET_ACTION_OVERLAY) {
      applied = action_engine_publish_validated_overlay_binding(
          deferred_response.profile_index, deferred_response.item_index,
          &deferred_response.action_profile
               .overlays[deferred_response.item_index]);
    }
  }

  response_init(output, deferred_response.command_id);
  output[1] = result == PROFILE_DOCUMENT_ASYNC_DONE && applied
                  ? HID_RESP_OK
                  : HID_RESP_ERROR;
  output[2] = deferred_response.profile_index;
  if (deferred_response.command_id == CMD_COMMIT_PROFILE_DOCUMENT) {
    output[3] = PROFILE_DOCUMENT_SCHEMA_VERSION;
    if (result == PROFILE_DOCUMENT_ASYNC_DONE) {
      write_u32_le(&output[4], generation);
    }
  } else if (deferred_response.command_id == CMD_COMMIT_ACTION_PROGRAM) {
    output[3] = deferred_response.item_index;
    write_u32_le(&output[4], deferred_response.program_hash);
    memset(&staging, 0, sizeof(staging));
  } else if (deferred_response.command_id == CMD_SET_ACTION_OVERLAY) {
    output[3] = deferred_response.item_index;
  }
  profile_document_store_async_consume();
  memset(&deferred_response, 0, sizeof(deferred_response));
  return true;
}
