static void effect_color_cycle(void) {
  /* Param[0] is a hue-cycle multiplier (1..32) for clear, perceptible control.
   * Speed remains controlled by the shared Speed slot (param 14). */
  uint8_t cycle_multiplier =
      (uint8_t)(1u + ((uint16_t)effect_param(0, 64u) * 31u) / 255u);
  uint8_t saturation = effect_param(1, 255u);
  uint8_t value = effect_param(2, 255u);
  uint8_t color_mix = effect_param(3, 0u);
  uint8_t tint_r = 255u;
  uint8_t tint_g = 0u;
  uint8_t tint_b = 0u;

  effect_primary_color(255u, 0u, 0u, &tint_r, &tint_g, &tint_b);
  uint8_t hue = (uint8_t)(effect_offset * cycle_multiplier);
  uint8_t r, g, b;
  led_matrix_hsv_to_rgb(hue, saturation, value, &r, &g, &b);
  if (color_mix > 0u) {
    r = (uint8_t)(((uint16_t)r * (255u - color_mix) +
                   (uint16_t)tint_r * color_mix) /
                  255u);
    g = (uint8_t)(((uint16_t)g * (255u - color_mix) +
                   (uint16_t)tint_g * color_mix) /
                  255u);
    b = (uint8_t)(((uint16_t)b * (255u - color_mix) +
                   (uint16_t)tint_b * color_mix) /
                  255u);
  }
  led_matrix_fill(r, g, b);
}


