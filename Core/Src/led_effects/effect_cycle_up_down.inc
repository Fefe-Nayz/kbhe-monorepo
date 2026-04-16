static void effect_cycle_up_down(void) {
  uint8_t spread = effect_param(0, 192u);   // Hue spread along projection axis
  uint8_t angle = effect_param(1, 64u);     // 64 ~= vertical default
  led_hsv_t base_hsv = effect_primary_hsv(255u, 0u, 0u);
  uint8_t time = mxfx_runner_time_i();

  int16_t dir_x = (int16_t)mxfx_cos8(angle) - 128;
  int16_t dir_y = (int16_t)mxfx_sin8(angle) - 128;

  for (uint8_t i = 0u; i < LED_MATRIX_NUM_LEDS; i++) {
    int16_t x = (int16_t)mxfx_led_coord_x(i) - (int16_t)mxfx_CENTER_X;
    int16_t y = (int16_t)mxfx_led_coord_y(i) - (int16_t)mxfx_CENTER_Y;
    int16_t projection = (int16_t)(((int32_t)x * dir_x + (int32_t)y * dir_y) >> 7);
    int16_t axis = projection + 128;
    if (axis < 0) {
      axis = 0;
    } else if (axis > 255) {
      axis = 255;
    }

    uint8_t r = 0u;
    uint8_t g = 0u;
    uint8_t b = 0u;
    led_hsv_t hsv = base_hsv;

    hsv.h = (uint8_t)(hsv.h +
                      (uint8_t)(((uint16_t)(uint8_t)axis * spread) >> 8u) -
                      time);
    led_matrix_hsv_to_rgb(hsv.h, hsv.s, hsv.v, &r, &g, &b);
    led_matrix_set_pixel_idx(i, r, g, b);
  }
}
