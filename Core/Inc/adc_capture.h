#ifndef ADC_CAPTURE_H_
#define ADC_CAPTURE_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ADC_CAPTURE_MAX_SAMPLES 30000u
#define ADC_CAPTURE_MAX_READ_SAMPLES 12u

void adc_capture_init(void);
bool adc_capture_start(uint8_t key_index, uint32_t duration_ms);
void adc_capture_cancel(void);

void adc_capture_process_scan(const uint32_t *adc_raw_values,
                              const uint16_t *adc_filtered_values,
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

#ifdef __cplusplus
}
#endif

#endif
