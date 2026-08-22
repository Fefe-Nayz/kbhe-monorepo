#ifndef RGB_FRAME_STAGING_H_
#define RGB_FRAME_STAGING_H_

#include "hid_protocol.h"
#include "settings.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RGB_FRAME_STAGING_CHUNK_COUNT                                        \
  ((LED_MATRIX_DATA_BYTES + HID_LED_BYTES_PER_CHUNK - 1u) /                 \
   HID_LED_BYTES_PER_CHUNK)

#if RGB_FRAME_STAGING_CHUNK_COUNT > 32u
#error "RGB frame staging bitmap supports at most 32 chunks"
#endif

typedef enum {
  RGB_FRAME_STAGING_INVALID = 0,
  RGB_FRAME_STAGING_ACCEPTED,
  RGB_FRAME_STAGING_COMPLETE,
} rgb_frame_staging_result_t;

typedef struct {
  uint8_t frame[LED_MATRIX_DATA_BYTES];
  uint32_t received_chunks;
  uint32_t effect_generation;
  bool active;
} rgb_frame_staging_t;

/** Cancel any incomplete frame. The last completed frame bytes are retained. */
void rgb_frame_staging_reset(rgb_frame_staging_t *staging);

/** Return whether a frame transaction is currently awaiting more chunks. */
bool rgb_frame_staging_is_active(const rgb_frame_staging_t *staging);

/**
 * Stage one fixed-offset frame chunk.
 *
 * Chunk zero is the transaction boundary and always starts a fresh frame.
 * Every chunk must have its exact canonical length. A mode/effect generation
 * change or malformed chunk cancels the transaction. COMPLETE is returned
 * only after every chunk of one generation has arrived.
 */
rgb_frame_staging_result_t rgb_frame_staging_push(
    rgb_frame_staging_t *staging, uint8_t chunk_index, uint8_t chunk_size,
    const uint8_t *data, uint32_t effect_generation);

/** Frame bytes are valid after RGB_FRAME_STAGING_COMPLETE. */
const uint8_t *
rgb_frame_staging_completed_frame(const rgb_frame_staging_t *staging);

#ifdef __cplusplus
}
#endif

#endif /* RGB_FRAME_STAGING_H_ */
