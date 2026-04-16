static void effect_hue_breathing(void) {
  uint8_t swing   = effect_param(0, 12u);   // Hue swing range (12=original, 64=wide)
  led_hsv_t base_hsv = effect_primary_hsv(255u, 0u, 0u);
  uint8_t time = mxfx_runner_time_i();

  for (uint8_t i = 0u; i < LED_MATRIX_NUM_LEDS; i++) {
    uint8_t r = 0u;
    uint8_t g = 0u;
    uint8_t b = 0u;
    led_hsv_t hsv = base_hsv;

    hsv.h = (uint8_t)(hsv.h +
                      mxfx_scale8((uint8_t)(abs_u8_diff(
                                     mxfx_sin8((uint8_t)(time / 2u)), 128u) *
                                 2u),
                                 swing));
    led_matrix_hsv_to_rgb(hsv.h, hsv.s, hsv.v, &r, &g, &b);
    led_matrix_set_pixel_idx(i, r, g, b);
  }
}
