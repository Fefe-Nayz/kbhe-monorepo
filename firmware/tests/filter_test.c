#include "analog/filter.h"

#include <assert.h>
#include <stdint.h>

static void assert_params(uint8_t expected_band, uint8_t expected_fast,
                          uint8_t expected_rest) {
  uint8_t band = 0u;
  uint8_t fast = 0u;
  uint8_t rest = 0u;

  filter_get_params(&band, &fast, &rest);
  assert(band == expected_band);
  assert(fast == expected_fast);
  assert(rest == expected_rest);
}

static void test_defaults_keep_public_order(void) {
  filter_init();
  assert_params(FILTER_DEFAULT_NOISE_BAND,
                FILTER_DEFAULT_ALPHA_MIN_DENOM,
                FILTER_DEFAULT_ALPHA_MAX_DENOM);
}

static void test_near_rest_uses_strong_smoothing(void) {
  filter_init();
  assert(filter_compute_next_filtered_value(0u, 1000u) == 1000u);
  filter_set_initialized(true);

  /* An eight-count movement sits inside the default noise band. With the
   * canonical 1/8 fast/rest denominators it advances by one count, not eight. */
  assert(filter_compute_next_filtered_value(0u, 1008u) == 1001u);

  /* Large real movement bypasses the EMA entirely. */
  assert(filter_compute_next_filtered_value(0u, 1100u) == 1100u);
}

static void test_legacy_swapped_snapshot_is_canonicalized(void) {
  filter_set_params(8u, 8u, 1u);
  assert_params(8u, 1u, 8u);

  filter_reset();
  assert(filter_compute_next_filtered_value(0u, 1000u) == 1000u);
  filter_set_initialized(true);
  assert(filter_compute_next_filtered_value(0u, 1008u) == 1001u);
}

int main(void) {
  test_defaults_keep_public_order();
  test_near_rest_uses_strong_smoothing();
  test_legacy_swapped_snapshot_is_canonicalized();
  return 0;
}
