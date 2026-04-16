static void effect_gradient_left_right(void) {
  // x_mul: 1..5 horizontal hue spread factor. h_scale=160 -> x_mul ~= 3.5
  uint8_t x_mul = (uint8_t)(1u + ((uint16_t)effect_param(0, 160u) * 4u) / 255u);
  // y_div: 1..6 vertical hue divisor. v_scale=120 -> y_div ~= 3.4
  uint8_t y_div = (uint8_t)(1u + ((uint16_t)effect_param(1, 120u) * 5u) / 255u);
  uint8_t sat   = effect_param(2, 144u);
  uint8_t val   = effect_param(3, 255u);
  led_hsv_t base_hsv = effect_primary_hsv(255u, 0u, 0u);

  for (uint8_t i = 0u; i < LED_MATRIX_NUM_LEDS; i++) {
    uint8_t hue = (uint8_t)(base_hsv.h +
                            mxfx_led_coord_x(i) * x_mul +
                            mxfx_led_coord_y(i) / y_div);
    uint8_t r = 0u;
    uint8_t g = 0u;
    uint8_t b = 0u;
    led_matrix_hsv_to_rgb(hue, sat, val, &r, &g, &b);
    led_matrix_set_pixel_idx(i, r, g, b);
  }
}
