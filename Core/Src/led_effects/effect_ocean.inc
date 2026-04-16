static void effect_ocean(void) {
  uint8_t hue_bias =
      (uint8_t)(100u + ((uint16_t)effect_param(0, 160u) * 90u) / 255u);
  uint8_t depth_dim = (uint8_t)(32u + ((uint16_t)effect_param(1, 64u) * 120u) / 255u);
  bool foam_highlight = effect_param_flag(2, true);
  uint8_t speed_mul =
      (uint8_t)(1u + ((uint16_t)effect_param(3, 160u) * 4u) / 255u);
  for (uint8_t i = 0; i < LED_MATRIX_NUM_LEDS; i++) {
    uint8_t x_term = scale_coord_to_u8(led_pos_x[i], LED_LAYOUT_MAX_X);
    uint8_t y_term = scale_coord_to_u8(led_pos_y[i], LED_LAYOUT_MAX_Y);
    uint8_t wave = (uint8_t)(x_term + (effect_offset * speed_mul));
    uint8_t hue = (uint8_t)(hue_bias + (wave / 12u) + (y_term / 16u));
    uint8_t brightness = (uint8_t)(255u - ((uint16_t)y_term * depth_dim / 255u));
    uint8_t r, g, b;
    led_matrix_hsv_to_rgb(hue, 255, brightness, &r, &g, &b);
    /* Foam appears at wave crests only — using wave position without y_term
     * so highlights are horizontal bands that travel with the wave rather
     * than diagonal stripes. */
    if (foam_highlight && (wave & 0x1Fu) < 4u) {
      r = (uint8_t)(r + ((255u - r) / 2u));
      g = (uint8_t)(g + ((255u - g) / 2u));
      b = (uint8_t)(b + ((255u - b) / 2u));
    }
    led_matrix_set_pixel_idx(i, r, g, b);
  }
}


