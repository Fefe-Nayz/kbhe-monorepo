#include "analog/rest_estimator.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static void fill_snapshot(uint16_t *samples, uint16_t base) {
  for (uint8_t key = 0u; key < NUM_KEYS; key++) {
    samples[key] = (uint16_t)(base + (key % 5u));
  }
}

static void observe_range(analog_rest_estimator_t *estimator,
                          uint16_t *samples, uint32_t first_ms,
                          uint32_t last_ms) {
  for (uint32_t now_ms = first_ms; now_ms <= last_ms; now_ms++) {
    analog_rest_estimator_observe(estimator, samples, NUM_KEYS, now_ms);
  }
}

static void test_requires_complete_stable_window(void) {
  analog_rest_estimator_t estimator;
  uint16_t samples[NUM_KEYS];
  int16_t result[NUM_KEYS];

  analog_rest_estimator_init(&estimator);
  fill_snapshot(samples, 2170u);

  observe_range(&estimator, samples, 0u, 2499u);
  assert(!analog_rest_estimator_snapshot(&estimator, 2499u, 2195u, 256u,
                                         result, NUM_KEYS));

  analog_rest_estimator_observe(&estimator, samples, NUM_KEYS, 2500u);
  assert(analog_rest_estimator_snapshot(&estimator, 2500u, 2195u, 256u,
                                        result, NUM_KEYS));
  for (uint8_t key = 0u; key < NUM_KEYS; key++) {
    assert(result[key] == (int16_t)samples[key]);
  }
}

static void test_motion_invalidates_ready_estimate(void) {
  analog_rest_estimator_t estimator;
  uint16_t samples[NUM_KEYS];
  int16_t result[NUM_KEYS];

  analog_rest_estimator_init(&estimator);
  fill_snapshot(samples, 2170u);
  observe_range(&estimator, samples, 0u, 2500u);
  assert(analog_rest_estimator_snapshot(&estimator, 2500u, 2195u, 256u,
                                        result, NUM_KEYS));

  samples[37] += ANALOG_REST_MAX_BLOCK_SPAN_ADC + 1u;
  analog_rest_estimator_observe(&estimator, samples, NUM_KEYS, 2501u);
  assert(!analog_rest_estimator_snapshot(&estimator, 2501u, 2195u, 256u,
                                         result, NUM_KEYS));
}

static void test_rejects_stale_or_implausible_rest(void) {
  analog_rest_estimator_t estimator;
  uint16_t samples[NUM_KEYS];
  int16_t result[NUM_KEYS];

  analog_rest_estimator_init(&estimator);
  fill_snapshot(samples, 2170u);
  observe_range(&estimator, samples, 0u, 2500u);

  assert(!analog_rest_estimator_snapshot(&estimator, 2500u, 2400u, 64u,
                                         result, NUM_KEYS));
  assert(!analog_rest_estimator_snapshot(
      &estimator, 2500u + ANALOG_REST_MAX_AGE_MS + 1u, 2195u, 256u, result,
      NUM_KEYS));
}

static void test_duplicate_millisecond_is_decimated(void) {
  analog_rest_estimator_t estimator;
  uint16_t samples[NUM_KEYS];
  int16_t result[NUM_KEYS];

  analog_rest_estimator_init(&estimator);
  fill_snapshot(samples, 2170u);
  for (uint32_t now_ms = 0u; now_ms <= 2500u; now_ms++) {
    analog_rest_estimator_observe(&estimator, samples, NUM_KEYS, now_ms);
    samples[0] += 100u;
    analog_rest_estimator_observe(&estimator, samples, NUM_KEYS, now_ms);
    samples[0] -= 100u;
  }

  assert(analog_rest_estimator_snapshot(&estimator, 2500u, 2195u, 256u,
                                        result, NUM_KEYS));
  assert(result[0] == 2170);
}

int main(void) {
  test_requires_complete_stable_window();
  test_motion_invalidates_ready_estimate();
  test_rejects_stale_or_implausible_rest();
  test_duplicate_millisecond_is_decimated();
  puts("analog_rest_estimator_test: OK");
  return 0;
}
