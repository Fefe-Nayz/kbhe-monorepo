#include "adc_capture.h"
#include "analog/analog.h"

#include <string.h>

typedef struct {
  uint8_t active;
  uint8_t key_index;
  uint16_t reserved;
  uint32_t start_time_ms;
  uint32_t duration_ms;
  uint32_t sample_count;
  uint32_t overflow_count;
} adc_capture_state_t;

static adc_capture_state_t g_capture;
static uint16_t g_raw_samples[ADC_CAPTURE_MAX_SAMPLES];
static uint16_t g_filtered_samples[ADC_CAPTURE_MAX_SAMPLES];

void adc_capture_init(void) {
  memset(&g_capture, 0, sizeof(g_capture));
}

bool adc_capture_start(uint8_t key_index, uint32_t duration_ms) {
  if (key_index >= 6u || duration_ms == 0u) {
    return false;
  }

  g_capture.active = 1u;
  g_capture.key_index = key_index;
  g_capture.start_time_ms = 0u;
  g_capture.duration_ms = duration_ms;
  g_capture.sample_count = 0u;
  g_capture.overflow_count = 0u;

  return true;
}

void adc_capture_cancel(void) { g_capture.active = 0u; }

void adc_capture_process_scan(const uint16_t *adc_filtered_values,
                              uint8_t key_count, uint32_t now_ms) {
  if (!g_capture.active || adc_filtered_values == NULL) {
    return;
  }

  if (g_capture.key_index >= key_count) {
    g_capture.active = 0u;
    return;
  }

  if (g_capture.sample_count == 0u) {
    g_capture.start_time_ms = now_ms;
  }

  if (g_capture.sample_count < ADC_CAPTURE_MAX_SAMPLES) {
    uint32_t sample_index = g_capture.sample_count;
    g_raw_samples[sample_index] = analog_read_raw_value(g_capture.key_index);
    g_filtered_samples[sample_index] = adc_filtered_values[g_capture.key_index];
    g_capture.sample_count++;
  } else {
    g_capture.overflow_count++;
  }

  if ((now_ms - g_capture.start_time_ms) >= g_capture.duration_ms) {
    g_capture.active = 0u;
  }
}

bool adc_capture_is_active(void) { return g_capture.active != 0u; }

uint8_t adc_capture_key_index(void) { return g_capture.key_index; }

uint32_t adc_capture_duration_ms(void) { return g_capture.duration_ms; }

uint32_t adc_capture_start_time_ms(void) { return g_capture.start_time_ms; }

uint32_t adc_capture_sample_count(void) { return g_capture.sample_count; }

uint32_t adc_capture_overflow_count(void) { return g_capture.overflow_count; }

uint8_t adc_capture_read_chunk(uint32_t start_index, uint8_t max_samples,
                               uint16_t *raw_out, uint16_t *filtered_out,
                               uint32_t *total_samples_out) {
  uint32_t total = g_capture.sample_count;

  if (total_samples_out != NULL) {
    *total_samples_out = total;
  }

  if (raw_out == NULL || filtered_out == NULL || start_index >= total) {
    return 0u;
  }

  if (max_samples == 0u || max_samples > ADC_CAPTURE_MAX_READ_SAMPLES) {
    max_samples = ADC_CAPTURE_MAX_READ_SAMPLES;
  }

  uint32_t remaining = total - start_index;
  uint8_t count = (remaining < max_samples) ? (uint8_t)remaining : max_samples;

  for (uint8_t i = 0; i < count; i++) {
    uint32_t idx = start_index + i;
    raw_out[i] = g_raw_samples[idx];
    filtered_out[i] = g_filtered_samples[idx];
  }

  return count;
}
