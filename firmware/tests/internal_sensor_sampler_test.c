#include "analog/internal_sensor_sampler.h"
#include "analog/scan_watchdog.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

/* Exact relevant behavior of the vendor HAL routines previously used by the
 * application. HAL_ADCEx_InjectedPollForConversion(..., 0) assigns TIMEOUT,
 * rather than OR-ing it, when JEOC is not set. InjectedStop then trusts that
 * clobbered software state and may disable an ADC still owned by regular DMA. */
static void legacy_zero_timeout_poll(ADC_HandleTypeDef *hadc) {
  if (!__HAL_ADC_GET_FLAG(hadc, ADC_FLAG_JEOC)) {
    hadc->State = HAL_ADC_STATE_TIMEOUT;
  }
}

static void legacy_injected_stop(ADC_HandleTypeDef *hadc) {
  if ((hadc->State & HAL_ADC_STATE_REG_BUSY) == 0u) {
    hadc->Instance->CR2 &= ~ADC_CR2_ADON;
  }
}

static void test_legacy_false_timeout_disables_regular_dma_owner(void) {
  ADC_TypeDef instance = {.CR2 = ADC_CR2_ADON};
  DMA_HandleTypeDef dma = {.ownership_cookie = 0xD4A0u};
  ADC_HandleTypeDef hadc = {
      .Instance = &instance,
      .State = HAL_ADC_STATE_REG_BUSY,
      .DMA_Handle = &dma,
  };

  legacy_zero_timeout_poll(&hadc);
  legacy_injected_stop(&hadc);

  assert((hadc.State & HAL_ADC_STATE_REG_BUSY) == 0u);
  assert((instance.CR2 & ADC_CR2_ADON) == 0u);
  assert(hadc.DMA_Handle == &dma);
  assert(dma.ownership_cookie == 0xD4A0u);
}

static void test_safe_pending_poll_preserves_adc_and_dma_ownership(void) {
  adc_internal_sensor_sampler_t sampler;
  adc_internal_sensor_values_t values = {0};
  ADC_TypeDef instance = {.CR2 = ADC_CR2_ADON};
  DMA_HandleTypeDef dma = {.ownership_cookie = 0xD4A0u};
  ADC_HandleTypeDef hadc = {
      .Instance = &instance,
      .State = HAL_ADC_STATE_REG_BUSY | 0x40000000u,
      .DMA_Handle = &dma,
  };
  uint32_t original_state = hadc.State;

  adc_internal_sensor_sampler_init(&sampler);
  assert(adc_internal_sensor_sampler_start(&sampler, &hadc, 100u));
  assert((instance.CR2 & (ADC_CR2_ADON | ADC_CR2_JSWSTART)) ==
         (ADC_CR2_ADON | ADC_CR2_JSWSTART));

  /* This is the ordinary sub-40-us state that the old zero-timeout HAL poll
   * treated as an error. It must remain a side-effect-free pending result. */
  assert(adc_internal_sensor_sampler_poll(&sampler, &hadc, 100u, &values) ==
         ADC_INTERNAL_SENSOR_PENDING);
  assert(hadc.State == original_state);
  assert(hadc.DMA_Handle == &dma);
  assert(dma.ownership_cookie == 0xD4A0u);
  assert((instance.CR2 & ADC_CR2_ADON) != 0u);

  instance.JDR1 = 1500u;
  instance.JDR2 = 1700u;
  instance.SR |= ADC_FLAG_JSTRT | ADC_FLAG_JEOC;
  assert(adc_internal_sensor_sampler_poll(&sampler, &hadc, 101u, &values) ==
         ADC_INTERNAL_SENSOR_READY);
  assert(values.vref_raw == 1500u);
  assert(values.temperature_raw == 1700u);
  assert(hadc.State == original_state);
  assert(hadc.DMA_Handle == &dma);
  assert(dma.ownership_cookie == 0xD4A0u);
  assert((instance.CR2 & ADC_CR2_ADON) != 0u);
  assert((instance.SR & (ADC_FLAG_JSTRT | ADC_FLAG_JEOC)) == 0u);
}

static void test_true_injected_timeout_requests_full_recovery(void) {
  adc_internal_sensor_sampler_t sampler;
  ADC_TypeDef instance = {.CR2 = ADC_CR2_ADON};
  DMA_HandleTypeDef dma = {.ownership_cookie = 0xD4A0u};
  ADC_HandleTypeDef hadc = {
      .Instance = &instance,
      .State = HAL_ADC_STATE_REG_BUSY,
      .DMA_Handle = &dma,
  };

  adc_internal_sensor_sampler_init(&sampler);
  assert(adc_internal_sensor_sampler_start(&sampler, &hadc, UINT32_MAX - 1u));
  assert(adc_internal_sensor_sampler_poll(&sampler, &hadc, 1u, NULL) ==
         ADC_INTERNAL_SENSOR_PENDING);
  assert(adc_internal_sensor_sampler_poll(&sampler, &hadc, 2u, NULL) ==
         ADC_INTERNAL_SENSOR_RECOVERY_REQUIRED);
  assert(hadc.State == HAL_ADC_STATE_REG_BUSY);
  assert(hadc.DMA_Handle == &dma);
  assert((instance.CR2 & ADC_CR2_ADON) != 0u);
}

static void test_scan_watchdog_has_hil_margin_without_hiding_taps(void) {
  /* All-key Raw HID diagnostics, settings traffic and RGB produced a measured
   * worst scan of 326 us. HAL_GetTick therefore cannot see even one elapsed
   * millisecond for that load, and the 2 ms policy cannot falsely recover. */
  const uint32_t hil_worst_scan_us = 326u;
  assert(hil_worst_scan_us < ADC_SCAN_WATCHDOG_TIMEOUT_MS * 1000u);
  assert(!adc_scan_watchdog_should_recover(false, false, 100u, 100u));
  assert(!adc_scan_watchdog_should_recover(false, false, 101u, 100u));
  assert(adc_scan_watchdog_should_recover(false, false, 102u, 100u));
  assert(!adc_scan_watchdog_should_recover(false, true, 500u, 100u));
  assert(adc_scan_watchdog_should_recover(true, true, 100u, 100u));
  assert(adc_scan_watchdog_should_recover(false, false, 0u,
                                          UINT32_MAX - 1u));
}

int main(void) {
  test_legacy_false_timeout_disables_regular_dma_owner();
  test_safe_pending_poll_preserves_adc_and_dma_ownership();
  test_true_injected_timeout_requests_full_recovery();
  test_scan_watchdog_has_hil_margin_without_hiding_taps();
  puts("internal_sensor_sampler_test: OK");
  return 0;
}
