#include "analog/diagnostic_profile_policy.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static void test_profiler_rotates_one_key_per_scan(void) {
  analog_diagnostic_profile_sweep_t sweep;

  analog_diagnostic_profile_reset(&sweep);

  for (uint8_t key = 0u; key < 82u; key++) {
    assert(analog_diagnostic_profile_take_key(&sweep, 82u) == key);
  }
  assert(sweep.next_key == 0u);
  assert(analog_diagnostic_profile_take_key(&sweep, 82u) == 0u);
  assert(sweep.next_key == 1u);
}

static void test_profiler_recovers_invalid_cursor(void) {
  analog_diagnostic_profile_sweep_t sweep;

  analog_diagnostic_profile_reset(&sweep);
  sweep.next_key = 255u;
  assert(analog_diagnostic_profile_take_key(&sweep, 82u) == 0u);
  assert(sweep.next_key == 1u);
  assert(analog_diagnostic_profile_take_key(NULL, 82u) == 0u);
  assert(analog_diagnostic_profile_take_key(&sweep, 0u) == 0u);
}

static analog_diagnostic_profile_sample_t sample_for_key(uint8_t key,
                                                          uint32_t base) {
  analog_diagnostic_profile_sample_t sample = {
      .raw_cycles = base,
      .filter_cycles = base + 1u,
      .calibration_cycles = base + 2u,
      .lut_cycles = base + 3u,
      .store_cycles = base + 4u,
      .key_cycles = base + 5u,
      .key_index = key,
  };
  return sample;
}

static void test_publishes_only_complete_non_overlapping_sweeps(void) {
  analog_diagnostic_profile_sweep_t sweep;
  analog_diagnostic_profile_snapshot_t published = {0};
  uint32_t first_raw_sum = 0u;
  uint32_t second_raw_sum = 0u;

  analog_diagnostic_profile_reset(&sweep);
  for (uint8_t key = 0u; key < 82u; key++) {
    analog_diagnostic_profile_sample_t sample = sample_for_key(key, key + 1u);
    first_raw_sum += sample.raw_cycles;
    assert(analog_diagnostic_profile_take_key(&sweep, 82u) == key);
    assert(analog_diagnostic_profile_record(&sweep, 82u, &sample, &published) ==
           (key == 81u));
  }
  assert(published.raw_cycles == first_raw_sum);
  assert(published.key_min_cycles == 6u);
  assert(published.key_max_cycles == 87u);
  assert(published.key_max_index == 81u);

  /* The last complete sweep remains published while the next one is partial. */
  for (uint8_t key = 0u; key < 41u; key++) {
    analog_diagnostic_profile_sample_t sample =
        sample_for_key(key, 100u + key);
    second_raw_sum += sample.raw_cycles;
    assert(analog_diagnostic_profile_take_key(&sweep, 82u) == key);
    assert(!analog_diagnostic_profile_record(&sweep, 82u, &sample,
                                             &published));
    assert(published.raw_cycles == first_raw_sum);
  }
  for (uint8_t key = 41u; key < 82u; key++) {
    analog_diagnostic_profile_sample_t sample =
        sample_for_key(key, 100u + key);
    second_raw_sum += sample.raw_cycles;
    assert(analog_diagnostic_profile_take_key(&sweep, 82u) == key);
    assert(analog_diagnostic_profile_record(&sweep, 82u, &sample, &published) ==
           (key == 81u));
  }
  assert(published.raw_cycles == second_raw_sum);
  assert(published.key_min_cycles == 105u);
  assert(published.key_max_cycles == 186u);
  assert(published.key_max_index == 81u);
}

static void test_reset_discards_partial_disabled_session(void) {
  analog_diagnostic_profile_sweep_t sweep;
  analog_diagnostic_profile_snapshot_t published = {0};

  analog_diagnostic_profile_reset(&sweep);
  for (uint8_t key = 0u; key < 10u; key++) {
    analog_diagnostic_profile_sample_t sample = sample_for_key(key, 50u);
    (void)analog_diagnostic_profile_take_key(&sweep, 82u);
    assert(!analog_diagnostic_profile_record(&sweep, 82u, &sample,
                                             &published));
  }

  /* This is the diagnostics-off transition used by analog_task(). */
  analog_diagnostic_profile_reset(&sweep);
  assert(sweep.sample_count == 0u);
  assert(sweep.next_key == 0u);
  assert(sweep.accumulated.raw_cycles == 0u);
  assert(analog_diagnostic_profile_take_key(&sweep, 82u) == 0u);
}

int main(void) {
  test_profiler_rotates_one_key_per_scan();
  test_profiler_recovers_invalid_cursor();
  test_publishes_only_complete_non_overlapping_sweeps();
  test_reset_discards_partial_disabled_session();
  puts("analog_diagnostic_profile_policy_test: OK");
  return 0;
}
