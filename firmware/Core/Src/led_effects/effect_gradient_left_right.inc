static void effect_gradient_left_right(void) {
  uint8_t h_scale = effect_param(0, 224u);   // Horizontal hue spread
  uint8_t angle = effect_param(1, 0u);       // Gradient axis angle
  led_hsv_t base_hsv = effect_primary_hsv(255u, 0u, 0u);
  int16_t dir_x = (int16_t)mxfx_cos8(angle) - 128;
  int16_t dir_y = (int16_t)mxfx_sin8(angle) - 128;

  for (uint8_t i = 0u; i < LED_MATRIX_NUM_LEDS; i++) {
    int16_t x = (int16_t)mxfx_led_coord_x(i) - (int16_t)mxfx_CENTER_X;
    int16_t y = (int16_t)mxfx_led_coord_y(i) - (int16_t)mxfx_CENTER_Y;
    uint8_t axis = mxfx_project_axis_u8(x, y, dir_x, dir_y);
    uint8_t hue =
        (uint8_t)(base_hsv.h + (uint8_t)(((uint16_t)axis * h_scale) >> 8u));
    uint8_t r = 0u;
    uint8_t g = 0u;
    uint8_t b = 0u;
    led_matrix_hsv_to_rgb(hue, base_hsv.s, base_hsv.v, &r, &g, &b);
    led_matrix_set_pixel_idx(i, r, g, b);
  }
}
