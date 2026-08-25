#include "analog/rest_estimator.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

static void analog_rest_estimator_reset_block(
    analog_rest_estimator_t *estimator, uint32_t now_ms) {
  memset(estimator->sums, 0, sizeof(estimator->sums));
  for (uint8_t key = 0u; key < NUM_KEYS; key++) {
    estimator->minima[key] = UINT16_MAX;
    estimator->maxima[key] = 0u;
  }
  estimator->sample_count = 0u;
  estimator->block_start_ms = now_ms;
  estimator->block_started = true;
  estimator->block_valid = true;
}

static uint16_t analog_rest_median5(const uint16_t *values) {
  uint16_t sorted[ANALOG_REST_BLOCK_COUNT];

  memcpy(sorted, values, sizeof(sorted));
  for (uint8_t i = 1u; i < ANALOG_REST_BLOCK_COUNT; i++) {
    uint16_t value = sorted[i];
    uint8_t position = i;
    while (position > 0u && sorted[position - 1u] > value) {
      sorted[position] = sorted[position - 1u];
      position--;
    }
    sorted[position] = value;
  }
  return sorted[ANALOG_REST_BLOCK_COUNT / 2u];
}

static void analog_rest_estimator_invalidate(
    analog_rest_estimator_t *estimator) {
  estimator->block_count = 0u;
  estimator->next_block = 0u;
  estimator->block_valid = false;
  estimator->ready = false;
}

static void analog_rest_estimator_finish_block(
    analog_rest_estimator_t *estimator, uint32_t now_ms) {
  if (estimator->sample_count < ANALOG_REST_MIN_SAMPLES_PER_BLOCK ||
      !estimator->block_valid) {
    estimator->block_count = 0u;
    estimator->next_block = 0u;
    estimator->ready = false;
    analog_rest_estimator_reset_block(estimator, now_ms);
    return;
  }

  for (uint8_t key = 0u; key < NUM_KEYS; key++) {
    if ((uint16_t)(estimator->maxima[key] - estimator->minima[key]) >
        ANALOG_REST_MAX_BLOCK_SPAN_ADC) {
      estimator->block_count = 0u;
      estimator->next_block = 0u;
      estimator->ready = false;
      analog_rest_estimator_reset_block(estimator, now_ms);
      return;
    }
    estimator->block_means[estimator->next_block][key] =
        (uint16_t)((estimator->sums[key] +
                    (uint32_t)(estimator->sample_count / 2u)) /
                   estimator->sample_count);
  }

  estimator->next_block =
      (uint8_t)((estimator->next_block + 1u) % ANALOG_REST_BLOCK_COUNT);
  if (estimator->block_count < ANALOG_REST_BLOCK_COUNT) {
    estimator->block_count++;
  }

  if (estimator->block_count == ANALOG_REST_BLOCK_COUNT) {
    uint16_t values[ANALOG_REST_BLOCK_COUNT];
    for (uint8_t key = 0u; key < NUM_KEYS; key++) {
      uint16_t minimum = UINT16_MAX;
      uint16_t maximum = 0u;
      for (uint8_t block = 0u; block < ANALOG_REST_BLOCK_COUNT; block++) {
        values[block] = estimator->block_means[block][key];
        if (values[block] < minimum) {
          minimum = values[block];
        }
        if (values[block] > maximum) {
          maximum = values[block];
        }
      }
      if ((uint16_t)(maximum - minimum) >
          ANALOG_REST_MAX_BLOCK_SPAN_ADC) {
        analog_rest_estimator_invalidate(estimator);
        analog_rest_estimator_reset_block(estimator, now_ms);
        return;
      }
      estimator->stable_values[key] = analog_rest_median5(values);
    }
    estimator->stable_at_ms = now_ms;
    estimator->ready = true;
  }

  analog_rest_estimator_reset_block(estimator, now_ms);
}

void analog_rest_estimator_init(analog_rest_estimator_t *estimator) {
  if (estimator == NULL) {
    return;
  }
  memset(estimator, 0, sizeof(*estimator));
  analog_rest_estimator_reset_block(estimator, 0u);
  estimator->block_started = false;
}

void analog_rest_estimator_observe(analog_rest_estimator_t *estimator,
                                   const uint16_t *raw_values,
                                   uint8_t key_count, uint32_t now_ms) {
  if (estimator == NULL || raw_values == NULL || key_count != NUM_KEYS) {
    return;
  }
  if (estimator->sample_timestamp_valid &&
      estimator->last_sample_ms == now_ms) {
    return;
  }
  estimator->last_sample_ms = now_ms;
  estimator->sample_timestamp_valid = true;

  if (!estimator->block_started) {
    analog_rest_estimator_reset_block(estimator, now_ms);
  }

  for (uint8_t key = 0u; key < NUM_KEYS; key++) {
    uint16_t sample = raw_values[key];
    estimator->sums[key] += sample;
    if (sample < estimator->minima[key]) {
      estimator->minima[key] = sample;
    }
    if (sample > estimator->maxima[key]) {
      estimator->maxima[key] = sample;
    }
    if ((uint16_t)(estimator->maxima[key] - estimator->minima[key]) >
        ANALOG_REST_MAX_BLOCK_SPAN_ADC) {
      analog_rest_estimator_invalidate(estimator);
    }
    if (estimator->ready) {
      uint16_t stable = estimator->stable_values[key];
      uint16_t delta = sample > stable ? (uint16_t)(sample - stable)
                                       : (uint16_t)(stable - sample);
      if (delta > ANALOG_REST_MAX_BLOCK_SPAN_ADC) {
        analog_rest_estimator_invalidate(estimator);
      }
    }
  }
  if (estimator->sample_count != UINT16_MAX) {
    estimator->sample_count++;
  }

  if ((uint32_t)(now_ms - estimator->block_start_ms) >=
      ANALOG_REST_BLOCK_DURATION_MS) {
    analog_rest_estimator_finish_block(estimator, now_ms);
  }
}

bool analog_rest_estimator_snapshot(const analog_rest_estimator_t *estimator,
                                    uint32_t now_ms, uint16_t reference_zero,
                                    uint16_t max_reference_delta,
                                    int16_t *values_out, uint8_t key_count) {
  if (estimator == NULL || values_out == NULL || key_count != NUM_KEYS ||
      !estimator->ready ||
      (uint32_t)(now_ms - estimator->stable_at_ms) > ANALOG_REST_MAX_AGE_MS) {
    return false;
  }

  for (uint8_t key = 0u; key < NUM_KEYS; key++) {
    uint16_t value = estimator->stable_values[key];
    uint16_t delta = value > reference_zero
                         ? (uint16_t)(value - reference_zero)
                         : (uint16_t)(reference_zero - value);
    if (delta > max_reference_delta || value > INT16_MAX) {
      return false;
    }
    values_out[key] = (int16_t)value;
  }
  return true;
}
