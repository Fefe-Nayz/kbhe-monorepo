#include "adc_capture.h"
#include "analog/analog.h"
#include "board_config.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

#define ADC_CAPTURE_BUFFER_BYTES (ADC_CAPTURE_MAX_SAMPLES * 4u)

/* Above the normal 8-count noise band, but low enough to catch a short tap
 * that never reaches an actuation threshold. */
#define ADC_INPUT_TRACE_RAW_START_DELTA 16u
#define ADC_INPUT_TRACE_RAW_END_DELTA 8u
#define ADC_INPUT_TRACE_FILTERED_START_DELTA 16u
#define ADC_INPUT_TRACE_FILTERED_END_DELTA 8u
#define ADC_INPUT_TRACE_END_STABLE_SCANS 16u
#define ADC_INPUT_TRACE_NO_RECORD UINT16_MAX

typedef struct {
  uint8_t active;
  uint8_t key_index;
  uint16_t reserved;
  uint32_t start_time_ms;
  uint32_t duration_ms;
  uint32_t sample_count;
  uint32_t overflow_count;
} adc_capture_state_t;

typedef struct {
  adc_input_trace_record_t records[ADC_INPUT_TRACE_MAX_RECORDS];
  uint16_t raw_baseline[NUM_KEYS];
  uint16_t filtered_baseline[NUM_KEYS];
  uint16_t active_record_index[NUM_KEYS];
  uint8_t rest_stable_scans[NUM_KEYS];
} adc_input_trace_storage_t;

typedef union {
  struct {
    uint16_t raw_samples[ADC_CAPTURE_MAX_SAMPLES];
    uint16_t filtered_samples[ADC_CAPTURE_MAX_SAMPLES];
  } adc;
  adc_input_trace_storage_t trace;
} adc_diagnostic_buffer_t;

_Static_assert(sizeof(adc_input_trace_storage_t) <= ADC_CAPTURE_BUFFER_BYTES,
               "all-key trace must fit inside the existing ADC capture arena");
_Static_assert(sizeof(adc_diagnostic_buffer_t) == ADC_CAPTURE_BUFFER_BYTES,
               "diagnostic arena must remain exactly 64 KiB");

typedef struct {
  uint8_t active;
  uint8_t reserved;
  uint16_t record_count;
  uint32_t duration_ms;
  uint32_t start_time_ms;
  uint32_t scan_count;
  uint32_t overflow_count;
  uint32_t max_process_cycles;
  uint32_t total_process_cycles;
  uint32_t measured_scan_count;
} adc_input_trace_state_t;

static adc_capture_state_t g_capture;
static adc_input_trace_state_t g_trace;
static adc_diagnostic_buffer_t g_diagnostic_buffer;

static uint16_t adc_input_trace_abs_delta(uint16_t lhs, uint16_t rhs) {
  return lhs >= rhs ? (uint16_t)(lhs - rhs) : (uint16_t)(rhs - lhs);
}

static uint16_t adc_input_trace_distance_um(uint8_t key_index) {
  int16_t distance = analog_read_travel_distance_value(key_index);
  return distance > 0 ? (uint16_t)distance : 0u;
}

static uint16_t
adc_input_trace_scan_offset(adc_input_trace_record_t *record) {
  uint32_t offset = g_trace.scan_count - record->start_scan;
  if (offset >= ADC_INPUT_TRACE_SCAN_UNSET) {
    record->flags |= ADC_INPUT_TRACE_FLAG_SCAN_SATURATED;
    return (uint16_t)(ADC_INPUT_TRACE_SCAN_UNSET - 1u);
  }
  return (uint16_t)offset;
}

static void adc_input_trace_increment(uint8_t *counter) {
  if (counter != NULL && *counter < UINT8_MAX) {
    (*counter)++;
  }
}

static void adc_input_trace_move_baseline(uint16_t *baseline,
                                          uint16_t current) {
  if (current > *baseline) {
    (*baseline)++;
  } else if (current < *baseline) {
    (*baseline)--;
  }
}

static void adc_input_trace_finish_record(uint8_t key_index,
                                          uint16_t record_index,
                                          bool active_at_end) {
  adc_input_trace_record_t *record = NULL;

  if (key_index >= NUM_KEYS || record_index >= g_trace.record_count) {
    return;
  }

  record = &g_diagnostic_buffer.trace.records[record_index];
  record->duration_scans = adc_input_trace_scan_offset(record);
  if (active_at_end) {
    record->flags |= ADC_INPUT_TRACE_FLAG_ACTIVE_AT_END;
  }
  g_diagnostic_buffer.trace.active_record_index[key_index] =
      ADC_INPUT_TRACE_NO_RECORD;
  g_diagnostic_buffer.trace.rest_stable_scans[key_index] = 0u;
}

static void adc_input_trace_stop(bool active_at_end) {
  if (!g_trace.active) {
    return;
  }

  for (uint8_t key = 0u; key < NUM_KEYS; key++) {
    uint16_t record_index =
        g_diagnostic_buffer.trace.active_record_index[key];
    if (record_index != ADC_INPUT_TRACE_NO_RECORD) {
      adc_input_trace_finish_record(key, record_index, active_at_end);
    }
  }

  g_trace.active = 0u;
}

static adc_input_trace_record_t *adc_input_trace_open_record(
    uint8_t key_index, uint8_t flags, uint16_t raw_value,
    uint16_t filtered_value, uint16_t distance_um) {
  adc_input_trace_record_t *record = NULL;
  uint16_t record_index = 0u;

  if (!g_trace.active || key_index >= NUM_KEYS) {
    return NULL;
  }

  record_index = g_diagnostic_buffer.trace.active_record_index[key_index];
  if (record_index != ADC_INPUT_TRACE_NO_RECORD) {
    return &g_diagnostic_buffer.trace.records[record_index];
  }

  if (g_trace.record_count >= ADC_INPUT_TRACE_MAX_RECORDS) {
    g_trace.overflow_count++;
    adc_input_trace_stop(true);
    return NULL;
  }

  record_index = g_trace.record_count++;
  record = &g_diagnostic_buffer.trace.records[record_index];
  memset(record, 0, sizeof(*record));
  record->start_scan = g_trace.scan_count;
  record->trigger_press_scan = ADC_INPUT_TRACE_SCAN_UNSET;
  record->trigger_release_scan = ADC_INPUT_TRACE_SCAN_UNSET;
  record->route_press_scan = ADC_INPUT_TRACE_SCAN_UNSET;
  record->route_release_scan = ADC_INPUT_TRACE_SCAN_UNSET;
  record->enqueue_press_scan = ADC_INPUT_TRACE_SCAN_UNSET;
  record->enqueue_release_scan = ADC_INPUT_TRACE_SCAN_UNSET;
  record->raw_baseline = g_diagnostic_buffer.trace.raw_baseline[key_index];
  record->raw_min = raw_value;
  record->raw_max = raw_value;
  record->filtered_baseline = filtered_value;
  record->filtered_max = filtered_value;
  record->distance_max_um = distance_um;
  record->key_index = key_index;
  record->flags = flags;

  g_diagnostic_buffer.trace.active_record_index[key_index] = record_index;
  g_diagnostic_buffer.trace.rest_stable_scans[key_index] = 0u;
  return record;
}

static adc_input_trace_record_t *adc_input_trace_open_from_runtime(
    uint8_t key_index, uint8_t flags) {
  return adc_input_trace_open_record(
      key_index, flags, analog_read_raw_value(key_index),
      analog_read_filtered_value(key_index),
      adc_input_trace_distance_um(key_index));
}

static void adc_input_trace_process_key(uint8_t key_index, uint16_t raw_value,
                                        uint16_t filtered_value,
                                        uint16_t distance_um) {
  uint16_t raw_delta = adc_input_trace_abs_delta(
      raw_value, g_diagnostic_buffer.trace.raw_baseline[key_index]);
  uint16_t filtered_delta = adc_input_trace_abs_delta(
      filtered_value, g_diagnostic_buffer.trace.filtered_baseline[key_index]);
  uint16_t record_index =
      g_diagnostic_buffer.trace.active_record_index[key_index];
  adc_input_trace_record_t *record = NULL;

  if (record_index == ADC_INPUT_TRACE_NO_RECORD) {
    uint8_t flags = 0u;
    if (raw_delta >= ADC_INPUT_TRACE_RAW_START_DELTA) {
      flags |= ADC_INPUT_TRACE_FLAG_RAW_STARTED;
    }
    if (filtered_delta >= ADC_INPUT_TRACE_FILTERED_START_DELTA) {
      flags |= ADC_INPUT_TRACE_FLAG_FILTERED_STARTED;
    }
    if (flags != 0u) {
      (void)adc_input_trace_open_record(key_index, flags, raw_value,
                                        filtered_value, distance_um);
      return;
    }

    if (raw_delta <= ADC_INPUT_TRACE_RAW_END_DELTA) {
      adc_input_trace_move_baseline(
          &g_diagnostic_buffer.trace.raw_baseline[key_index], raw_value);
    }
    if (filtered_delta <= ADC_INPUT_TRACE_FILTERED_END_DELTA) {
      adc_input_trace_move_baseline(
          &g_diagnostic_buffer.trace.filtered_baseline[key_index],
          filtered_value);
    }
    return;
  }

  if (record_index >= g_trace.record_count) {
    g_diagnostic_buffer.trace.active_record_index[key_index] =
        ADC_INPUT_TRACE_NO_RECORD;
    return;
  }

  record = &g_diagnostic_buffer.trace.records[record_index];
  if (raw_value < record->raw_min) {
    record->raw_min = raw_value;
  }
  if (raw_value > record->raw_max) {
    record->raw_max = raw_value;
  }
  if (filtered_value > record->filtered_max) {
    record->filtered_max = filtered_value;
  }
  if (distance_um > record->distance_max_um) {
    record->distance_max_um = distance_um;
  }

  if (raw_delta <= ADC_INPUT_TRACE_RAW_END_DELTA &&
      filtered_delta <= ADC_INPUT_TRACE_FILTERED_END_DELTA &&
      record->trigger_press_count == record->trigger_release_count) {
    uint8_t *stable_scans =
        &g_diagnostic_buffer.trace.rest_stable_scans[key_index];
    if (*stable_scans < ADC_INPUT_TRACE_END_STABLE_SCANS) {
      (*stable_scans)++;
    }
    if (*stable_scans >= ADC_INPUT_TRACE_END_STABLE_SCANS) {
      adc_input_trace_finish_record(key_index, record_index, false);
      g_diagnostic_buffer.trace.raw_baseline[key_index] = raw_value;
      g_diagnostic_buffer.trace.filtered_baseline[key_index] = filtered_value;
    }
  } else {
    g_diagnostic_buffer.trace.rest_stable_scans[key_index] = 0u;
  }
}

void adc_capture_init(void) {
  memset(&g_capture, 0, sizeof(g_capture));
  memset(&g_trace, 0, sizeof(g_trace));
}

bool adc_capture_start(uint8_t key_index, uint32_t duration_ms) {
  if (key_index >= NUM_KEYS || duration_ms == 0u || g_trace.active) {
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
  if (g_trace.active) {
    if (adc_filtered_values == NULL || key_count > NUM_KEYS) {
      adc_input_trace_stop(true);
      return;
    }

    g_trace.scan_count++;
    if (g_trace.scan_count == 1u) {
      g_trace.start_time_ms = now_ms;
    }
    for (uint8_t key = 0u; key < key_count; key++) {
      adc_input_trace_process_key(key, analog_read_raw_value(key),
                                  adc_filtered_values[key],
                                  adc_input_trace_distance_um(key));
      if (!g_trace.active) {
        break;
      }
    }

    if (g_trace.active &&
        (now_ms - g_trace.start_time_ms) >= g_trace.duration_ms) {
      adc_input_trace_stop(true);
    }
    return;
  }

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
    g_diagnostic_buffer.adc.raw_samples[sample_index] =
        analog_read_raw_value(g_capture.key_index);
    g_diagnostic_buffer.adc.filtered_samples[sample_index] =
        adc_filtered_values[g_capture.key_index];
    g_capture.sample_count++;
  } else {
    g_capture.overflow_count++;
    g_capture.active = 0u;
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
    raw_out[i] = g_diagnostic_buffer.adc.raw_samples[idx];
    filtered_out[i] = g_diagnostic_buffer.adc.filtered_samples[idx];
  }
  return count;
}

bool adc_input_trace_start(uint32_t duration_ms) {
  if (duration_ms == 0u || duration_ms > ADC_INPUT_TRACE_MAX_DURATION_MS ||
      g_capture.active || g_trace.active) {
    return false;
  }

  memset(&g_trace, 0, sizeof(g_trace));
  for (uint8_t key = 0u; key < NUM_KEYS; key++) {
    g_diagnostic_buffer.trace.active_record_index[key] =
        ADC_INPUT_TRACE_NO_RECORD;
    g_diagnostic_buffer.trace.rest_stable_scans[key] = 0u;
    g_diagnostic_buffer.trace.raw_baseline[key] = analog_read_raw_value(key);
    g_diagnostic_buffer.trace.filtered_baseline[key] =
        analog_read_filtered_value(key);
  }
  g_trace.duration_ms = duration_ms;
  g_trace.active = 1u;
  return true;
}

bool adc_input_trace_is_active(void) { return g_trace.active != 0u; }
uint32_t adc_input_trace_duration_ms(void) { return g_trace.duration_ms; }
uint32_t adc_input_trace_scan_count(void) { return g_trace.scan_count; }
uint16_t adc_input_trace_record_count(void) { return g_trace.record_count; }
uint32_t adc_input_trace_overflow_count(void) {
  return g_trace.overflow_count;
}
uint32_t adc_input_trace_max_process_cycles(void) {
  return g_trace.max_process_cycles;
}
uint32_t adc_input_trace_total_process_cycles(void) {
  return g_trace.total_process_cycles;
}

bool adc_input_trace_read(uint16_t record_index,
                          adc_input_trace_record_t *record_out) {
  if (record_out == NULL || record_index >= g_trace.record_count) {
    return false;
  }
  *record_out = g_diagnostic_buffer.trace.records[record_index];
  return true;
}

void adc_input_trace_record_process_cycles(uint32_t cycles) {
  if (g_trace.scan_count == 0u ||
      g_trace.measured_scan_count == g_trace.scan_count) {
    return;
  }

  g_trace.measured_scan_count = g_trace.scan_count;
  g_trace.total_process_cycles += cycles;
  if (cycles > g_trace.max_process_cycles) {
    g_trace.max_process_cycles = cycles;
  }
}

void adc_input_trace_trigger(uint8_t key_index, bool pressed) {
  adc_input_trace_record_t *record = NULL;

  if (!g_trace.active || key_index >= NUM_KEYS) {
    return;
  }
  record = adc_input_trace_open_from_runtime(
      key_index, ADC_INPUT_TRACE_FLAG_SYNTHETIC);
  if (record == NULL) {
    return;
  }

  if (pressed) {
    if (record->trigger_press_scan == ADC_INPUT_TRACE_SCAN_UNSET) {
      record->trigger_press_scan =
          adc_input_trace_scan_offset(record);
    }
    adc_input_trace_increment(&record->trigger_press_count);
  } else {
    if (record->trigger_release_scan == ADC_INPUT_TRACE_SCAN_UNSET) {
      record->trigger_release_scan =
          adc_input_trace_scan_offset(record);
    }
    adc_input_trace_increment(&record->trigger_release_count);
  }
}

void adc_input_trace_route(uint8_t key_index, uint16_t keycode,
                           adc_input_trace_route_t route, bool pressed) {
  adc_input_trace_record_t *record = NULL;

  if (!g_trace.active || key_index >= NUM_KEYS) {
    return;
  }
  record = adc_input_trace_open_from_runtime(
      key_index, ADC_INPUT_TRACE_FLAG_SYNTHETIC);
  if (record == NULL) {
    return;
  }

  if (record->keycode == 0u) {
    record->keycode = keycode;
  }
  record->route = (uint8_t)route;
  if (pressed) {
    if (record->route_press_scan == ADC_INPUT_TRACE_SCAN_UNSET) {
      record->route_press_scan =
          adc_input_trace_scan_offset(record);
    }
    adc_input_trace_increment(&record->route_press_count);
  } else {
    if (record->route_release_scan == ADC_INPUT_TRACE_SCAN_UNSET) {
      record->route_release_scan =
          adc_input_trace_scan_offset(record);
    }
    adc_input_trace_increment(&record->route_release_count);
  }
}

void adc_input_trace_hid_enqueue(adc_input_trace_route_t route, bool accepted) {
  if (!g_trace.active) {
    return;
  }

  for (uint8_t key = 0u; key < NUM_KEYS; key++) {
    uint16_t record_index =
        g_diagnostic_buffer.trace.active_record_index[key];
    adc_input_trace_record_t *record = NULL;
    if (record_index == ADC_INPUT_TRACE_NO_RECORD ||
        record_index >= g_trace.record_count) {
      continue;
    }
    record = &g_diagnostic_buffer.trace.records[record_index];
    if (record->route != (uint8_t)route) {
      continue;
    }

    if (!accepted &&
        (record->route_press_count > record->enqueue_press_count ||
         record->route_release_count > record->enqueue_release_count)) {
      record->flags |= ADC_INPUT_TRACE_FLAG_ENQUEUE_FAILED;
      adc_input_trace_increment(&record->enqueue_failure_count);
      continue;
    }
    if (accepted && record->route_press_count > record->enqueue_press_count) {
      if (record->enqueue_press_scan == ADC_INPUT_TRACE_SCAN_UNSET) {
        record->enqueue_press_scan =
            adc_input_trace_scan_offset(record);
      }
      adc_input_trace_increment(&record->enqueue_press_count);
    }
    if (accepted && record->route_release_count > record->enqueue_release_count) {
      if (record->enqueue_release_scan == ADC_INPUT_TRACE_SCAN_UNSET) {
        record->enqueue_release_scan =
            adc_input_trace_scan_offset(record);
      }
      adc_input_trace_increment(&record->enqueue_release_count);
    }
  }
}
