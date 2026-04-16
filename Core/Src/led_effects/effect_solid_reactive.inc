static void effect_solid_reactive_math(const led_hsv_t *base,
                                           led_hsv_t *hsv,
                                           uint16_t offset) {
  if (offset > 255u) {
    offset = 255u;
  }

  *hsv = *base;
  hsv->h = (uint8_t)(base->h + mxfx_scale8((uint8_t)(255u - offset), 64u));
}

static void effect_solid_reactive(void) {
  mxfx_reactive_render(effect_solid_reactive_math);
}
