static void effect_solid_reactive_nexus_math(const led_hsv_t *base,
                                                 led_hsv_t *hsv, int16_t dx,
                                                 int16_t dy, uint8_t dist,
                                                 uint16_t tick) {
  uint8_t decay = effect_param(0, 128u);
  uint8_t spread = effect_param(1, 96u);
  uint8_t gain = effect_param(2, 255u);
  uint16_t tick_term =
      (uint16_t)(((uint32_t)tick * (uint16_t)(128u + decay)) >> 8u);
  uint16_t radius_limit =
      (uint16_t)(24u + (((uint32_t)spread * 128u) / 255u));
  int16_t axis_half = (int16_t)(2u + (((uint16_t)spread * 14u) / 255u));
  uint16_t effect = (tick_term > dist) ? (uint16_t)(tick_term - dist) : 255u;
  uint8_t value;

  if (effect > 255u) {
    effect = 255u;
  }
  if (dist > radius_limit) {
    effect = 255u;
  }
  if ((dx > axis_half || dx < -axis_half) &&
      (dy > axis_half || dy < -axis_half)) {
    effect = 255u;
  }

  hsv->h = (uint8_t)(base->h + (dy / 4));
  value = mxfx_scale8((uint8_t)(255u - effect), gain);
  hsv->v = mxfx_qadd8(hsv->v, value);
}

static void effect_solid_reactive_nexus_render(bool default_multi) {
  bool multi = effect_param_flag(3, default_multi);
  uint8_t start = (multi || mxfx_last_hit_tracker.count == 0u)
                      ? 0u
                      : (uint8_t)(mxfx_last_hit_tracker.count - 1u);

  mxfx_reactive_splash_render(start, effect_solid_reactive_nexus_math);
}

static void effect_solid_reactive_nexus(void) {
  effect_solid_reactive_nexus_render(false);
}

static void effect_solid_reactive_multi_nexus(void) {
  effect_solid_reactive_nexus_render(true);
}
