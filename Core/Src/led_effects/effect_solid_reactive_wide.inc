static void effect_solid_reactive_wide_math(const led_hsv_t *base,
                                                led_hsv_t *hsv, int16_t dx,
                                                int16_t dy, uint8_t dist,
                                                uint16_t tick) {
  uint16_t effect = (uint16_t)(tick + (uint16_t)dist * 5u);
  (void)base;
  (void)dx;
  (void)dy;

  if (effect > 255u) {
    effect = 255u;
  }

  hsv->v = mxfx_qadd8(hsv->v, (uint8_t)(255u - effect));
}

static void effect_solid_reactive_wide(void) {
  uint8_t start = (mxfx_last_hit_tracker.count > 0u)
                      ? (uint8_t)(mxfx_last_hit_tracker.count - 1u)
                      : 0u;
  mxfx_reactive_splash_render(start, effect_solid_reactive_wide_math);
}

static void effect_solid_reactive_multi_wide(void) {
  mxfx_reactive_splash_render(0u, effect_solid_reactive_wide_math);
}
