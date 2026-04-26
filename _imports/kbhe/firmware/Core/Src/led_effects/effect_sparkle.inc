static void effect_sparkle(void) {
  uint8_t density = (uint8_t)(2u + ((uint16_t)(255u - effect_param(0, 48u)) * 10u) / 255u);
  uint8_t sparkle_value = effect_param(1, 224u);
  uint8_t rainbow_mix = effect_param(2, 160u);
  uint8_t ambient = effect_param(3, 24u);
  uint8_t ambient_r = 255u;
  uint8_t ambient_g = 210u;
  uint8_t ambient_b = 96u;

  effect_primary_color(255u, 210u, 96u, &ambient_r, &ambient_g, &ambient_b);

  for (uint8_t i = 0; i < LED_MATRIX_NUM_LEDS; i++) {
    uint8_t seed = (uint8_t)((led_pos_x[i] / 19u) + (led_pos_y[i] / 23u) + effect_offset) & 0x1Fu;
    uint8_t r, g, b;

    if ((seed % density) == 0u) {
      r = g = b = sparkle_value;
    } else if (seed < density) {
      uint8_t hue = scale_coord_to_u8(led_pos_x[i], LED_LAYOUT_MAX_X) + effect_offset;
      led_matrix_hsv_to_rgb(hue, rainbow_mix, sparkle_value / 2u, &r, &g, &b);
    } else {
      r = scale_u8(ambient_r, ambient);
      g = scale_u8(ambient_g, ambient);
      b = scale_u8(ambient_b, ambient);
    }

    led_matrix_set_pixel_idx(i, r, g, b);
  }
}


