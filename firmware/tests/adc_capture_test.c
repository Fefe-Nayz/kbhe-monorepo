#include "adc_capture.h"

#include "board_config.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

uint16_t analog_read_raw_value(uint8_t key) {
  return (uint16_t)(1000u + key);
}

static void test_capture_is_wired_to_scans_and_duration(void) {
  uint16_t filtered[NUM_KEYS];
  uint16_t raw_out[4] = {0};
  uint16_t filtered_out[4] = {0};
  uint32_t total = 0u;

  for (uint8_t i = 0u; i < NUM_KEYS; i++) {
    filtered[i] = (uint16_t)(2000u + i);
  }
  adc_capture_init();
  assert(!adc_capture_start(NUM_KEYS, 10u));
  assert(!adc_capture_start(0u, 0u));
  assert(adc_capture_start(3u, 2u));
  adc_capture_process_scan(filtered, NUM_KEYS, 10u);
  adc_capture_process_scan(filtered, NUM_KEYS, 11u);
  adc_capture_process_scan(filtered, NUM_KEYS, 12u);
  assert(!adc_capture_is_active());
  assert(adc_capture_sample_count() == 3u);
  assert(adc_capture_read_chunk(0u, 4u, raw_out, filtered_out, &total) == 3u);
  assert(total == 3u);
  for (uint8_t i = 0u; i < 3u; i++) {
    assert(raw_out[i] == 1003u);
    assert(filtered_out[i] == 2003u);
  }
}

static void test_capacity_stops_instead_of_running_forever(void) {
  uint16_t filtered[NUM_KEYS];
  memset(filtered, 0, sizeof(filtered));

  adc_capture_init();
  assert(adc_capture_start(0u, UINT32_MAX));
  for (uint32_t i = 0u; i <= ADC_CAPTURE_MAX_SAMPLES; i++) {
    adc_capture_process_scan(filtered, NUM_KEYS, 0u);
  }
  assert(!adc_capture_is_active());
  assert(adc_capture_sample_count() == ADC_CAPTURE_MAX_SAMPLES);
  assert(adc_capture_overflow_count() == 1u);
}

int main(void) {
  test_capture_is_wired_to_scans_and_duration();
  test_capacity_stops_instead_of_running_forever();
  puts("adc_capture_test: ok");
  return 0;
}
