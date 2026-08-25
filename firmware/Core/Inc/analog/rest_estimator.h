#ifndef ANALOG_REST_ESTIMATOR_H_
#define ANALOG_REST_ESTIMATOR_H_

#include "board_config.h"

#include <stdbool.h>
#include <stdint.h>

/* Five independent half-second means reject a short accidental touch without
 * storing tens of thousands of ADC samples. Sampling is decimated to 1 kHz;
 * the estimator therefore costs no work in most 8 kHz input scans. */
#define ANALOG_REST_BLOCK_DURATION_MS 500u
#define ANALOG_REST_BLOCK_COUNT 5u
#define ANALOG_REST_MIN_SAMPLES_PER_BLOCK 250u
#define ANALOG_REST_MAX_BLOCK_SPAN_ADC 24u
#define ANALOG_REST_MAX_AGE_MS 750u

typedef struct {
  uint32_t sums[NUM_KEYS];
  uint16_t minima[NUM_KEYS];
  uint16_t maxima[NUM_KEYS];
  uint16_t block_means[ANALOG_REST_BLOCK_COUNT][NUM_KEYS];
  uint16_t stable_values[NUM_KEYS];
  uint32_t block_start_ms;
  uint32_t last_sample_ms;
  uint32_t stable_at_ms;
  uint16_t sample_count;
  uint8_t block_count;
  uint8_t next_block;
  bool block_started;
  bool sample_timestamp_valid;
  bool block_valid;
  bool ready;
} analog_rest_estimator_t;

void analog_rest_estimator_init(analog_rest_estimator_t *estimator);

/* Observe one immutable logical-key snapshot. Calls sharing the same
 * millisecond are ignored, bounding average work independently of scan rate. */
void analog_rest_estimator_observe(analog_rest_estimator_t *estimator,
                                   const uint16_t *raw_values,
                                   uint8_t key_count, uint32_t now_ms);

/* Return the latest stable median only when it is fresh and every key remains
 * near the supplied hardware rest reference. */
bool analog_rest_estimator_snapshot(const analog_rest_estimator_t *estimator,
                                    uint32_t now_ms, uint16_t reference_zero,
                                    uint16_t max_reference_delta,
                                    int16_t *values_out, uint8_t key_count);

#endif /* ANALOG_REST_ESTIMATOR_H_ */
