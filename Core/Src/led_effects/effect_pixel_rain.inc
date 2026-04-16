static void effect_pixel_rain(void) {
  static uint8_t colors[LED_MATRIX_NUM_LEDS][3];
  static uint8_t active_index = 0u;
  static uint32_t next_update_ms = 0u;
  led_hsv_t base_hsv = effect_primary_hsv(255u, 0u, 0u);
  uint8_t hue_range = effect_param(0, 255u);
  uint32_t now_ms = HAL_GetTick();

  if (effect_offset == 0u) {
    memset(colors, 0, sizeof(colors));
    active_index = mxfx_random8_max(LED_MATRIX_NUM_LEDS);
    next_update_ms = 0u;
  }

  if (now_ms >= next_update_ms) {
    if ((mxfx_random8() & 2u) != 0u) {
      colors[active_index][0] = 0u;
      colors[active_index][1] = 0u;
      colors[active_index][2] = 0u;
    } else {
      int16_t signed_random = (int16_t)mxfx_random8() - 128;
      int16_t hue_offset = (signed_random * hue_range) / 255;
      uint8_t hue = (uint8_t)(base_hsv.h + (int8_t)hue_offset);
      uint8_t sat = mxfx_random8_min_max(127u, 255u);
      led_matrix_hsv_to_rgb(hue, sat, base_hsv.v, &colors[active_index][0],
                            &colors[active_index][1],
                            &colors[active_index][2]);
    }

    active_index = mxfx_random8_max(LED_MATRIX_NUM_LEDS);
    next_update_ms = now_ms +
                     (uint32_t)(2048u -
                                mxfx_scale16by8(1792u, effect_animation_speed()));
  }

  for (uint8_t i = 0u; i < LED_MATRIX_NUM_LEDS; i++) {
    led_matrix_set_pixel_idx(i, colors[i][0], colors[i][1], colors[i][2]);
  }
}
