#include "adc_ema.h"

#include <stddef.h>

static inline uint16_t u16_abs_diff(uint16_t a, uint16_t b) {
  return (a >= b) ? (uint16_t)(a - b) : (uint16_t)(b - a);
}

static inline uint16_t adc_ema_alpha_for_error(const adc_ema_t *s,
                                               uint16_t error) {
  // Guard rails
  if (s->alpha_max_q15 < s->alpha_min_q15) {
    return s->alpha_min_q15;
  }

  const uint16_t band = s->noise_band;
  if (band == 0u) {
    return s->alpha_max_q15;
  }

  if (error <= band) {
    return s->alpha_min_q15;
  }

  // Requirement: no filtering when variation is >= 35 ADC counts.
  if (error >= 35u) {
    return ADC_EMA_Q15_ONE;
  }

  // Scale up to alpha_max when error reaches ~8x band.
  // This keeps the filter very stable inside ±band, but reacts quickly to real steps.
  const uint32_t error_hi = (uint32_t)band * 8u;
  if ((uint32_t)error >= error_hi) {
    return s->alpha_max_q15;
  }

  const uint32_t span = error_hi - band; // == 7*band
  const uint32_t t_num = (uint32_t)(error - band); // 0..span
  const uint32_t alpha_span = (uint32_t)(s->alpha_max_q15 - s->alpha_min_q15);

  const uint32_t alpha = (uint32_t)s->alpha_min_q15 +
                         (alpha_span * t_num + (span / 2u)) / span;

  return (uint16_t)alpha;
}

void adc_ema_init(adc_ema_t *s, uint16_t noise_band, uint16_t alpha_min_q15,
                  uint16_t alpha_max_q15) {
  if (s == NULL) {
    return;
  }

  s->y_q16 = 0u;
  s->noise_band = noise_band;
  s->alpha_min_q15 = alpha_min_q15;
  s->alpha_max_q15 = alpha_max_q15;
  s->initialized = 0u;
}

void adc_ema_reset(adc_ema_t *s, uint16_t sample) {
  if (s == NULL) {
    return;
  }

  s->y_q16 = ((uint32_t)sample) << 16;
  s->initialized = 1u;
}

uint16_t adc_ema_value(const adc_ema_t *s) {
  if (s == NULL) {
    return 0u;
  }

  return (uint16_t)(s->y_q16 >> 16);
}

uint16_t adc_ema_update(adc_ema_t *s, uint16_t sample) {
  if (s == NULL) {
    return sample;
  }

  if (!s->initialized) {
    adc_ema_reset(s, sample);
    return sample;
  }

  const uint16_t y = (uint16_t)(s->y_q16 >> 16);
  const uint16_t error = u16_abs_diff(sample, y);
  const uint16_t alpha_q15 = adc_ema_alpha_for_error(s, error);

  const int64_t x_q16 = ((int64_t)sample) << 16;
  const int64_t y_q16 = (int64_t)s->y_q16;
  const int64_t e_q16 = x_q16 - y_q16;

  // y += alpha * (x - y)
  const int64_t delta = ((int64_t)alpha_q15 * e_q16) >> 15;
  const int64_t y_next = y_q16 + delta;

  // Clamp to valid Q16.16 range for uint16 extraction
  uint32_t y_next_u32;
  if (y_next < 0) {
    y_next_u32 = 0u;
  } else if (y_next > (int64_t)(0xFFFFu << 16)) {
    y_next_u32 = (0xFFFFu << 16);
  } else {
    y_next_u32 = (uint32_t)y_next;
  }

  s->y_q16 = y_next_u32;
  return (uint16_t)(y_next_u32 >> 16);
}
