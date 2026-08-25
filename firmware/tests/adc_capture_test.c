#include "adc_capture.h"

#include "board_config.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint16_t test_raw[NUM_KEYS];
static uint16_t test_filtered[NUM_KEYS];
static int16_t test_distance[NUM_KEYS];

uint16_t analog_read_raw_value(uint8_t key) {
  return test_raw[key];
}

uint16_t analog_read_filtered_value(uint8_t key) {
  return test_filtered[key];
}

int16_t analog_read_travel_distance_value(uint8_t key) {
  return test_distance[key];
}

static void reset_samples(void) {
  for (uint8_t i = 0u; i < NUM_KEYS; i++) {
    test_raw[i] = (uint16_t)(1000u + i);
    test_filtered[i] = (uint16_t)(2000u + i);
    test_distance[i] = 0;
  }
}

static void test_capture_is_wired_to_scans_and_duration(void) {
  uint16_t filtered[NUM_KEYS];
  uint16_t raw_out[4] = {0};
  uint16_t filtered_out[4] = {0};
  uint32_t total = 0u;

  reset_samples();
  memcpy(filtered, test_filtered, sizeof(filtered));
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
  reset_samples();
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

static void test_sparse_trace_correlates_every_stage(void) {
  adc_input_trace_record_t record = {0};

  reset_samples();
  adc_capture_init();
  assert(!adc_input_trace_start(0u));
  assert(!adc_input_trace_start(ADC_INPUT_TRACE_MAX_DURATION_MS + 1u));
  assert(adc_input_trace_start(100u));
  assert(!adc_capture_start(0u, 10u));

  /* First scan is a coherent baseline for all 82 keys. */
  adc_capture_process_scan(test_filtered, NUM_KEYS, 100u);
  adc_input_trace_record_process_cycles(90u);
  assert(adc_input_trace_record_count() == 0u);

  test_raw[3] = 1050u;
  test_filtered[3] = 2050u;
  test_distance[3] = 1300;
  adc_capture_process_scan(test_filtered, NUM_KEYS, 101u);
  adc_input_trace_record_process_cycles(120u);
  adc_input_trace_trigger(3u, true);
  adc_input_trace_route(3u, 0x0Au, ADC_INPUT_TRACE_ROUTE_NKRO, true);
  adc_input_trace_hid_enqueue(ADC_INPUT_TRACE_ROUTE_NKRO, true);

  adc_input_trace_trigger(3u, false);
  adc_input_trace_route(3u, 0x0Au, ADC_INPUT_TRACE_ROUTE_NKRO, false);
  adc_input_trace_hid_enqueue(ADC_INPUT_TRACE_ROUTE_NKRO, true);

  test_raw[3] = 1003u;
  test_filtered[3] = 2003u;
  test_distance[3] = 0;
  for (uint32_t scan = 0u; scan < 16u; scan++) {
    adc_capture_process_scan(test_filtered, NUM_KEYS, 102u + scan);
    adc_input_trace_record_process_cycles(80u);
  }

  assert(adc_input_trace_record_count() == 1u);
  assert(adc_input_trace_read(0u, &record));
  assert(record.key_index == 3u);
  assert(record.keycode == 0x0Au);
  assert(record.route == ADC_INPUT_TRACE_ROUTE_NKRO);
  assert(record.raw_baseline == 1003u);
  assert(record.raw_max == 1050u);
  assert(record.filtered_max == 2050u);
  assert(record.distance_max_um == 1300u);
  assert(record.trigger_press_count == 1u);
  assert(record.trigger_release_count == 1u);
  assert(record.route_press_count == 1u);
  assert(record.route_release_count == 1u);
  assert(record.enqueue_press_count == 1u);
  assert(record.enqueue_release_count == 1u);
  assert(record.enqueue_failure_count == 0u);
  assert(record.trigger_press_scan != ADC_INPUT_TRACE_SCAN_UNSET);
  assert(record.enqueue_press_scan != ADC_INPUT_TRACE_SCAN_UNSET);
  assert((record.flags & ADC_INPUT_TRACE_FLAG_RAW_STARTED) != 0u);
  assert((record.flags & ADC_INPUT_TRACE_FLAG_ACTIVE_AT_END) == 0u);
  assert(adc_input_trace_max_process_cycles() == 120u);
  assert(adc_input_trace_total_process_cycles() ==
         (90u + 120u + 16u * 80u));
  assert(adc_input_trace_overflow_count() == 0u);
  adc_capture_process_scan(test_filtered, NUM_KEYS, 200u);
}

static void test_sparse_trace_records_pre_trigger_sensor_pulse(void) {
  adc_input_trace_record_t record = {0};

  reset_samples();
  adc_capture_init();
  assert(adc_input_trace_start(3u));
  adc_capture_process_scan(test_filtered, NUM_KEYS, 10u);
  test_raw[7] = 1040u;
  test_filtered[7] = 2040u;
  test_distance[7] = 300;
  adc_capture_process_scan(test_filtered, NUM_KEYS, 11u);
  adc_capture_process_scan(test_filtered, NUM_KEYS, 13u);

  assert(!adc_input_trace_is_active());
  assert(adc_input_trace_record_count() == 1u);
  assert(adc_input_trace_read(0u, &record));
  assert(record.key_index == 7u);
  assert(record.trigger_press_count == 0u);
  assert(record.route_press_count == 0u);
  assert(record.enqueue_press_count == 0u);
  assert((record.flags & ADC_INPUT_TRACE_FLAG_ACTIVE_AT_END) != 0u);
}

static void test_sparse_trace_exposes_enqueue_failures_and_repeat_storms(void) {
  adc_input_trace_record_t record = {0};

  reset_samples();
  adc_capture_init();
  assert(adc_input_trace_start(10u));
  adc_capture_process_scan(test_filtered, NUM_KEYS, 0u);
  adc_input_trace_trigger(5u, true);
  adc_input_trace_trigger(5u, true);
  adc_input_trace_route(5u, 0x0Cu, ADC_INPUT_TRACE_ROUTE_6KRO, true);
  adc_input_trace_route(5u, 0x0Cu, ADC_INPUT_TRACE_ROUTE_6KRO, true);
  adc_input_trace_hid_enqueue(ADC_INPUT_TRACE_ROUTE_6KRO, false);
  adc_input_trace_hid_enqueue(ADC_INPUT_TRACE_ROUTE_6KRO, true);
  adc_capture_process_scan(test_filtered, NUM_KEYS, 10u);

  assert(adc_input_trace_read(0u, &record));
  assert(record.trigger_press_count == 2u);
  assert(record.route_press_count == 2u);
  assert(record.enqueue_press_count == 1u);
  assert(record.enqueue_failure_count == 1u);
  assert((record.flags & ADC_INPUT_TRACE_FLAG_SYNTHETIC) != 0u);
  assert((record.flags & ADC_INPUT_TRACE_FLAG_ENQUEUE_FAILED) != 0u);
}

static void test_sparse_trace_restart_hides_stale_records(void) {
  adc_input_trace_record_t record = {0};

  reset_samples();
  adc_capture_init();
  assert(adc_input_trace_start(10u));
  adc_capture_process_scan(test_filtered, NUM_KEYS, 0u);
  adc_input_trace_trigger(9u, true);
  assert(adc_input_trace_record_count() == 1u);
  adc_capture_process_scan(test_filtered, NUM_KEYS, 10u);
  assert(!adc_input_trace_is_active());
  assert(adc_input_trace_read(0u, &record));
  assert(record.key_index == 9u);

  /* Restart only resets the small state and per-key indexes. The 64 KiB
   * shared arena is cleared lazily, one record at a time, so arming cannot
   * create a scan-sized pause. record_count still makes stale bytes
   * inaccessible immediately. */
  assert(adc_input_trace_start(10u));
  assert(adc_input_trace_record_count() == 0u);
  assert(!adc_input_trace_read(0u, &record));
  adc_capture_process_scan(test_filtered, NUM_KEYS, 20u);
  adc_input_trace_trigger(4u, true);
  assert(adc_input_trace_record_count() == 1u);
  assert(adc_input_trace_read(0u, &record));
  assert(record.key_index == 4u);
  assert(record.trigger_press_count == 1u);
}

static void test_sparse_trace_flags_one_long_excursion_without_ending_session(void) {
  adc_input_trace_record_t record = {0};

  reset_samples();
  adc_capture_init();
  assert(adc_input_trace_start(ADC_INPUT_TRACE_MAX_DURATION_MS));
  adc_capture_process_scan(test_filtered, NUM_KEYS, 0u);
  adc_input_trace_trigger(1u, true);
  for (uint32_t scan = 0u; scan < ADC_INPUT_TRACE_SCAN_UNSET; scan++) {
    adc_capture_process_scan(test_filtered, NUM_KEYS, 0u);
  }
  assert(adc_input_trace_is_active());
  adc_input_trace_route(1u, 0x05u, ADC_INPUT_TRACE_ROUTE_NKRO, true);
  assert(adc_input_trace_read(0u, &record));
  assert(record.route_press_scan == ADC_INPUT_TRACE_SCAN_UNSET - 1u);
  assert((record.flags & ADC_INPUT_TRACE_FLAG_SCAN_SATURATED) != 0u);

  /* Saturating one pulse-local u16 offset must not impose a roughly seven
   * second limit on the requested 30-second trace session. */
  adc_capture_process_scan(test_filtered, NUM_KEYS,
                           ADC_INPUT_TRACE_MAX_DURATION_MS);
  assert(!adc_input_trace_is_active());
}

int main(void) {
  test_capture_is_wired_to_scans_and_duration();
  test_capacity_stops_instead_of_running_forever();
  test_sparse_trace_correlates_every_stage();
  test_sparse_trace_records_pre_trigger_sensor_pulse();
  test_sparse_trace_exposes_enqueue_failures_and_repeat_storms();
  test_sparse_trace_restart_hides_stale_records();
  test_sparse_trace_flags_one_long_excursion_without_ending_session();
  puts("adc_capture_test: ok");
  return 0;
}
