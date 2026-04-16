static void effect_solid_reactive_simple_math(const led_hsv_t *base,
                                                  led_hsv_t *hsv,
                                                  uint16_t offset) {
  if (offset > 255u) {
    offset = 255u;
  }

  *hsv = *base;
  hsv->v = mxfx_scale8((uint8_t)(255u - offset), base->v);
}

static void effect_solid_reactive_simple(void) {
  mxfx_reactive_render(effect_solid_reactive_simple_math);
}
