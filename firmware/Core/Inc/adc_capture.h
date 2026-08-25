#ifndef ADC_CAPTURE_H_
#define ADC_CAPTURE_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Diagnostic capture is deliberately bounded to 64 KiB for both channels. */
#define ADC_CAPTURE_MAX_SAMPLES 16384u
#define ADC_CAPTURE_MAX_READ_SAMPLES 12u

/*
 * Sparse, all-key input trace.
 *
 * The trace is mutually exclusive with the single-key ADC recorder and reuses
 * the same 64 KiB arena.  It therefore adds no large diagnostic allocation to
 * production firmware.  Per-pulse offsets saturate only when one individual
 * excursion remains open for roughly seven seconds; the overall diagnostic
 * window can run long enough to overlap a normal 20-second host capture.
 */
#define ADC_INPUT_TRACE_MAX_DURATION_MS 30000u
#define ADC_INPUT_TRACE_MAX_RECORDS 1500u
#define ADC_INPUT_TRACE_SCAN_UNSET 0xFFFFu
#define ADC_INPUT_TRACE_KEY_UNKNOWN 0xFFu

typedef enum {
  ADC_INPUT_TRACE_ROUTE_NONE = 0u,
  ADC_INPUT_TRACE_ROUTE_6KRO = 1u,
  ADC_INPUT_TRACE_ROUTE_NKRO = 2u,
  ADC_INPUT_TRACE_ROUTE_OTHER = 3u,
} adc_input_trace_route_t;

enum {
  ADC_INPUT_TRACE_FLAG_RAW_STARTED = 1u << 0,
  ADC_INPUT_TRACE_FLAG_FILTERED_STARTED = 1u << 1,
  ADC_INPUT_TRACE_FLAG_SYNTHETIC = 1u << 2,
  ADC_INPUT_TRACE_FLAG_ACTIVE_AT_END = 1u << 3,
  ADC_INPUT_TRACE_FLAG_ENQUEUE_FAILED = 1u << 4,
  ADC_INPUT_TRACE_FLAG_SCAN_SATURATED = 1u << 5,
};

/* One physical sensor excursion plus every downstream stage observed for it.
 * Scan fields other than start_scan are offsets from start_scan; 0xFFFF means
 * the stage was never observed.  Counters expose chatter/repeat storms without
 * allocating one event for every repeated logical edge. */
typedef struct __attribute__((packed)) {
  uint32_t start_scan;
  uint16_t duration_scans;
  uint16_t trigger_press_scan;
  uint16_t trigger_release_scan;
  uint16_t route_press_scan;
  uint16_t route_release_scan;
  uint16_t enqueue_press_scan;
  uint16_t enqueue_release_scan;
  uint16_t raw_baseline;
  uint16_t raw_min;
  uint16_t raw_max;
  uint16_t filtered_baseline;
  uint16_t filtered_max;
  uint16_t distance_max_um;
  uint16_t keycode;
  uint8_t key_index;
  uint8_t route;
  uint8_t trigger_press_count;
  uint8_t trigger_release_count;
  uint8_t route_press_count;
  uint8_t route_release_count;
  uint8_t enqueue_press_count;
  uint8_t enqueue_release_count;
  uint8_t flags;
  uint8_t enqueue_failure_count;
} adc_input_trace_record_t;

_Static_assert(sizeof(adc_input_trace_record_t) == 42u,
               "sparse input trace record wire format changed");

void adc_capture_init(void);
bool adc_capture_start(uint8_t key_index, uint32_t duration_ms);
void adc_capture_cancel(void);

void adc_capture_process_scan(const uint16_t *adc_filtered_values,
                              uint8_t key_count, uint32_t now_ms);

bool adc_capture_is_active(void);
uint8_t adc_capture_key_index(void);
uint32_t adc_capture_duration_ms(void);
uint32_t adc_capture_start_time_ms(void);
uint32_t adc_capture_sample_count(void);
uint32_t adc_capture_overflow_count(void);

uint8_t adc_capture_read_chunk(uint32_t start_index, uint8_t max_samples,
                               uint16_t *raw_out, uint16_t *filtered_out,
                               uint32_t *total_samples_out);

bool adc_input_trace_start(uint32_t duration_ms);
bool adc_input_trace_is_active(void);
uint32_t adc_input_trace_duration_ms(void);
uint32_t adc_input_trace_scan_count(void);
uint16_t adc_input_trace_record_count(void);
uint32_t adc_input_trace_overflow_count(void);
uint32_t adc_input_trace_max_process_cycles(void);
uint32_t adc_input_trace_total_process_cycles(void);
bool adc_input_trace_read(uint16_t record_index,
                          adc_input_trace_record_t *record_out);

/* Called immediately after adc_capture_process_scan() with the measured cost
 * of that call.  A repeated call for the same scan is ignored. */
void adc_input_trace_record_process_cycles(uint32_t cycles);

/* Downstream stage hooks.  These declarations are weak so focused host tests
 * that link trigger/layout/HID modules without adc_capture.c keep their small
 * dependency surface.  Every caller must null-check the symbol. */
void adc_input_trace_trigger(uint8_t key_index, bool pressed)
    __attribute__((weak));
void adc_input_trace_route(uint8_t key_index, uint16_t keycode,
                           adc_input_trace_route_t route, bool pressed)
    __attribute__((weak));
void adc_input_trace_hid_enqueue(adc_input_trace_route_t route, bool accepted)
    __attribute__((weak));

#ifdef __cplusplus
}
#endif

#endif
