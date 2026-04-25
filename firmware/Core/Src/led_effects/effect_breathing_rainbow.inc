static void effect_breathing_rainbow(void) {
  uint8_t min_brightness = effect_param(0, 24u);
  uint8_t hue_mul =
      (uint8_t)(1u + ((uint16_t)effect_param(1, 192u) * 4u) / 255u);
  uint8_t sat = effect_param(2, 255u);
  uint8_t breath = (effect_offset % 128);
  if (breath > 64)
    breath = 128 - breath;
  uint8_t brightness =
      (uint8_t)(min_brightness + (((uint16_t)(255u - min_brightness) * breath) / 64u));
  uint8_t hue = (uint8_t)((effect_offset * hue_mul) / 2u);
  uint8_t r, g, b;
  led_matrix_hsv_to_rgb(hue, sat, brightness, &r, &g, &b);
  led_matrix_fill(r, g, b);
}


