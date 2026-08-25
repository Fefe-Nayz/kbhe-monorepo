#ifndef KBHE_ANALOG_INTERNAL_SENSOR_SAMPLER_H_
#define KBHE_ANALOG_INTERNAL_SENSOR_SAMPLER_H_

#include <stdbool.h>
#include <stdint.h>

#include "stm32f7xx_hal.h"

#define ADC_INTERNAL_SENSOR_TIMEOUT_MS 4u

typedef struct {
  uint32_t started_ms;
  bool pending;
} adc_internal_sensor_sampler_t;

typedef struct {
  uint32_t vref_raw;
  uint32_t temperature_raw;
} adc_internal_sensor_values_t;

typedef enum {
  ADC_INTERNAL_SENSOR_IDLE = 0,
  ADC_INTERNAL_SENSOR_PENDING,
  ADC_INTERNAL_SENSOR_READY,
  ADC_INTERNAL_SENSOR_RECOVERY_REQUIRED,
} adc_internal_sensor_result_t;

void adc_internal_sensor_sampler_init(adc_internal_sensor_sampler_t *sampler);

bool adc_internal_sensor_sampler_start(adc_internal_sensor_sampler_t *sampler,
                                       ADC_HandleTypeDef *hadc,
                                       uint32_t now_ms);

adc_internal_sensor_result_t adc_internal_sensor_sampler_poll(
    adc_internal_sensor_sampler_t *sampler, ADC_HandleTypeDef *hadc,
    uint32_t now_ms, adc_internal_sensor_values_t *values);

void adc_internal_sensor_sampler_abort(adc_internal_sensor_sampler_t *sampler,
                                       ADC_HandleTypeDef *hadc);

#endif /* KBHE_ANALOG_INTERNAL_SENSOR_SAMPLER_H_ */
