static void effect_plasma(void) {
  uint8_t anim_mul =
      (uint8_t)(1u + ((uint16_t)effect_param(0, 96u) * 4u) / 255u);
  uint8_t sat = effect_param(1, 192u);
  uint8_t radial_div = (uint8_t)(12u + effect_param(2, 128u));
  uint8_t value = effect_param(3, 255u);
  for (uint8_t i = 0; i < LED_MATRIX_NUM_LEDS; i++) {
    uint16_t x = led_pos_x[i] / 12u;
    uint16_t y = led_pos_y[i] / 12u;
    int16_t cx = (int16_t)(led_pos_x[i] / 10u) - (int16_t)(LED_LAYOUT_CENTER_X / 10u);
    int16_t cy = (int16_t)(led_pos_y[i] / 10u) - (int16_t)(LED_LAYOUT_CENTER_Y / 10u);
    uint8_t v1 = (uint8_t)(x + effect_offset * anim_mul);
    uint8_t v2 = (uint8_t)(y + effect_offset * anim_mul);
    uint8_t v3 = (uint8_t)(((x + y) * 3u) / 2u + effect_offset * (anim_mul + 1u));
    uint16_t radial = (uint16_t)((cx * cx + cy * cy) / radial_div);
    uint8_t hue = (uint8_t)(v1 + v2 + v3 + radial);
    uint8_t r, g, b;
    led_matrix_hsv_to_rgb(hue, sat, value, &r, &g, &b);
    led_matrix_set_pixel_idx(i, r, g, b);
  }
}


