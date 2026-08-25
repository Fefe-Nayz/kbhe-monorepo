#include "analog/internal_sensor_sampler.h"

#include <stddef.h>

static void adc_internal_sensor_clear_flags(ADC_HandleTypeDef *hadc) {
  __HAL_ADC_CLEAR_FLAG(hadc, ADC_FLAG_JSTRT | ADC_FLAG_JEOC);
}

void adc_internal_sensor_sampler_init(adc_internal_sensor_sampler_t *sampler) {
  if (sampler == NULL) {
    return;
  }

  sampler->started_ms = 0u;
  sampler->pending = false;
}

bool adc_internal_sensor_sampler_start(adc_internal_sensor_sampler_t *sampler,
                                       ADC_HandleTypeDef *hadc,
                                       uint32_t now_ms) {
  if (sampler == NULL || hadc == NULL || hadc->Instance == NULL ||
      sampler->pending ||
      !HAL_IS_BIT_SET(hadc->Instance->CR2, ADC_CR2_ADON)) {
    return false;
  }

  /* ADC1 is continuously owned by the regular keyboard DMA scanner. Starting
   * the already-configured injected sequence at the register boundary leaves
   * hadc->State and hadc->DMA_Handle untouched. In particular, do not use a
   * zero-timeout HAL poll: its ordinary "not ready yet" path overwrites the
   * complete shared-handle state with HAL_ADC_STATE_TIMEOUT. */
  adc_internal_sensor_clear_flags(hadc);
  SET_BIT(hadc->Instance->CR2, ADC_CR2_JSWSTART);
  sampler->started_ms = now_ms;
  sampler->pending = true;
  return true;
}

adc_internal_sensor_result_t adc_internal_sensor_sampler_poll(
    adc_internal_sensor_sampler_t *sampler, ADC_HandleTypeDef *hadc,
    uint32_t now_ms, adc_internal_sensor_values_t *values) {
  if (sampler == NULL || hadc == NULL || hadc->Instance == NULL) {
    return ADC_INTERNAL_SENSOR_RECOVERY_REQUIRED;
  }
  if (!sampler->pending) {
    return ADC_INTERNAL_SENSOR_IDLE;
  }

  if (__HAL_ADC_GET_FLAG(hadc, ADC_FLAG_JEOC)) {
    if (values != NULL) {
      values->vref_raw = hadc->Instance->JDR1;
      values->temperature_raw = hadc->Instance->JDR2;
    }
    adc_internal_sensor_clear_flags(hadc);
    sampler->pending = false;
    return ADC_INTERNAL_SENSOR_READY;
  }

  if ((uint32_t)(now_ms - sampler->started_ms) >=
      ADC_INTERNAL_SENSOR_TIMEOUT_MS) {
    sampler->pending = false;
    return ADC_INTERNAL_SENSOR_RECOVERY_REQUIRED;
  }

  return ADC_INTERNAL_SENSOR_PENDING;
}

void adc_internal_sensor_sampler_abort(adc_internal_sensor_sampler_t *sampler,
                                       ADC_HandleTypeDef *hadc) {
  if (sampler != NULL) {
    sampler->pending = false;
  }
  if (hadc != NULL && hadc->Instance != NULL) {
    adc_internal_sensor_clear_flags(hadc);
  }
}
