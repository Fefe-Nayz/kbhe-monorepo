#include "profile_document_store.h"

#include "flash_storage.h"

#include <stddef.h>
#include <string.h>

#define PROFILE_DOCUMENT_MAGIC 0x50464432u /* "PFD2" */
#define PROFILE_DOCUMENT_RAW_SCHEMA_VERSION 2u
#define PROFILE_DOCUMENT_HASH_BITS 12u
#define PROFILE_DOCUMENT_HASH_SIZE (1u << PROFILE_DOCUMENT_HASH_BITS)
#define PROFILE_DOCUMENT_LITERAL_MAX 128u
#define PROFILE_DOCUMENT_MATCH_MIN 4u
#define PROFILE_DOCUMENT_MATCH_MAX 131u

typedef struct __attribute__((packed)) {
  uint32_t magic;
  uint16_t schema_version;
  uint16_t header_size;
  uint32_t settings_size;
  uint32_t actions_size;
} profile_document_header_t;

typedef struct __attribute__((packed)) {
  settings_profile_t settings;
  action_profile_t actions;
} profile_document_raw_t;

#define PROFILE_DOCUMENT_RAW_SIZE ((uint32_t)sizeof(profile_document_raw_t))
#define PROFILE_DOCUMENT_ENCODED_MAX                                           \
  (PROFILE_DOCUMENT_RAW_SIZE +                                                \
   ((PROFILE_DOCUMENT_RAW_SIZE + PROFILE_DOCUMENT_LITERAL_MAX - 1u) /         \
    PROFILE_DOCUMENT_LITERAL_MAX))

_Static_assert(offsetof(profile_document_raw_t, actions) ==
                   sizeof(settings_profile_t),
               "profile raw components must be contiguous");

typedef enum {
  PROFILE_DOCUMENT_PHASE_IDLE = 0,
  PROFILE_DOCUMENT_PHASE_COPY_SETTINGS,
  PROFILE_DOCUMENT_PHASE_COPY_ACTIONS,
  PROFILE_DOCUMENT_PHASE_VALIDATE_PROGRAMS,
  PROFILE_DOCUMENT_PHASE_VALIDATE_MACRO_GRAPH,
  PROFILE_DOCUMENT_PHASE_VALIDATE_MACRO_DEPTH,
  PROFILE_DOCUMENT_PHASE_VALIDATE_OVERLAYS,
  PROFILE_DOCUMENT_PHASE_INIT_COMPRESS,
  PROFILE_DOCUMENT_PHASE_COMPRESS,
  PROFILE_DOCUMENT_PHASE_MATCH_HEADER,
  PROFILE_DOCUMENT_PHASE_MATCH_PAYLOAD,
  PROFILE_DOCUMENT_PHASE_WAIT_FLASH,
  PROFILE_DOCUMENT_PHASE_FLASH,
  PROFILE_DOCUMENT_PHASE_DONE,
  PROFILE_DOCUMENT_PHASE_ERROR,
} profile_document_async_phase_t;

typedef struct {
  profile_document_async_phase_t phase;
  uint8_t profile_index;
  uint8_t validate_index;
  uint32_t copy_offset;
  uint32_t expected_generation;
  uint32_t new_generation;
  const settings_profile_t *settings_source;
  const action_profile_t *actions_source;
  const volatile uint32_t *settings_source_revision;
  const volatile uint32_t *actions_source_revision;
  uint32_t settings_revision_at_copy_start;
  uint32_t actions_revision_at_copy_start;
  uint8_t source_restart_count;
  uint16_t macro_reach[ACTION_PROGRAM_COUNT];
  uint16_t macro_depth_pending;
  uint8_t macro_depths[ACTION_PROGRAM_COUNT];
  profile_document_header_t header;
  profile_document_raw_t raw_snapshot;
  uint8_t encoded[PROFILE_DOCUMENT_ENCODED_MAX];
  uint16_t hash_positions[PROFILE_DOCUMENT_HASH_SIZE];
  uint32_t compress_position;
  uint32_t literal_anchor;
  uint32_t encoded_length;
  uint32_t match_offset;
  uint32_t stored_payload_length;
  uint32_t stored_generation;
  uint16_t stored_schema_version;
} profile_document_async_state_t;

static profile_document_async_state_t async_state;

static uint32_t profile_document_hash4(const uint8_t *bytes) {
  uint32_t value = 0u;
  memcpy(&value, bytes, sizeof(value));
  return (value * 2654435761u) >> (32u - PROFILE_DOCUMENT_HASH_BITS);
}

static bool profile_document_emit_literals(const uint8_t *raw,
                                            uint32_t offset,
                                            uint32_t length) {
  if (length == 0u || length > PROFILE_DOCUMENT_LITERAL_MAX ||
      async_state.encoded_length > PROFILE_DOCUMENT_ENCODED_MAX - 1u - length) {
    return false;
  }
  async_state.encoded[async_state.encoded_length++] = (uint8_t)(length - 1u);
  memcpy(&async_state.encoded[async_state.encoded_length], raw + offset,
         length);
  async_state.encoded_length += length;
  return true;
}

static bool profile_document_emit_match(uint32_t offset, uint32_t length) {
  if (offset == 0u || offset > UINT16_MAX ||
      length < PROFILE_DOCUMENT_MATCH_MIN ||
      length > PROFILE_DOCUMENT_MATCH_MAX ||
      async_state.encoded_length > PROFILE_DOCUMENT_ENCODED_MAX - 3u) {
    return false;
  }
  async_state.encoded[async_state.encoded_length++] =
      (uint8_t)(0x80u | (uint8_t)(length - PROFILE_DOCUMENT_MATCH_MIN));
  async_state.encoded[async_state.encoded_length++] = (uint8_t)offset;
  async_state.encoded[async_state.encoded_length++] = (uint8_t)(offset >> 8);
  return true;
}

static bool profile_document_compress_step(uint32_t byte_budget,
                                           bool *done_out) {
  const uint8_t *raw = (const uint8_t *)&async_state.raw_snapshot;
  uint32_t work = 0u;

  if (done_out == NULL) {
    return false;
  }
  *done_out = false;
  if (byte_budget == 0u) {
    byte_budget = 1u;
  }

  while (work < byte_budget &&
         async_state.compress_position + PROFILE_DOCUMENT_MATCH_MIN <=
             PROFILE_DOCUMENT_RAW_SIZE) {
    uint32_t position = async_state.compress_position;
    uint32_t hash = profile_document_hash4(raw + position);
    uint16_t previous = async_state.hash_positions[hash];
    uint32_t match_length = 0u;

    if (position - async_state.literal_anchor >=
        PROFILE_DOCUMENT_LITERAL_MAX) {
      if (!profile_document_emit_literals(
              raw, async_state.literal_anchor,
              PROFILE_DOCUMENT_LITERAL_MAX)) {
        return false;
      }
      async_state.literal_anchor += PROFILE_DOCUMENT_LITERAL_MAX;
    }

    async_state.hash_positions[hash] = (uint16_t)position;
    if (previous != UINT16_MAX && (uint32_t)previous < position &&
        position - (uint32_t)previous <= UINT16_MAX &&
        memcmp(raw + previous, raw + position, PROFILE_DOCUMENT_MATCH_MIN) ==
            0) {
      uint32_t max_length = PROFILE_DOCUMENT_RAW_SIZE - position;
      if (max_length > PROFILE_DOCUMENT_MATCH_MAX) {
        max_length = PROFILE_DOCUMENT_MATCH_MAX;
      }
      match_length = PROFILE_DOCUMENT_MATCH_MIN;
      while (match_length < max_length &&
             raw[(uint32_t)previous + match_length] ==
                 raw[position + match_length]) {
        match_length++;
      }
    }

    if (match_length < PROFILE_DOCUMENT_MATCH_MIN) {
      async_state.compress_position++;
      work++;
      continue;
    }

    if (position != async_state.literal_anchor &&
        !profile_document_emit_literals(
            raw, async_state.literal_anchor,
            position - async_state.literal_anchor)) {
      return false;
    }
    if (!profile_document_emit_match(position - (uint32_t)previous,
                                     match_length)) {
      return false;
    }
    for (uint32_t skipped = 1u; skipped < match_length; skipped++) {
      uint32_t skipped_position = position + skipped;
      if (skipped_position + PROFILE_DOCUMENT_MATCH_MIN >
          PROFILE_DOCUMENT_RAW_SIZE) {
        break;
      }
      async_state.hash_positions[profile_document_hash4(
          raw + skipped_position)] = (uint16_t)skipped_position;
    }
    async_state.compress_position += match_length;
    async_state.literal_anchor = async_state.compress_position;
    work += match_length;
  }

  if (async_state.compress_position + PROFILE_DOCUMENT_MATCH_MIN >
      PROFILE_DOCUMENT_RAW_SIZE) {
    async_state.compress_position = PROFILE_DOCUMENT_RAW_SIZE;
    if (async_state.literal_anchor < PROFILE_DOCUMENT_RAW_SIZE) {
      uint32_t remaining =
          PROFILE_DOCUMENT_RAW_SIZE - async_state.literal_anchor;
      uint32_t chunk = remaining > PROFILE_DOCUMENT_LITERAL_MAX
                           ? PROFILE_DOCUMENT_LITERAL_MAX
                           : remaining;
      if (!profile_document_emit_literals(raw, async_state.literal_anchor,
                                          chunk)) {
        return false;
      }
      async_state.literal_anchor += chunk;
    }
    *done_out = async_state.literal_anchor == PROFILE_DOCUMENT_RAW_SIZE;
  }
  return true;
}

static bool profile_document_actions_are_valid(
    const action_profile_t *actions) {
  return action_engine_validate_profile(actions) == ACTION_VALIDATE_OK;
}

static bool profile_document_read_header(uint8_t profile_index,
                                         profile_document_header_t *header,
                                         uint32_t *actual_len,
                                         uint16_t *schema_version,
                                         uint32_t *generation) {
  uint32_t length = 0u;
  uint16_t schema = 0u;
  uint32_t loaded_generation = 0u;

  if (profile_index >= SETTINGS_PROFILE_COUNT || header == NULL ||
      flash_storage_object_read_range(
          FLASH_STORAGE_NAMESPACE_PROFILE_DOCUMENT, profile_index, 0u, header,
          sizeof(*header), &length, &schema, &loaded_generation) !=
          FLASH_STORAGE_STATUS_OK ||
      header->magic != PROFILE_DOCUMENT_MAGIC ||
      header->schema_version != schema || header->header_size != sizeof(*header) ||
      header->settings_size != sizeof(settings_profile_t) ||
      header->actions_size != sizeof(action_profile_t) ||
      ((schema == PROFILE_DOCUMENT_RAW_SCHEMA_VERSION &&
        length != sizeof(*header) + PROFILE_DOCUMENT_RAW_SIZE) ||
       (schema == PROFILE_DOCUMENT_SCHEMA_VERSION &&
        (length <= sizeof(*header) ||
         length > sizeof(*header) + PROFILE_DOCUMENT_ENCODED_MAX)) ||
       (schema != PROFILE_DOCUMENT_RAW_SCHEMA_VERSION &&
        schema != PROFILE_DOCUMENT_SCHEMA_VERSION))) {
    return false;
  }
  if (actual_len != NULL) {
    *actual_len = length;
  }
  if (schema_version != NULL) {
    *schema_version = schema;
  }
  if (generation != NULL) {
    *generation = loaded_generation;
  }
  return true;
}

static bool profile_document_decode(uint8_t profile_index,
                                    const profile_document_header_t *header,
                                    uint32_t actual_len,
                                    uint16_t schema_version) {
  uint8_t *raw = (uint8_t *)&async_state.raw_snapshot;
  uint32_t input_offset = sizeof(*header);
  uint32_t output_offset = 0u;

  if (async_state.phase != PROFILE_DOCUMENT_PHASE_IDLE) {
    return false;
  }
  if (schema_version == PROFILE_DOCUMENT_RAW_SCHEMA_VERSION) {
    return flash_storage_object_read_range(
               FLASH_STORAGE_NAMESPACE_PROFILE_DOCUMENT, profile_index,
               input_offset, raw, PROFILE_DOCUMENT_RAW_SIZE, NULL, NULL,
               NULL) == FLASH_STORAGE_STATUS_OK;
  }

  while (input_offset < actual_len && output_offset < PROFILE_DOCUMENT_RAW_SIZE) {
    uint8_t token = 0u;
    if (flash_storage_object_read_range(
            FLASH_STORAGE_NAMESPACE_PROFILE_DOCUMENT, profile_index,
            input_offset++, &token, sizeof(token), NULL, NULL, NULL) !=
        FLASH_STORAGE_STATUS_OK) {
      return false;
    }
    if ((token & 0x80u) == 0u) {
      uint32_t length = (uint32_t)token + 1u;
      if (length > actual_len - input_offset ||
          length > PROFILE_DOCUMENT_RAW_SIZE - output_offset ||
          flash_storage_object_read_range(
              FLASH_STORAGE_NAMESPACE_PROFILE_DOCUMENT, profile_index,
              input_offset, raw + output_offset, length, NULL, NULL, NULL) !=
              FLASH_STORAGE_STATUS_OK) {
        return false;
      }
      input_offset += length;
      output_offset += length;
    } else {
      uint8_t encoded_offset[2];
      uint32_t length =
          (uint32_t)(token & 0x7Fu) + PROFILE_DOCUMENT_MATCH_MIN;
      uint32_t offset = 0u;
      if (actual_len - input_offset < sizeof(encoded_offset) ||
          flash_storage_object_read_range(
              FLASH_STORAGE_NAMESPACE_PROFILE_DOCUMENT, profile_index,
              input_offset, encoded_offset, sizeof(encoded_offset), NULL,
              NULL, NULL) != FLASH_STORAGE_STATUS_OK) {
        return false;
      }
      input_offset += sizeof(encoded_offset);
      offset = (uint32_t)encoded_offset[0] |
               ((uint32_t)encoded_offset[1] << 8);
      if (offset == 0u || offset > output_offset ||
          length > PROFILE_DOCUMENT_RAW_SIZE - output_offset) {
        return false;
      }
      for (uint32_t i = 0u; i < length; i++) {
        raw[output_offset] = raw[output_offset - offset];
        output_offset++;
      }
    }
  }
  return input_offset == actual_len && output_offset == PROFILE_DOCUMENT_RAW_SIZE;
}

bool profile_document_store_get_generation(uint8_t profile_index,
                                           uint32_t *generation) {
  profile_document_header_t header;
  flash_storage_status_t status = FLASH_STORAGE_STATUS_NOT_FOUND;

  if (profile_index >= SETTINGS_PROFILE_COUNT || generation == NULL) {
    return false;
  }
  status = flash_storage_object_read_range(
      FLASH_STORAGE_NAMESPACE_PROFILE_DOCUMENT, profile_index, 0u, &header,
      sizeof(header), NULL, NULL, NULL);
  if (status == FLASH_STORAGE_STATUS_NOT_FOUND) {
    *generation = 0u;
    return true;
  }
  if (status != FLASH_STORAGE_STATUS_OK ||
      !profile_document_read_header(profile_index, &header, NULL, NULL,
                                    generation)) {
    return false;
  }
  return true;
}

bool profile_document_store_matches(uint8_t profile_index,
                                    const settings_profile_t *settings,
                                    const action_profile_t *actions,
                                    uint32_t *generation) {
  uint32_t loaded_generation = 0u;
  profile_document_header_t header;
  uint32_t actual_len = 0u;
  uint16_t schema_version = 0u;

  if (settings == NULL || actions == NULL ||
      !profile_document_read_header(profile_index, &header, &actual_len,
                                    &schema_version, &loaded_generation) ||
      !profile_document_decode(profile_index, &header, actual_len,
                               schema_version) ||
      memcmp(&async_state.raw_snapshot.settings, settings,
             sizeof(*settings)) != 0 ||
      memcmp(&async_state.raw_snapshot.actions, actions, sizeof(*actions)) !=
          0) {
    return false;
  }
  if (generation != NULL) {
    *generation = loaded_generation;
  }
  return true;
}

bool profile_document_store_load(uint8_t profile_index,
                                 settings_profile_t *settings_out,
                                 action_profile_t *actions_out,
                                 uint32_t *generation) {
  profile_document_header_t header;
  uint32_t actual_len = 0u;
  uint32_t loaded_generation = 0u;
  uint16_t schema_version = 0u;

  if (profile_index >= SETTINGS_PROFILE_COUNT ||
      (settings_out == NULL && actions_out == NULL)) {
    return false;
  }
  if (!profile_document_read_header(profile_index, &header, &actual_len,
                                    &schema_version, &loaded_generation) ||
      !profile_document_decode(profile_index, &header, actual_len,
                               schema_version)) {
    return false;
  }

  /* The two components are one canonical transaction: reject the complete
   * document before publishing either half when its action graph is invalid. */
  if (!profile_document_actions_are_valid(&async_state.raw_snapshot.actions)) {
    return false;
  }

  if (settings_out != NULL) {
    memcpy(settings_out, &async_state.raw_snapshot.settings,
           sizeof(*settings_out));
  }
  if (actions_out != NULL) {
    memcpy(actions_out, &async_state.raw_snapshot.actions,
           sizeof(*actions_out));
  }
  if (generation != NULL) {
    *generation = loaded_generation;
  }
  return true;
}

bool profile_document_store_save(uint8_t profile_index,
                                 const settings_profile_t *settings,
                                 const action_profile_t *actions,
                                 uint32_t expected_generation,
                                 uint32_t *new_generation) {
  profile_document_header_t header = {
      .magic = PROFILE_DOCUMENT_MAGIC,
      .schema_version = PROFILE_DOCUMENT_SCHEMA_VERSION,
      .header_size = sizeof(profile_document_header_t),
      .settings_size = sizeof(settings_profile_t),
      .actions_size = sizeof(action_profile_t),
  };
  flash_storage_segment_t segments[2];
  bool compression_done = false;

  if (profile_index >= SETTINGS_PROFILE_COUNT || settings == NULL ||
      !profile_document_actions_are_valid(actions) ||
      async_state.phase != PROFILE_DOCUMENT_PHASE_IDLE) {
    return false;
  }
  memcpy(&async_state.raw_snapshot.settings, settings, sizeof(*settings));
  memcpy(&async_state.raw_snapshot.actions, actions, sizeof(*actions));
  memset(async_state.hash_positions, 0xFF,
         sizeof(async_state.hash_positions));
  async_state.compress_position = 0u;
  async_state.literal_anchor = 0u;
  async_state.encoded_length = 0u;
  do {
    if (!profile_document_compress_step(UINT32_MAX, &compression_done)) {
      return false;
    }
  } while (!compression_done);
  segments[0].data = &header;
  segments[0].length = sizeof(header);
  segments[1].data = async_state.encoded;
  segments[1].length = async_state.encoded_length;
  return flash_storage_object_write_segments(
             FLASH_STORAGE_NAMESPACE_PROFILE_DOCUMENT, profile_index,
             PROFILE_DOCUMENT_SCHEMA_VERSION, segments,
             (uint8_t)(sizeof(segments) / sizeof(segments[0])),
             expected_generation, new_generation) == FLASH_STORAGE_STATUS_OK;
}

bool profile_document_store_save_async_begin(
    uint8_t profile_index, const settings_profile_t *settings,
    const action_profile_t *actions, uint32_t expected_generation) {
  return profile_document_store_save_async_begin_tracked(
      profile_index, settings, actions, NULL, NULL, expected_generation);
}

bool profile_document_store_save_async_begin_tracked(
    uint8_t profile_index, const settings_profile_t *settings,
    const action_profile_t *actions,
    const volatile uint32_t *settings_source_revision,
    const volatile uint32_t *actions_source_revision,
    uint32_t expected_generation) {
  if (profile_index >= SETTINGS_PROFILE_COUNT || settings == NULL ||
      actions == NULL || async_state.phase != PROFILE_DOCUMENT_PHASE_IDLE) {
    return false;
  }

  async_state.phase = PROFILE_DOCUMENT_PHASE_COPY_SETTINGS;
  async_state.profile_index = profile_index;
  async_state.validate_index = 0u;
  async_state.copy_offset = 0u;
  async_state.expected_generation = expected_generation;
  async_state.new_generation = 0u;
  async_state.match_offset = 0u;
  async_state.stored_payload_length = 0u;
  async_state.stored_generation = 0u;
  async_state.stored_schema_version = 0u;
  async_state.settings_source = settings;
  async_state.actions_source = actions;
  async_state.settings_source_revision = settings_source_revision;
  async_state.actions_source_revision = actions_source_revision;
  async_state.settings_revision_at_copy_start =
      settings_source_revision != NULL ? *settings_source_revision : 0u;
  async_state.actions_revision_at_copy_start =
      actions_source_revision != NULL ? *actions_source_revision : 0u;
  async_state.source_restart_count = 0u;
  async_state.header.magic = PROFILE_DOCUMENT_MAGIC;
  async_state.header.schema_version = PROFILE_DOCUMENT_SCHEMA_VERSION;
  async_state.header.header_size = sizeof(profile_document_header_t);
  async_state.header.settings_size = sizeof(settings_profile_t);
  async_state.header.actions_size = sizeof(action_profile_t);
  return true;
}

static uint32_t profile_document_copy_chunk(void *destination,
                                            const void *source,
                                            uint32_t total_length,
                                            uint32_t byte_budget) {
  uint32_t remaining = total_length - async_state.copy_offset;
  uint32_t chunk = remaining > byte_budget ? byte_budget : remaining;

  if (chunk != 0u) {
    memcpy((uint8_t *)destination + async_state.copy_offset,
           (const uint8_t *)source + async_state.copy_offset, chunk);
    async_state.copy_offset += chunk;
  }
  return remaining - chunk;
}

void profile_document_store_async_task(uint32_t copy_byte_budget,
                                       uint16_t flash_word_budget) {
  if (copy_byte_budget == 0u) {
    copy_byte_budget = 1u;
  }

  switch (async_state.phase) {
  case PROFILE_DOCUMENT_PHASE_COPY_SETTINGS:
    if (profile_document_copy_chunk(
            &async_state.raw_snapshot.settings, async_state.settings_source,
            sizeof(async_state.raw_snapshot.settings), copy_byte_budget) ==
        0u) {
      async_state.copy_offset = 0u;
      async_state.phase = PROFILE_DOCUMENT_PHASE_COPY_ACTIONS;
    }
    break;

  case PROFILE_DOCUMENT_PHASE_COPY_ACTIONS:
    if (profile_document_copy_chunk(
            &async_state.raw_snapshot.actions, async_state.actions_source,
            sizeof(async_state.raw_snapshot.actions), copy_byte_budget) ==
        0u) {
      bool settings_changed =
          async_state.settings_source_revision != NULL &&
          *async_state.settings_source_revision !=
              async_state.settings_revision_at_copy_start;
      bool actions_changed =
          async_state.actions_source_revision != NULL &&
          *async_state.actions_source_revision !=
              async_state.actions_revision_at_copy_start;

      if (settings_changed || actions_changed) {
        if (async_state.source_restart_count >=
            PROFILE_DOCUMENT_ASYNC_SOURCE_RESTART_LIMIT) {
          async_state.phase = PROFILE_DOCUMENT_PHASE_ERROR;
          break;
        }
        async_state.source_restart_count++;
        async_state.settings_revision_at_copy_start =
            async_state.settings_source_revision != NULL
                ? *async_state.settings_source_revision
                : 0u;
        async_state.actions_revision_at_copy_start =
            async_state.actions_source_revision != NULL
                ? *async_state.actions_source_revision
                : 0u;
        async_state.copy_offset = 0u;
        async_state.phase = PROFILE_DOCUMENT_PHASE_COPY_SETTINGS;
        break;
      }
      async_state.copy_offset = 0u;
      async_state.validate_index = 0u;
      async_state.phase = PROFILE_DOCUMENT_PHASE_VALIDATE_PROGRAMS;
    }
    break;

  case PROFILE_DOCUMENT_PHASE_VALIDATE_PROGRAMS:
    if (action_engine_validate_program(
            &async_state.raw_snapshot.actions
                 .programs[async_state.validate_index]) != ACTION_VALIDATE_OK) {
      async_state.phase = PROFILE_DOCUMENT_PHASE_ERROR;
      break;
    }
    async_state.macro_reach[async_state.validate_index] =
        action_engine_program_macro_dependencies(
            &async_state.raw_snapshot.actions
                 .programs[async_state.validate_index]);
    async_state.validate_index++;
    if (async_state.validate_index >= ACTION_PROGRAM_COUNT) {
      async_state.validate_index = 0u;
      async_state.phase = PROFILE_DOCUMENT_PHASE_VALIDATE_MACRO_GRAPH;
    }
    break;

  case PROFILE_DOCUMENT_PHASE_VALIDATE_MACRO_GRAPH: {
    uint8_t via = async_state.validate_index;
    uint16_t via_bit = (uint16_t)(1u << via);
    for (uint8_t program = 0u; program < ACTION_PROGRAM_COUNT; program++) {
      if ((async_state.macro_reach[program] & via_bit) != 0u) {
        async_state.macro_reach[program] |= async_state.macro_reach[via];
      }
    }
    async_state.validate_index++;
    if (async_state.validate_index >= ACTION_PROGRAM_COUNT) {
      for (uint8_t program = 0u; program < ACTION_PROGRAM_COUNT; program++) {
        if ((async_state.macro_reach[program] &
             (uint16_t)(1u << program)) != 0u) {
          async_state.phase = PROFILE_DOCUMENT_PHASE_ERROR;
          break;
        }
      }
      if (async_state.phase != PROFILE_DOCUMENT_PHASE_ERROR) {
        async_state.macro_depth_pending = UINT16_MAX;
        memset(async_state.macro_depths, 0,
               sizeof(async_state.macro_depths));
        async_state.phase = PROFILE_DOCUMENT_PHASE_VALIDATE_MACRO_DEPTH;
      }
    }
    break;
  }

  case PROFILE_DOCUMENT_PHASE_VALIDATE_MACRO_DEPTH: {
    bool made_progress = false;
    for (uint8_t program = 0u; program < ACTION_PROGRAM_COUNT; program++) {
      uint16_t program_bit = (uint16_t)(1u << program);
      uint8_t depth = 1u;
      if ((async_state.macro_depth_pending & program_bit) == 0u ||
          (async_state.macro_reach[program] &
           async_state.macro_depth_pending) != 0u) {
        continue;
      }
      for (uint8_t target = 0u; target < ACTION_PROGRAM_COUNT; target++) {
        if ((async_state.macro_reach[program] &
             (uint16_t)(1u << target)) != 0u &&
            depth <= async_state.macro_depths[target]) {
          depth = (uint8_t)(async_state.macro_depths[target] + 1u);
        }
      }
      if (depth > ACTION_ENGINE_MAX_INSTANCES) {
        async_state.phase = PROFILE_DOCUMENT_PHASE_ERROR;
        break;
      }
      async_state.macro_depths[program] = depth;
      async_state.macro_depth_pending &= (uint16_t)~program_bit;
      made_progress = true;
      break;
    }
    if (async_state.phase == PROFILE_DOCUMENT_PHASE_ERROR) {
      break;
    }
    if (async_state.macro_depth_pending == 0u) {
      async_state.validate_index = 0u;
      async_state.phase = PROFILE_DOCUMENT_PHASE_VALIDATE_OVERLAYS;
    } else if (!made_progress) {
      /* The preceding transitive-closure phase already rejects cycles; keep a
       * fail-closed guard here in case the graph snapshot is ever corrupted. */
      async_state.phase = PROFILE_DOCUMENT_PHASE_ERROR;
    }
    break;
  }

  case PROFILE_DOCUMENT_PHASE_VALIDATE_OVERLAYS: {
    const action_overlay_binding_t *binding =
        &async_state.raw_snapshot.actions.overlays[async_state.validate_index];
    if (binding->state_index >= ACTION_STATE_COUNT ||
        binding->active_value > 1u || binding->follows_state > 1u ||
        binding->config.blend_mode >= (uint8_t)LED_OVERLAY_BLEND_MAX) {
      async_state.phase = PROFILE_DOCUMENT_PHASE_ERROR;
      break;
    }
    async_state.validate_index++;
    if (async_state.validate_index >= LED_STATE_OVERLAY_COUNT) {
      async_state.copy_offset = 0u;
      async_state.phase = PROFILE_DOCUMENT_PHASE_INIT_COMPRESS;
    }
    break;
  }

  case PROFILE_DOCUMENT_PHASE_INIT_COMPRESS: {
    uint32_t remaining =
        sizeof(async_state.hash_positions) - async_state.copy_offset;
    uint32_t chunk =
        remaining > copy_byte_budget ? copy_byte_budget : remaining;
    memset((uint8_t *)async_state.hash_positions + async_state.copy_offset,
           0xFF, chunk);
    async_state.copy_offset += chunk;
    if (async_state.copy_offset == sizeof(async_state.hash_positions)) {
      async_state.compress_position = 0u;
      async_state.literal_anchor = 0u;
      async_state.encoded_length = 0u;
      async_state.phase = PROFILE_DOCUMENT_PHASE_COMPRESS;
    }
    break;
  }

  case PROFILE_DOCUMENT_PHASE_COMPRESS: {
    bool done = false;
    if (!profile_document_compress_step(copy_byte_budget, &done)) {
      async_state.phase = PROFILE_DOCUMENT_PHASE_ERROR;
    } else if (done) {
      async_state.phase = PROFILE_DOCUMENT_PHASE_MATCH_HEADER;
    }
    break;
  }

  case PROFILE_DOCUMENT_PHASE_MATCH_HEADER: {
    profile_document_header_t stored_header;
    uint32_t actual_length = 0u;
    uint32_t next_expected = async_state.expected_generation + 1u;

    if (!profile_document_store_get_generation(
            async_state.profile_index, &async_state.stored_generation)) {
      async_state.phase = PROFILE_DOCUMENT_PHASE_ERROR;
      break;
    }

    /* A normal CAS write still advances the generation. Only the expected+1
     * case can be a retry whose successful response was lost. Comparing that
     * candidate incrementally preserves idempotency without decoding 13 KiB
     * in the RAW-HID handler. */
    if (async_state.expected_generation == FLASH_STORAGE_GENERATION_ANY ||
        async_state.stored_generation == async_state.expected_generation) {
      async_state.phase = PROFILE_DOCUMENT_PHASE_WAIT_FLASH;
      break;
    }
    if (next_expected == 0u) {
      next_expected = 1u;
    }
    if (async_state.stored_generation != next_expected ||
        !profile_document_read_header(
            async_state.profile_index, &stored_header, &actual_length,
            &async_state.stored_schema_version,
            &async_state.stored_generation)) {
      async_state.phase = PROFILE_DOCUMENT_PHASE_WAIT_FLASH;
      break;
    }

    async_state.stored_payload_length =
        actual_length - (uint32_t)sizeof(stored_header);
    if ((async_state.stored_schema_version ==
             PROFILE_DOCUMENT_RAW_SCHEMA_VERSION &&
         async_state.stored_payload_length != PROFILE_DOCUMENT_RAW_SIZE) ||
        (async_state.stored_schema_version == PROFILE_DOCUMENT_SCHEMA_VERSION &&
         async_state.stored_payload_length != async_state.encoded_length)) {
      async_state.phase = PROFILE_DOCUMENT_PHASE_WAIT_FLASH;
      break;
    }
    async_state.match_offset = 0u;
    async_state.phase = PROFILE_DOCUMENT_PHASE_MATCH_PAYLOAD;
    break;
  }

  case PROFILE_DOCUMENT_PHASE_MATCH_PAYLOAD: {
    uint8_t stored[32];
    const uint8_t *expected =
        async_state.stored_schema_version ==
                PROFILE_DOCUMENT_RAW_SCHEMA_VERSION
            ? (const uint8_t *)&async_state.raw_snapshot
            : async_state.encoded;
    uint32_t remaining =
        async_state.stored_payload_length - async_state.match_offset;
    uint32_t chunk = remaining;

    if (chunk > copy_byte_budget) {
      chunk = copy_byte_budget;
    }
    if (chunk > sizeof(stored)) {
      chunk = sizeof(stored);
    }
    if (chunk == 0u) {
      async_state.new_generation = async_state.stored_generation;
      async_state.phase = PROFILE_DOCUMENT_PHASE_DONE;
      break;
    }
    if (flash_storage_object_read_range(
            FLASH_STORAGE_NAMESPACE_PROFILE_DOCUMENT,
            async_state.profile_index,
            (uint32_t)sizeof(profile_document_header_t) +
                async_state.match_offset,
            stored, chunk, NULL, NULL, NULL) != FLASH_STORAGE_STATUS_OK ||
        memcmp(stored, expected + async_state.match_offset, chunk) != 0) {
      async_state.phase = PROFILE_DOCUMENT_PHASE_WAIT_FLASH;
      break;
    }
    async_state.match_offset += chunk;
    if (async_state.match_offset == async_state.stored_payload_length) {
      async_state.new_generation = async_state.stored_generation;
      async_state.phase = PROFILE_DOCUMENT_PHASE_DONE;
    }
    break;
  }

  case PROFILE_DOCUMENT_PHASE_WAIT_FLASH: {
    const flash_storage_segment_t segments[] = {
        {.data = &async_state.header, .length = sizeof(async_state.header)},
        {.data = async_state.encoded,
         .length = async_state.encoded_length},
    };
    flash_storage_status_t status =
        flash_storage_object_write_segments_async_begin(
            FLASH_STORAGE_NAMESPACE_PROFILE_DOCUMENT,
            async_state.profile_index, PROFILE_DOCUMENT_SCHEMA_VERSION,
            segments, (uint8_t)(sizeof(segments) / sizeof(segments[0])),
            async_state.expected_generation, &async_state.new_generation);
    if (status == FLASH_STORAGE_STATUS_BUSY) {
      break;
    }
    async_state.phase = status == FLASH_STORAGE_STATUS_OK
                            ? PROFILE_DOCUMENT_PHASE_FLASH
                            : PROFILE_DOCUMENT_PHASE_ERROR;
    break;
  }

  case PROFILE_DOCUMENT_PHASE_FLASH: {
    if (!flash_storage_write_async_is_owner(
            FLASH_STORAGE_NAMESPACE_PROFILE_DOCUMENT,
            async_state.profile_index)) {
      /* A singleton journal transaction must never be advanced by a client
       * that did not start it. Treat loss of ownership as a hard local error;
       * the actual owner remains free to complete its transaction. */
      async_state.phase = PROFILE_DOCUMENT_PHASE_ERROR;
      break;
    }
    flash_storage_async_result_t result =
        flash_storage_write_async_step(flash_word_budget);
    if (result == FLASH_STORAGE_ASYNC_DONE) {
      async_state.phase = PROFILE_DOCUMENT_PHASE_DONE;
    } else if (result == FLASH_STORAGE_ASYNC_ERROR) {
      async_state.phase = PROFILE_DOCUMENT_PHASE_ERROR;
    }
    break;
  }

  case PROFILE_DOCUMENT_PHASE_IDLE:
  case PROFILE_DOCUMENT_PHASE_DONE:
  case PROFILE_DOCUMENT_PHASE_ERROR:
  default:
    break;
  }
}

profile_document_async_result_t
profile_document_store_async_result(uint32_t *new_generation) {
  if (async_state.phase == PROFILE_DOCUMENT_PHASE_IDLE) {
    return PROFILE_DOCUMENT_ASYNC_IDLE;
  }
  if (async_state.phase == PROFILE_DOCUMENT_PHASE_DONE) {
    if (new_generation != NULL) {
      *new_generation = async_state.new_generation;
    }
    return PROFILE_DOCUMENT_ASYNC_DONE;
  }
  if (async_state.phase == PROFILE_DOCUMENT_PHASE_ERROR) {
    return PROFILE_DOCUMENT_ASYNC_ERROR;
  }
  return PROFILE_DOCUMENT_ASYNC_IN_PROGRESS;
}

void profile_document_store_async_consume(void) {
  if (async_state.phase == PROFILE_DOCUMENT_PHASE_DONE ||
      async_state.phase == PROFILE_DOCUMENT_PHASE_ERROR) {
    async_state.phase = PROFILE_DOCUMENT_PHASE_IDLE;
    async_state.settings_source = NULL;
    async_state.actions_source = NULL;
    async_state.settings_source_revision = NULL;
    async_state.actions_source_revision = NULL;
  }
}
