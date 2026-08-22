#include "action_engine.h"
#include "flash_storage.h"
#include "profile_document_store.h"

#include <stddef.h>
#include <string.h>

#define ACTION_STORE_SCHEMA_VERSION 1u

/* The legacy action-engine persistence API returns once a durable write has
 * been queued. Keep its source immutable while ProfileDocument copies it in
 * bounded slices; action_engine's shared update scratch remains available for
 * non-persistent runtime edits. */
static action_profile_t action_store_pending_profile;
static bool action_store_save_pending;

static bool action_store_profile_is_valid(const action_profile_t *profile) {
  return action_engine_validate_profile(profile) == ACTION_VALIDATE_OK;
}

static bool action_store_flash_profile_is_valid(uint8_t profile_index) {
  action_program_t program;
  action_overlay_binding_t binding;
  uint8_t marker = 0u;
  uint32_t actual_len = 0u;
  uint16_t schema_version = 0u;

  if (flash_storage_object_read_range(
          FLASH_STORAGE_NAMESPACE_ACTION_PROFILE, profile_index, 0u, &marker,
          sizeof(marker), &actual_len, &schema_version, NULL) !=
          FLASH_STORAGE_STATUS_OK ||
      actual_len != sizeof(action_profile_t) ||
      schema_version != ACTION_STORE_SCHEMA_VERSION) {
    return false;
  }
  for (uint8_t i = 0u; i < ACTION_PROGRAM_COUNT; i++) {
    if (flash_storage_object_read_range(
            FLASH_STORAGE_NAMESPACE_ACTION_PROFILE, profile_index,
            offsetof(action_profile_t, programs) +
                (uint32_t)i * sizeof(program),
            &program, sizeof(program), NULL, NULL, NULL) !=
            FLASH_STORAGE_STATUS_OK ||
        action_engine_validate_program(&program) != ACTION_VALIDATE_OK) {
      return false;
    }
  }
  for (uint8_t i = 0u; i < LED_STATE_OVERLAY_COUNT; i++) {
    if (flash_storage_object_read_range(
            FLASH_STORAGE_NAMESPACE_ACTION_PROFILE, profile_index,
            offsetof(action_profile_t, overlays) +
                (uint32_t)i * sizeof(binding),
            &binding, sizeof(binding), NULL, NULL, NULL) !=
            FLASH_STORAGE_STATUS_OK ||
        binding.state_index >= ACTION_STATE_COUNT ||
        binding.active_value > 1u || binding.follows_state > 1u ||
        binding.config.blend_mode >= (uint8_t)LED_OVERLAY_BLEND_MAX) {
      return false;
    }
  }
  return true;
}

bool action_store_load_profile(uint8_t profile_index,
                               action_profile_t *profile_out) {
  uint8_t document_marker = 0u;

  if (profile_index >= ACTION_PROFILE_COUNT || profile_out == NULL) {
    return false;
  }
  flash_storage_status_t document_status = flash_storage_object_read_range(
      FLASH_STORAGE_NAMESPACE_PROFILE_DOCUMENT, profile_index, 0u,
      &document_marker, sizeof(document_marker), NULL, NULL, NULL);
  if (document_status == FLASH_STORAGE_STATUS_OK) {
    return profile_document_store_load(profile_index, NULL, profile_out, NULL);
  }
  if (document_status != FLASH_STORAGE_STATUS_NOT_FOUND) {
    return false;
  }
  if (!action_store_flash_profile_is_valid(profile_index) ||
      flash_storage_object_read(
          FLASH_STORAGE_NAMESPACE_ACTION_PROFILE, profile_index, profile_out,
          sizeof(*profile_out), NULL, NULL, NULL) != FLASH_STORAGE_STATUS_OK) {
    return false;
  }
  return action_store_profile_is_valid(profile_out);
}

bool action_store_save_profile(uint8_t profile_index,
                               const action_profile_t *profile) {
  const settings_profile_t *settings_profile = NULL;
  if (profile_index >= ACTION_PROFILE_COUNT ||
      !action_store_profile_is_valid(profile) || action_store_save_pending ||
      profile_document_store_async_result(NULL) !=
          PROFILE_DOCUMENT_ASYNC_IDLE) {
    return false;
  }
  settings_profile = settings_profile_snapshot_view(profile_index);
  if (settings_profile == NULL) {
    return false;
  }

  memcpy(&action_store_pending_profile, profile,
         sizeof(action_store_pending_profile));
  if (!profile_document_store_save_async_begin_tracked(
          profile_index, settings_profile, &action_store_pending_profile,
          settings_profile_snapshot_revision_source(profile_index), NULL,
          FLASH_STORAGE_GENERATION_ANY)) {
    return false;
  }
  action_store_save_pending = true;
  return true;
}

void action_store_async_task(void) {
  profile_document_async_result_t result;

  if (!action_store_save_pending) {
    return;
  }
  result = profile_document_store_async_result(NULL);
  if (result == PROFILE_DOCUMENT_ASYNC_DONE ||
      result == PROFILE_DOCUMENT_ASYNC_ERROR) {
    profile_document_store_async_consume();
    action_store_save_pending = false;
  }
}
