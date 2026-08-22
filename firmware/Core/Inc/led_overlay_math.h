#ifndef LED_OVERLAY_MATH_H_
#define LED_OVERLAY_MATH_H_

#include <stdbool.h>
#include <stdint.h>

static inline uint8_t led_overlay_scale_u8(uint8_t value, uint8_t scale) {
  return (uint8_t)(((uint16_t)value * scale) / 255u);
}

static inline uint8_t led_overlay_blend_channel(uint8_t base,
                                                uint8_t overlay,
                                                uint8_t alpha) {
  uint16_t inverse = (uint16_t)(255u - alpha);
  return (uint8_t)((((uint16_t)base * inverse) +
                    ((uint16_t)overlay * alpha) + 127u) /
                   255u);
}

/* Prepare a progress-bar pixel for composition. In filled-only mode the
 * unfilled region has no overlay contribution at all and a fractional edge
 * scales opacity, preserving the effect underneath. The classic mode instead
 * scales RGB towards black, retaining its shaded track. */
static inline bool led_overlay_prepare_progress_pixel(
    bool filled_only, uint8_t fill_scale, uint8_t *r, uint8_t *g, uint8_t *b,
    uint8_t *alpha) {
  if (r == 0 || g == 0 || b == 0 || alpha == 0 || *alpha == 0u) {
    return false;
  }

  if (filled_only) {
    if (fill_scale == 0u) {
      return false;
    }
    *alpha = led_overlay_scale_u8(*alpha, fill_scale);
  } else {
    *r = led_overlay_scale_u8(*r, fill_scale);
    *g = led_overlay_scale_u8(*g, fill_scale);
    *b = led_overlay_scale_u8(*b, fill_scale);
  }

  return *alpha != 0u;
}

#endif /* LED_OVERLAY_MATH_H_ */
