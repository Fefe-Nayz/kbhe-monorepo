static void effect_starlight_smooth(void) {
  static uint8_t phase_offsets[LED_MATRIX_NUM_LEDS] = {0};
  led_hsv_t base_hsv = effect_primary_hsv(255u, 0u, 0u);
  uint8_t time = mxfx_runner_time_i();

  if (effect_offset == 0u) {
    memset(phase_offsets, 0, sizeof(phase_offsets));
  }

  for (uint8_t i = 0u; i < LED_MATRIX_NUM_LEDS; i++) {
    uint8_t r = 0u;
    uint8_t g = 0u;
    uint8_t b = 0u;
    led_hsv_t hsv = base_hsv;

    if (phase_offsets[i] == 0u) {
      phase_offsets[i] = mxfx_random8();
    }

    hsv.v = mxfx_scale8(
        (uint8_t)(abs_u8_diff(mxfx_sin8((uint8_t)((time + phase_offsets[i]) / 2u)),
                              128u) *
                  2u),
        hsv.v);
    led_matrix_hsv_to_rgb(hsv.h, hsv.s, hsv.v, &r, &g, &b);
    led_matrix_set_pixel_idx(i, r, g, b);
  }
}
