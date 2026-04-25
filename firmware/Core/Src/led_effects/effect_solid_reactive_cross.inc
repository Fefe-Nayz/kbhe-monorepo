static void effect_solid_reactive_cross_math(const led_hsv_t *base,
                                                 led_hsv_t *hsv, int16_t dx,
                                                 int16_t dy, uint8_t dist,
                                                 uint16_t tick) {
  uint8_t decay = effect_param(0, 128u);
  uint8_t spread = effect_param(1, 128u);
  uint8_t gain = effect_param(2, 255u);
  uint16_t tick_term =
      (uint16_t)(((uint32_t)tick * (uint16_t)(128u + decay)) >> 8u);
  uint16_t radial_scale = (uint16_t)(1u + (((uint16_t)spread * 3u) / 255u));
  uint16_t effect = (uint16_t)(tick_term + (uint16_t)dist * radial_scale);
  int16_t adx = (dx < 0) ? (int16_t)(-dx) : dx;
  int16_t ady = (dy < 0) ? (int16_t)(-dy) : dy;
  uint16_t axis_scale =
      (uint16_t)(6u + (((uint16_t)(255u - spread) * 18u) / 255u));
  uint16_t axis_term;
  uint8_t value;

  (void)base;

  axis_term =
      ((uint16_t)((adx > ady) ? ady : adx) * axis_scale > 255u)
          ? 255u
          : (uint16_t)((adx > ady) ? ady : adx) * axis_scale;
  effect = (uint16_t)(effect + axis_term);

  if (effect > 255u) {
    effect = 255u;
  }

  value = mxfx_scale8((uint8_t)(255u - effect), gain);
  hsv->v = mxfx_qadd8(hsv->v, value);
}

static void effect_solid_reactive_cross_render(bool default_multi) {
  bool multi = effect_param_flag(3, default_multi);
  uint8_t start = (multi || mxfx_last_hit_tracker.count == 0u)
                      ? 0u
                      : (uint8_t)(mxfx_last_hit_tracker.count - 1u);

  mxfx_reactive_splash_render(start, effect_solid_reactive_cross_math);
}

static void effect_solid_reactive_cross(void) {
  effect_solid_reactive_cross_render(false);
}

static void effect_solid_reactive_multi_cross(void) {
  effect_solid_reactive_cross_render(true);
}
