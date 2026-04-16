static void effect_solid_reactive_nexus_math(const led_hsv_t *base,
                                                 led_hsv_t *hsv, int16_t dx,
                                                 int16_t dy, uint8_t dist,
                                                 uint16_t tick) {
  uint16_t effect = (uint16_t)(tick - dist);

  if (effect > 255u) {
    effect = 255u;
  }
  if (dist > 72u) {
    effect = 255u;
  }
  if ((dx > 8 || dx < -8) && (dy > 8 || dy < -8)) {
    effect = 255u;
  }

  hsv->h = (uint8_t)(base->h + (dy / 4));
  hsv->v = mxfx_qadd8(hsv->v, (uint8_t)(255u - effect));
}

static void effect_solid_reactive_nexus(void) {
  uint8_t start = (mxfx_last_hit_tracker.count > 0u)
                      ? (uint8_t)(mxfx_last_hit_tracker.count - 1u)
                      : 0u;
  mxfx_reactive_splash_render(start, effect_solid_reactive_nexus_math);
}

static void effect_solid_reactive_multi_nexus(void) {
  mxfx_reactive_splash_render(0u, effect_solid_reactive_nexus_math);
}
