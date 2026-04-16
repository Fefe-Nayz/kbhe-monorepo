static void effect_cycle_all(void) {
  led_hsv_t hsv = effect_primary_hsv(255u, 0u, 0u);
  uint8_t time = mxfx_runner_time_i();
  uint8_t r = 0u;
  uint8_t g = 0u;
  uint8_t b = 0u;

  hsv.h = time;
  led_matrix_hsv_to_rgb(hsv.h, hsv.s, hsv.v, &r, &g, &b);

  for (uint8_t i = 0u; i < LED_MATRIX_NUM_LEDS; i++) {
    led_matrix_set_pixel_idx(i, r, g, b);
  }
}
