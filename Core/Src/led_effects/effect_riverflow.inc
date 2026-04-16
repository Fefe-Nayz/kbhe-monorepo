static void effect_riverflow(void) {
  led_hsv_t base_hsv = effect_primary_hsv(255u, 0u, 0u);

  for (uint8_t i = 0u; i < LED_MATRIX_NUM_LEDS; i++) {
    uint8_t time = (uint8_t)mxfx_scale16by8(
        (uint16_t)(mxfx_timer16() + (uint16_t)(i * 315u)),
        (uint8_t)(effect_animation_speed() / 8u));
    uint8_t r = 0u;
    uint8_t g = 0u;
    uint8_t b = 0u;
    led_hsv_t hsv = base_hsv;

    hsv.v = mxfx_scale8((uint8_t)(abs_u8_diff(mxfx_sin8(time), 128u) * 2u),
                       hsv.v);
    led_matrix_hsv_to_rgb(hsv.h, hsv.s, hsv.v, &r, &g, &b);
    led_matrix_set_pixel_idx(i, r, g, b);
  }
}
