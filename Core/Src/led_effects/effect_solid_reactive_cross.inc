static void effect_solid_reactive_cross_math(const led_hsv_t *base,
                                                 led_hsv_t *hsv, int16_t dx,
                                                 int16_t dy, uint8_t dist,
                                                 uint16_t tick) {
  uint16_t effect = (uint16_t)(tick + dist);
  int16_t adx = (dx < 0) ? (int16_t)(-dx) : dx;
  int16_t ady = (dy < 0) ? (int16_t)(-dy) : dy;
  uint16_t dx_scaled;
  uint16_t dy_scaled;

  (void)base;

  dx_scaled = ((uint16_t)adx * 16u > 255u) ? 255u : (uint16_t)adx * 16u;
  dy_scaled = ((uint16_t)ady * 16u > 255u) ? 255u : (uint16_t)ady * 16u;
  effect = (uint16_t)(effect + ((dx_scaled > dy_scaled) ? dy_scaled : dx_scaled));

  if (effect > 255u) {
    effect = 255u;
  }

  hsv->v = mxfx_qadd8(hsv->v, (uint8_t)(255u - effect));
}

static void effect_solid_reactive_cross(void) {
  uint8_t start = (mxfx_last_hit_tracker.count > 0u)
                      ? (uint8_t)(mxfx_last_hit_tracker.count - 1u)
                      : 0u;
  mxfx_reactive_splash_render(start, effect_solid_reactive_cross_math);
}

static void effect_solid_reactive_multi_cross(void) {
  mxfx_reactive_splash_render(0u, effect_solid_reactive_cross_math);
}
