static void effect_key_state_demo(void) {
  bool invert = effect_param_flag(0, false);
  uint8_t pressed_value = effect_param(1, 255u);
  uint8_t released_value = effect_param(2, 96u);
  uint8_t pressed_r = 0u;
  uint8_t pressed_g = 255u;
  uint8_t pressed_b = 0u;
  uint8_t released_r = 255u;
  uint8_t released_g = 0u;
  uint8_t released_b = 0u;

  effect_primary_color(0u, 255u, 0u, &pressed_r, &pressed_g, &pressed_b);
  effect_secondary_color(255u, 0u, 0u, &released_r, &released_g, &released_b);

  for (uint8_t key = 0u; key < NUM_KEYS; key++) {
    bool pressed = trigger_get_key_state(key) == PRESSED;
    uint8_t r = 0u;
    uint8_t g = 0u;
    uint8_t b = 0u;

    if (invert) {
      pressed = !pressed;
    }

    if (pressed) {
      r = scale_u8(pressed_r, pressed_value);
      g = scale_u8(pressed_g, pressed_value);
      b = scale_u8(pressed_b, pressed_value);
    } else {
      r = scale_u8(released_r, released_value);
      g = scale_u8(released_g, released_value);
      b = scale_u8(released_b, released_value);
    }

    led_matrix_set_pixel_idx(key, r, g, b);
  }
}


