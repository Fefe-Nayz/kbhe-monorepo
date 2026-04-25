static void effect_breathing(void) {
  uint8_t min_brightness = effect_param(0, 24u);
  uint8_t max_brightness = effect_param(1, 255u);
  uint8_t plateau = (uint8_t)(((uint16_t)effect_param(2, 48u) * 20u) / 255u);
  uint8_t base_r = 255u;
  uint8_t base_g = 0u;
  uint8_t base_b = 0u;

  effect_primary_color(255u, 0u, 0u, &base_r, &base_g, &base_b);
  if (max_brightness < min_brightness) {
    uint8_t tmp = max_brightness;
    max_brightness = min_brightness;
    min_brightness = tmp;
  }
  uint8_t breath = (effect_offset % 128);
  if (breath > 64)
    breath = 128 - breath;
  if (plateau > 0u && breath > (uint8_t)(64u - plateau)) {
    breath = 64u;
  }
  uint8_t brightness = (uint8_t)(
      min_brightness +
      (((uint16_t)(max_brightness - min_brightness) * breath) / 64u));

  uint8_t r = (uint8_t)((uint16_t)base_r * brightness / 255u);
  uint8_t g = (uint8_t)((uint16_t)base_g * brightness / 255u);
  uint8_t b = (uint8_t)((uint16_t)base_b * brightness / 255u);

  led_matrix_fill(r, g, b);
}
