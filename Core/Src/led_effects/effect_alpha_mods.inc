static bool effect_is_modifier_key(uint8_t key_index) {
  const settings_key_t *key = settings_get_key(key_index);
  if (key == NULL) {
    return false;
  }

  // USB HID modifier usages: 0xE0..0xE7 (Left Ctrl..Right GUI).
  return (key->hid_keycode >= 0xE0u) && (key->hid_keycode <= 0xE7u);
}

static void effect_alpha_mods(void) {
  /* Param 0 (hue_offset 0-255): hue shift between alpha keys and modifier keys.
   * 0 = use animation speed for dynamic hue drift (original behavior).
   * Non-zero = fixed offset (e.g. 128 = complementary color). */
  uint8_t hue_offset = effect_param(0, 0u);
  led_hsv_t hsv_alpha = effect_primary_hsv(255u, 0u, 0u);
  led_hsv_t hsv_mod = hsv_alpha;
  uint8_t r_alpha = 0u;
  uint8_t g_alpha = 0u;
  uint8_t b_alpha = 0u;
  uint8_t r_mod = 0u;
  uint8_t g_mod = 0u;
  uint8_t b_mod = 0u;

  hsv_mod.h = (uint8_t)(hsv_mod.h +
                        (hue_offset > 0u ? hue_offset : effect_animation_speed()));
  led_matrix_hsv_to_rgb(hsv_alpha.h, hsv_alpha.s, hsv_alpha.v, &r_alpha,
                        &g_alpha, &b_alpha);
  led_matrix_hsv_to_rgb(hsv_mod.h, hsv_mod.s, hsv_mod.v, &r_mod, &g_mod,
                        &b_mod);

  for (uint8_t i = 0u; i < LED_MATRIX_NUM_LEDS; i++) {
    if (effect_is_modifier_key(i)) {
      led_matrix_set_pixel_idx(i, r_mod, g_mod, b_mod);
    } else {
      led_matrix_set_pixel_idx(i, r_alpha, g_alpha, b_alpha);
    }
  }
}
