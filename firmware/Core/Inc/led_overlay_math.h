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

static inline uint8_t led_overlay_add_channel(uint8_t base, uint8_t overlay,
                                              uint8_t alpha) {
  uint16_t sum =
      (uint16_t)base + led_overlay_scale_u8(overlay, alpha);
  return sum > 255u ? 255u : (uint8_t)sum;
}

/* Replace differs from alpha composition: opacity controls the intensity of
 * the replacement colour, while transition_alpha cross-fades from the
 * existing effect to that colour. At the end of the transition no underlying
 * effect contribution remains. */
static inline uint8_t led_overlay_replace_channel(uint8_t base,
                                                  uint8_t overlay,
                                                  uint8_t opacity,
                                                  uint8_t transition_alpha) {
  uint8_t target = led_overlay_scale_u8(overlay, opacity);
  return led_overlay_blend_channel(base, target, transition_alpha);
}

/* Interpolate an 8-bit overlay alpha without overshooting either endpoint.
 * The elapsed value is deliberately 32-bit for HAL tick arithmetic, while
 * durations remain bounded to uint16_t by the overlay protocol. */
static inline uint8_t led_overlay_transition_u8(uint8_t from, uint8_t to,
                                                uint32_t elapsed,
                                                uint16_t duration) {
  int32_t delta = (int32_t)to - (int32_t)from;

  if (duration == 0u || elapsed >= (uint32_t)duration) {
    return to;
  }
  return (uint8_t)((int32_t)from +
                   ((delta * (int32_t)elapsed) / (int32_t)duration));
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
