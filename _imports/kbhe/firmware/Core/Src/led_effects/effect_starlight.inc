static void effect_starlight_set_color(uint8_t index, led_hsv_t base_hsv,
                                           uint8_t colors[][3]) {
  uint8_t time = (uint8_t)mxfx_scale16by8(mxfx_timer16(),
                                         (uint8_t)(effect_animation_speed() / 8u));
  led_hsv_t hsv = base_hsv;

  hsv.v = mxfx_scale8((uint8_t)(abs_u8_diff(mxfx_sin8(time), 128u) * 2u), hsv.v);
  led_matrix_hsv_to_rgb(hsv.h, hsv.s, hsv.v, &colors[index][0],
                        &colors[index][1], &colors[index][2]);
}

static void effect_starlight(void) {
  static uint8_t colors[LED_MATRIX_NUM_LEDS][3];
  static uint16_t active_index = (uint16_t)LED_MATRIX_NUM_LEDS;
  led_hsv_t base_hsv = effect_primary_hsv(255u, 0u, 0u);

  if (effect_offset == 0u) {
    for (uint8_t i = 0u; i < LED_MATRIX_NUM_LEDS; i++) {
      effect_starlight_set_color(i, base_hsv, colors);
    }
    active_index = (uint16_t)LED_MATRIX_NUM_LEDS;
  }

  if ((mxfx_scale16by8(mxfx_timer16(), mxfx_qadd8(effect_animation_speed(), 5u)) %
       5u) == 0u) {
    active_index = mxfx_random8_max(LED_MATRIX_NUM_LEDS);
  }

  if (active_index < LED_MATRIX_NUM_LEDS) {
    effect_starlight_set_color((uint8_t)active_index, base_hsv, colors);
    active_index = (uint16_t)LED_MATRIX_NUM_LEDS;
  }

  for (uint8_t i = 0u; i < LED_MATRIX_NUM_LEDS; i++) {
    led_matrix_set_pixel_idx(i, colors[i][0], colors[i][1], colors[i][2]);
  }
}
