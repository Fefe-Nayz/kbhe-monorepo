static void effect_rainbow_beacon(void) {
  led_hsv_t base_hsv = effect_primary_hsv(255u, 0u, 0u);
  uint8_t time = mxfx_runner_time_sin_cos();
  int8_t cos_value = (int8_t)mxfx_cos8(time) - 128;
  int8_t sin_value = (int8_t)mxfx_sin8(time) - 128;

  for (uint8_t i = 0u; i < LED_MATRIX_NUM_LEDS; i++) {
    int16_t x = (int16_t)mxfx_led_coord_x(i);
    int16_t y = (int16_t)mxfx_led_coord_y(i);
    uint8_t r = 0u;
    uint8_t g = 0u;
    uint8_t b = 0u;
    led_hsv_t hsv = base_hsv;

    hsv.h = (uint8_t)(hsv.h +
                      (((y - (int16_t)mxfx_CENTER_Y) * 2 * cos_value +
                        (x - (int16_t)mxfx_CENTER_X) * 2 * sin_value) /
                       128));
    led_matrix_hsv_to_rgb(hsv.h, hsv.s, hsv.v, &r, &g, &b);
    led_matrix_set_pixel_idx(i, r, g, b);
  }
}
