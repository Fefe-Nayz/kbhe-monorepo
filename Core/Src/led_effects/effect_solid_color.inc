static void effect_solid_color(void) {
  uint8_t brightness = effect_param(0, 255u);
  uint8_t base_r = 255u;
  uint8_t base_g = 0u;
  uint8_t base_b = 0u;

  effect_primary_color(255u, 0u, 0u, &base_r, &base_g, &base_b);
  led_matrix_fill(
      (uint8_t)(((uint16_t)base_r * brightness) / 255u),
      (uint8_t)(((uint16_t)base_g * brightness) / 255u),
      (uint8_t)(((uint16_t)base_b * brightness) / 255u));
}
