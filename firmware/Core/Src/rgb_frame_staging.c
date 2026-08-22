#include "rgb_frame_staging.h"

#include <string.h>

static uint8_t rgb_frame_expected_chunk_size(uint8_t chunk_index) {
  uint16_t offset = (uint16_t)chunk_index * HID_LED_BYTES_PER_CHUNK;
  uint16_t remaining = LED_MATRIX_DATA_BYTES - offset;
  return remaining > HID_LED_BYTES_PER_CHUNK
             ? HID_LED_BYTES_PER_CHUNK
             : (uint8_t)remaining;
}

static uint32_t rgb_frame_complete_mask(void) {
#if RGB_FRAME_STAGING_CHUNK_COUNT == 32u
  return UINT32_MAX;
#else
  return (UINT32_C(1) << RGB_FRAME_STAGING_CHUNK_COUNT) - 1u;
#endif
}

void rgb_frame_staging_reset(rgb_frame_staging_t *staging) {
  if (staging == NULL) {
    return;
  }
  staging->received_chunks = 0u;
  staging->effect_generation = 0u;
  staging->active = false;
}

bool rgb_frame_staging_is_active(const rgb_frame_staging_t *staging) {
  return staging != NULL && staging->active;
}

rgb_frame_staging_result_t rgb_frame_staging_push(
    rgb_frame_staging_t *staging, uint8_t chunk_index, uint8_t chunk_size,
    const uint8_t *data, uint32_t effect_generation) {
  uint16_t offset = 0u;

  if (staging == NULL) {
    return RGB_FRAME_STAGING_INVALID;
  }

  /* A new chunk zero supersedes an incomplete predecessor even when the new
   * packet is malformed. Later packets can never complete the old frame. */
  if (chunk_index == 0u) {
    rgb_frame_staging_reset(staging);
  }

  if (data == NULL || chunk_index >= RGB_FRAME_STAGING_CHUNK_COUNT ||
      chunk_size != rgb_frame_expected_chunk_size(chunk_index)) {
    rgb_frame_staging_reset(staging);
    return RGB_FRAME_STAGING_INVALID;
  }

  if (chunk_index == 0u) {
    staging->active = true;
    staging->effect_generation = effect_generation;
  } else if (!staging->active ||
             staging->effect_generation != effect_generation) {
    rgb_frame_staging_reset(staging);
    return RGB_FRAME_STAGING_INVALID;
  }

  offset = (uint16_t)chunk_index * HID_LED_BYTES_PER_CHUNK;
  memcpy(&staging->frame[offset], data, chunk_size);
  staging->received_chunks |= UINT32_C(1) << chunk_index;

  if (staging->received_chunks == rgb_frame_complete_mask()) {
    staging->received_chunks = 0u;
    staging->effect_generation = 0u;
    staging->active = false;
    return RGB_FRAME_STAGING_COMPLETE;
  }
  return RGB_FRAME_STAGING_ACCEPTED;
}

const uint8_t *
rgb_frame_staging_completed_frame(const rgb_frame_staging_t *staging) {
  return staging == NULL ? NULL : staging->frame;
}
