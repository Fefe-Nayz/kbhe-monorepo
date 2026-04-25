#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Exponential Moving Average (EMA) with adaptive alpha.
//
// Update equation:
//   y[n] = y[n-1] + alpha * (x[n] - y[n-1])
//
// alpha is adapted based on the instantaneous error |x - y|:
// - Within noise_band (e.g. ±30 ADC counts): alpha = alpha_min (strong smoothing)
// - Far outside noise_band: alpha approaches alpha_max (fast response)
// - Between: linear interpolation

typedef struct {
  uint32_t y_q16;           // filtered value in Q16.16
  uint16_t noise_band;      // dead-band around the current value (ADC counts)
  uint16_t alpha_min_q15;   // minimum alpha in Q0.15 (0..32768)
  uint16_t alpha_max_q15;   // maximum alpha in Q0.15 (0..32768)
  uint8_t initialized;
} adc_ema_t;

// Convenience constants for Q15
#define ADC_EMA_Q15_ONE (32768u)

// Compute alpha in Q15 from a rational (numerator/denominator)
// Example: ADC_EMA_Q15_FROM_RATIO(1, 8) == 0.125 in Q15.
#define ADC_EMA_Q15_FROM_RATIO(num, den) ((uint16_t)((((uint32_t)(num) << 15) + ((den) / 2u)) / (uint32_t)(den)))

void adc_ema_init(adc_ema_t *s, uint16_t noise_band, uint16_t alpha_min_q15,
                  uint16_t alpha_max_q15);

void adc_ema_reset(adc_ema_t *s, uint16_t sample);

uint16_t adc_ema_update(adc_ema_t *s, uint16_t sample);

uint16_t adc_ema_value(const adc_ema_t *s);

#ifdef __cplusplus
}
#endif
