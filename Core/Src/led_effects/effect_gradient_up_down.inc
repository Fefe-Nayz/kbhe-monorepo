static void effect_gradient_up_down(void) {
  uint8_t v_scale = effect_param(0, 160u);  // Vertical hue spread (160=original ~25-unit sweep)
  uint8_t sat     = effect_param(1, 144u);  // Saturation
  uint8_t val     = effect_param(2, 255u);  // Value / brightness
  led_hsv_t base_hsv = effect_primary_hsv(255u, 0u, 0u);

  for (uint8_t i = 0u; i < LED_MATRIX_NUM_LEDS; i++) {
    uint8_t y = mxfx_led_coord_y(i);
    uint8_t r = 0u;
    uint8_t g = 0u;
    uint8_t b = 0u;
    uint8_t hue = (uint8_t)(base_hsv.h + (uint8_t)(((uint16_t)y * v_scale) >> 8u));

    led_matrix_hsv_to_rgb(hue, sat, val, &r, &g, &b);
    led_matrix_set_pixel_idx(i, r, g, b);
  }
}
