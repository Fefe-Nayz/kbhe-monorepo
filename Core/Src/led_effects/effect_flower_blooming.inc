static void effect_flower_blooming(void) {
  led_hsv_t base_hsv = effect_primary_hsv(255u, 0u, 0u);
  uint8_t time = (uint8_t)mxfx_scale16by8(
      mxfx_timer16(), mxfx_qadd8((uint8_t)(effect_animation_speed() / 10u), 1u));

  for (uint8_t i = 0u; i < LED_MATRIX_NUM_LEDS; i++) {
    uint8_t x = mxfx_led_coord_x(i);
    uint8_t y = mxfx_led_coord_y(i);
    uint8_t r = 0u;
    uint8_t g = 0u;
    uint8_t b = 0u;
    led_hsv_t hsv = base_hsv;

    if (y > mxfx_CENTER_Y) {
      hsv.h = (uint8_t)(x * 3u - y * 3u + time);
      led_matrix_hsv_to_rgb(hsv.h, hsv.s, hsv.v, &b, &g, &r);
    } else {
      hsv.h = (uint8_t)(x * 3u - y * 3u - time);
      led_matrix_hsv_to_rgb(hsv.h, hsv.s, hsv.v, &r, &g, &b);
    }

    led_matrix_set_pixel_idx(i, r, g, b);
  }
}
