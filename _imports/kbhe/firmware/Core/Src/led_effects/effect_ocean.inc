static void effect_ocean(void) {
  uint8_t hue_bias =
      (uint8_t)(100u + ((uint16_t)effect_param(0, 160u) * 90u) / 255u);
  uint8_t depth_dim = (uint8_t)(32u + ((uint16_t)effect_param(1, 64u) * 120u) / 255u);
  bool foam_highlight = effect_param_flag(2, true);
  uint8_t speed_mul =
      (uint8_t)(1u + ((uint16_t)effect_param(3, 160u) * 4u) / 255u);
  uint8_t wave_angle = effect_param(4, 0u);
  int16_t dir_x = (int16_t)triangle_wave_u8(wave_angle) - 128;
  int16_t dir_y = (int16_t)triangle_wave_u8((uint8_t)(wave_angle + 64u)) - 128;
  for (uint8_t i = 0; i < LED_MATRIX_NUM_LEDS; i++) {
    uint8_t x_term = scale_coord_to_u8(led_pos_x[i], LED_LAYOUT_MAX_X);
    uint8_t y_term = scale_coord_to_u8(led_pos_y[i], LED_LAYOUT_MAX_Y);
    int16_t centered_x = (int16_t)x_term - 128;
    int16_t centered_y = (int16_t)y_term - 128;
    int16_t projected = (int16_t)(((int32_t)centered_x * dir_x +
                                   (int32_t)centered_y * dir_y) /
                                  128);
    uint8_t axis_phase = (uint8_t)(projected + 128);
    uint8_t wave = (uint8_t)(axis_phase + (effect_offset * speed_mul));
    uint8_t hue = (uint8_t)(hue_bias + (wave / 12u) + (y_term / 16u));
    uint8_t brightness = (uint8_t)(255u - ((uint16_t)y_term * depth_dim / 255u));
    uint8_t r, g, b;
    led_matrix_hsv_to_rgb(hue, 255, brightness, &r, &g, &b);
    /* Foam appears at moving crests along the selected wave direction. */
    if (foam_highlight && (wave & 0x1Fu) < 4u) {
      r = (uint8_t)(r + ((255u - r) / 2u));
      g = (uint8_t)(g + ((255u - g) / 2u));
      b = (uint8_t)(b + ((255u - b) / 2u));
    }
    led_matrix_set_pixel_idx(i, r, g, b);
  }
}


