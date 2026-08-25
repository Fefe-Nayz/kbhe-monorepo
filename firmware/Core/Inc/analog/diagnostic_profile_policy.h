#ifndef KBHE_ANALOG_DIAGNOSTIC_PROFILE_POLICY_H_
#define KBHE_ANALOG_DIAGNOSTIC_PROFILE_POLICY_H_

#include <stdbool.h>
#include <stdint.h>

/* Detailed analog diagnostics used to time every operation for all 82 keys in
 * one scan. That burst performs 902 DWT reads and, at the old 1/32 sampling
 * cadence, delayed almost exactly one scan in 32 past the 125 us deadline.
 * Sample one rotating key per scan instead: a complete breakdown is refreshed
 * after NUM_KEYS scans while the real-time cost stays bounded and amortized. */
#define ANALOG_DIAGNOSTIC_PROFILE_STEPS_PER_KEY 5u
#define ANALOG_DIAGNOSTIC_PROFILE_KEYS_PER_SCAN 1u
#define ANALOG_DIAGNOSTIC_PROFILE_DWT_READS_PER_KEY                         \
    (ANALOG_DIAGNOSTIC_PROFILE_STEPS_PER_KEY * 2u + 1u)
#define ANALOG_DIAGNOSTIC_PROFILE_DWT_READS_PER_SCAN                        \
    (ANALOG_DIAGNOSTIC_PROFILE_DWT_READS_PER_KEY *                         \
     ANALOG_DIAGNOSTIC_PROFILE_KEYS_PER_SCAN)
#define ANALOG_DIAGNOSTIC_PROFILE_DWT_READ_BUDGET 11u

_Static_assert(ANALOG_DIAGNOSTIC_PROFILE_KEYS_PER_SCAN == 1u,
               "the rotating profiler assumes one key per scan");
_Static_assert(ANALOG_DIAGNOSTIC_PROFILE_DWT_READS_PER_SCAN <=
                   ANALOG_DIAGNOSTIC_PROFILE_DWT_READ_BUDGET,
               "analog diagnostics exceeded their per-scan timing budget");

typedef struct {
  uint32_t raw_cycles;
  uint32_t filter_cycles;
  uint32_t calibration_cycles;
  uint32_t lut_cycles;
  uint32_t store_cycles;
  uint32_t key_cycles;
  uint8_t key_index;
} analog_diagnostic_profile_sample_t;

typedef struct {
  uint32_t raw_cycles;
  uint32_t filter_cycles;
  uint32_t calibration_cycles;
  uint32_t lut_cycles;
  uint32_t store_cycles;
  uint32_t key_sum_cycles;
  uint32_t key_min_cycles;
  uint32_t key_max_cycles;
  uint8_t key_max_index;
} analog_diagnostic_profile_snapshot_t;

typedef struct {
  analog_diagnostic_profile_snapshot_t accumulated;
  uint8_t next_key;
  uint8_t sample_count;
} analog_diagnostic_profile_sweep_t;

static inline void analog_diagnostic_profile_reset(
    analog_diagnostic_profile_sweep_t *sweep) {
  if (sweep == 0) {
    return;
  }

  sweep->accumulated.raw_cycles = 0u;
  sweep->accumulated.filter_cycles = 0u;
  sweep->accumulated.calibration_cycles = 0u;
  sweep->accumulated.lut_cycles = 0u;
  sweep->accumulated.store_cycles = 0u;
  sweep->accumulated.key_sum_cycles = 0u;
  sweep->accumulated.key_min_cycles = UINT32_MAX;
  sweep->accumulated.key_max_cycles = 0u;
  sweep->accumulated.key_max_index = 0u;
  sweep->next_key = 0u;
  sweep->sample_count = 0u;
}

static inline uint8_t
analog_diagnostic_profile_take_key(analog_diagnostic_profile_sweep_t *sweep,
                                   uint8_t key_count) {
  uint8_t selected = 0u;

  if (sweep == 0 || key_count == 0u) {
    return 0u;
  }

  selected = sweep->next_key;
  if (selected >= key_count) {
    selected = 0u;
  }
  selected = (uint8_t)(selected % key_count);
  sweep->next_key = (uint8_t)(selected + 1u);
  if (sweep->next_key >= key_count) {
    sweep->next_key = 0u;
  }
  return selected;
}

/* Return true only when a complete, non-overlapping all-key sweep is copied
 * to `published`. A reset (used whenever diagnostics is disabled) discards a
 * partial sweep, so a later session can never combine old and new samples. */
static inline bool analog_diagnostic_profile_record(
    analog_diagnostic_profile_sweep_t *sweep, uint8_t key_count,
    const analog_diagnostic_profile_sample_t *sample,
    analog_diagnostic_profile_snapshot_t *published) {
  if (sweep == 0 || sample == 0 || published == 0 || key_count == 0u) {
    return false;
  }

  sweep->accumulated.raw_cycles += sample->raw_cycles;
  sweep->accumulated.filter_cycles += sample->filter_cycles;
  sweep->accumulated.calibration_cycles += sample->calibration_cycles;
  sweep->accumulated.lut_cycles += sample->lut_cycles;
  sweep->accumulated.store_cycles += sample->store_cycles;
  sweep->accumulated.key_sum_cycles += sample->key_cycles;
  if (sample->key_cycles < sweep->accumulated.key_min_cycles) {
    sweep->accumulated.key_min_cycles = sample->key_cycles;
  }
  if (sample->key_cycles >= sweep->accumulated.key_max_cycles) {
    sweep->accumulated.key_max_cycles = sample->key_cycles;
    sweep->accumulated.key_max_index = sample->key_index;
  }

  sweep->sample_count++;
  if (sweep->sample_count < key_count) {
    return false;
  }

  *published = sweep->accumulated;
  analog_diagnostic_profile_reset(sweep);
  return true;
}

#endif /* KBHE_ANALOG_DIAGNOSTIC_PROFILE_POLICY_H_ */
