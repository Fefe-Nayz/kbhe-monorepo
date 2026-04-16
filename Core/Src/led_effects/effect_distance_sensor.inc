static void effect_distance_sensor(void) {
  led_matrix_clear();
  uint8_t min_value = effect_param(0, 32u);
  uint8_t hue_span = effect_param(1, 170u);
  uint8_t sat = effect_param(2, 255u);
  bool reverse = effect_param_flag(3, false);
  if (hue_span == 0u) {
    hue_span = 1u;
  }

  for (uint8_t key = 0; key < NUM_KEYS; key++) {
    uint8_t level = analog_read_normalized_value(key);

    // Hue ramps from cyan/blue to red as distance increases.
    uint8_t hue = reverse
                      ? (uint8_t)(((uint16_t)hue_span * level) / 255u)
                      : (uint8_t)(hue_span -
                                  ((uint16_t)hue_span * level) / 255u);
    // Also increase brightness with travel for better readability.
    uint8_t value = (uint8_t)(min_value + ((uint16_t)(255u - min_value) * level) / 255u);

    uint8_t r, g, b;
    led_matrix_hsv_to_rgb(hue, sat, value, &r, &g, &b);
    led_matrix_set_pixel_idx(key, r, g, b);
  }
}


