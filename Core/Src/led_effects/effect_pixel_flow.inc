static void effect_pixel_flow(void) {
  static uint8_t colors[LED_MATRIX_NUM_LEDS][3];
  static uint32_t wait_until_ms = 0u;
  led_hsv_t base_hsv = effect_primary_hsv(255u, 0u, 0u);
  uint32_t now_ms = HAL_GetTick();
  uint32_t interval = 3000u /
                      mxfx_scale16by8(mxfx_qadd8(effect_animation_speed(), 16u),
                                     16u);

  if (effect_offset == 0u) {
    for (uint8_t i = 0u; i < LED_MATRIX_NUM_LEDS; i++) {
      if ((mxfx_random8() & 2u) != 0u) {
        colors[i][0] = 0u;
        colors[i][1] = 0u;
        colors[i][2] = 0u;
      } else {
        uint8_t hue = mxfx_random8();
        uint8_t sat = mxfx_random8_min_max(127u, 255u);
        led_matrix_hsv_to_rgb(hue, sat, base_hsv.v, &colors[i][0],
                              &colors[i][1], &colors[i][2]);
      }
    }
    wait_until_ms = 0u;
  }

  if (now_ms >= wait_until_ms) {
    for (uint8_t i = 0u; i < (uint8_t)(LED_MATRIX_NUM_LEDS - 1u); i++) {
      colors[i][0] = colors[i + 1u][0];
      colors[i][1] = colors[i + 1u][1];
      colors[i][2] = colors[i + 1u][2];
    }

    if ((mxfx_random8() & 2u) != 0u) {
      colors[LED_MATRIX_NUM_LEDS - 1u][0] = 0u;
      colors[LED_MATRIX_NUM_LEDS - 1u][1] = 0u;
      colors[LED_MATRIX_NUM_LEDS - 1u][2] = 0u;
    } else {
      uint8_t hue = mxfx_random8();
      uint8_t sat = mxfx_random8_min_max(127u, 255u);
      led_matrix_hsv_to_rgb(hue, sat, base_hsv.v,
                            &colors[LED_MATRIX_NUM_LEDS - 1u][0],
                            &colors[LED_MATRIX_NUM_LEDS - 1u][1],
                            &colors[LED_MATRIX_NUM_LEDS - 1u][2]);
    }

    wait_until_ms = now_ms + interval;
  }

  for (uint8_t i = 0u; i < LED_MATRIX_NUM_LEDS; i++) {
    led_matrix_set_pixel_idx(i, colors[i][0], colors[i][1], colors[i][2]);
  }
}
