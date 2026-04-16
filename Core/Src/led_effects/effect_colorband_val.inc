static void effect_colorband_val(void) {
  led_hsv_t base_hsv = effect_primary_hsv(255u, 0u, 0u);
  uint8_t time = mxfx_runner_time_i();

  for (uint8_t i = 0u; i < LED_MATRIX_NUM_LEDS; i++) {
    uint8_t x = mxfx_led_coord_x(i);
    uint8_t r = 0u;
    uint8_t g = 0u;
    uint8_t b = 0u;
    int16_t val = (int16_t)base_hsv.v -
                  (int16_t)abs_u8_diff((uint8_t)(mxfx_scale8(x, 228u) + 28u),
                                       time) *
                      8;
    led_hsv_t hsv = base_hsv;

    hsv.v = mxfx_scale8((uint8_t)((val < 0) ? 0 : val), base_hsv.v);
    led_matrix_hsv_to_rgb(hsv.h, hsv.s, hsv.v, &r, &g, &b);
    led_matrix_set_pixel_idx(i, r, g, b);
  }
}
