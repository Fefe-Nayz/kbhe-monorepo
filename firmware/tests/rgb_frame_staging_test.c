#include "rgb_frame_staging.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void make_frame(uint8_t *frame, uint8_t seed) {
  for (uint16_t i = 0u; i < LED_MATRIX_DATA_BYTES; i++) {
    frame[i] = (uint8_t)(seed + i);
  }
}

static rgb_frame_staging_result_t push_frame_chunk(
    rgb_frame_staging_t *staging, const uint8_t *frame, uint8_t chunk,
    uint32_t generation) {
  uint16_t offset = (uint16_t)chunk * HID_LED_BYTES_PER_CHUNK;
  uint16_t remaining = LED_MATRIX_DATA_BYTES - offset;
  uint8_t size = remaining > HID_LED_BYTES_PER_CHUNK
                     ? HID_LED_BYTES_PER_CHUNK
                     : (uint8_t)remaining;
  return rgb_frame_staging_push(staging, chunk, size, &frame[offset],
                                generation);
}

static void test_partial_and_late_chunks_never_complete(void) {
  rgb_frame_staging_t staging = {0};
  uint8_t frame[LED_MATRIX_DATA_BYTES];
  make_frame(frame, 3u);

  assert(push_frame_chunk(&staging, frame, 1u, 7u) ==
         RGB_FRAME_STAGING_INVALID);
  assert(push_frame_chunk(&staging, frame, 0u, 7u) ==
         RGB_FRAME_STAGING_ACCEPTED);
  for (uint8_t chunk = 1u; chunk < RGB_FRAME_STAGING_CHUNK_COUNT - 1u;
       chunk++) {
    assert(push_frame_chunk(&staging, frame, chunk, 7u) ==
           RGB_FRAME_STAGING_ACCEPTED);
  }
  assert(rgb_frame_staging_is_active(&staging));
  assert(push_frame_chunk(&staging, frame,
                          RGB_FRAME_STAGING_CHUNK_COUNT - 1u, 7u) ==
         RGB_FRAME_STAGING_COMPLETE);
  assert(!rgb_frame_staging_is_active(&staging));
  assert(memcmp(rgb_frame_staging_completed_frame(&staging), frame,
                sizeof(frame)) == 0);
  assert(push_frame_chunk(&staging, frame, 1u, 7u) ==
         RGB_FRAME_STAGING_INVALID);
}

static void test_new_chunk_zero_cannot_mix_generations(void) {
  rgb_frame_staging_t staging = {0};
  uint8_t old_frame[LED_MATRIX_DATA_BYTES];
  uint8_t new_frame[LED_MATRIX_DATA_BYTES];
  make_frame(old_frame, 11u);
  make_frame(new_frame, 101u);

  assert(push_frame_chunk(&staging, old_frame, 0u, 4u) ==
         RGB_FRAME_STAGING_ACCEPTED);
  assert(push_frame_chunk(&staging, old_frame, 1u, 4u) ==
         RGB_FRAME_STAGING_ACCEPTED);
  assert(push_frame_chunk(&staging, new_frame, 0u, 4u) ==
         RGB_FRAME_STAGING_ACCEPTED);
  for (uint8_t chunk = 1u; chunk < RGB_FRAME_STAGING_CHUNK_COUNT; chunk++) {
    rgb_frame_staging_result_t expected =
        chunk + 1u == RGB_FRAME_STAGING_CHUNK_COUNT
            ? RGB_FRAME_STAGING_COMPLETE
            : RGB_FRAME_STAGING_ACCEPTED;
    assert(push_frame_chunk(&staging, new_frame, chunk, 4u) == expected);
  }
  assert(memcmp(rgb_frame_staging_completed_frame(&staging), new_frame,
                sizeof(new_frame)) == 0);
}

static void test_generation_and_shape_changes_cancel(void) {
  rgb_frame_staging_t staging = {0};
  uint8_t frame[LED_MATRIX_DATA_BYTES];
  make_frame(frame, 29u);

  assert(push_frame_chunk(&staging, frame, 0u, 8u) ==
         RGB_FRAME_STAGING_ACCEPTED);
  assert(push_frame_chunk(&staging, frame, 1u, 9u) ==
         RGB_FRAME_STAGING_INVALID);
  assert(!rgb_frame_staging_is_active(&staging));

  assert(push_frame_chunk(&staging, frame, 0u, 8u) ==
         RGB_FRAME_STAGING_ACCEPTED);
  assert(rgb_frame_staging_push(&staging, 1u,
                                HID_LED_BYTES_PER_CHUNK - 1u, frame, 8u) ==
         RGB_FRAME_STAGING_INVALID);
  assert(!rgb_frame_staging_is_active(&staging));
}

int main(void) {
  test_partial_and_late_chunks_never_complete();
  test_new_chunk_zero_cannot_mix_generations();
  test_generation_and_shape_changes_cancel();
  puts("rgb_frame_staging_test: ok");
  return 0;
}
