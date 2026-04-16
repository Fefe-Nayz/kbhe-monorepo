static bool effect_is_alpha_key(uint8_t key_index) {
  /* If the host has sent a layout-aware alpha mask, use it. */
  if (alpha_key_mask_configured) {
    uint8_t byte_idx = key_index / 8u;
    uint8_t bit_idx  = key_index % 8u;
    if (byte_idx >= ALPHA_MASK_BYTES) {
      return false;
    }
    return (alpha_key_mask[byte_idx] >> bit_idx) & 0x01u;
  }

  /* Fallback: treat standard A-Z HID usages as alpha (layout-agnostic). */
  const settings_key_t *key = settings_get_key(key_index);
  if (key == NULL) {
    return false;
  }
  return (key->hid_keycode >= 0x04u) && (key->hid_keycode <= 0x1Du);
}

static void effect_alpha_mods(void) {
  // Param 0: hue offset between alpha keys and non-alpha keys.
  uint8_t hue_offset = effect_param(0, 128u);
  // Param 1: modifier saturation scale (0..255).
  uint8_t mod_sat_scale = effect_param(1, 255u);
  // Param 2: modifier brightness scale (0..255).
  uint8_t mod_val_scale = effect_param(2, 255u);
  led_hsv_t hsv_alpha = effect_primary_hsv(255u, 0u, 0u);
  led_hsv_t hsv_mod = hsv_alpha;
  uint8_t r_alpha = 0u;
  uint8_t g_alpha = 0u;
  uint8_t b_alpha = 0u;
  uint8_t r_mod = 0u;
  uint8_t g_mod = 0u;
  uint8_t b_mod = 0u;

  // Legacy profiles had these slots unset (0); keep historical full-intensity behavior.
  if (mod_sat_scale == 0u && mod_val_scale == 0u) {
    mod_sat_scale = 255u;
    mod_val_scale = 255u;
  }

  hsv_mod.h = (uint8_t)(hsv_mod.h + hue_offset);
  hsv_mod.s = mxfx_scale8(hsv_mod.s, mod_sat_scale);
  hsv_mod.v = mxfx_scale8(hsv_mod.v, mod_val_scale);

  led_matrix_hsv_to_rgb(hsv_alpha.h, hsv_alpha.s, hsv_alpha.v, &r_alpha,
                        &g_alpha, &b_alpha);
  led_matrix_hsv_to_rgb(hsv_mod.h, hsv_mod.s, hsv_mod.v, &r_mod, &g_mod,
                        &b_mod);

  for (uint8_t i = 0u; i < LED_MATRIX_NUM_LEDS; i++) {
    if (effect_is_alpha_key(i)) {
      led_matrix_set_pixel_idx(i, r_alpha, g_alpha, b_alpha);
    } else {
      led_matrix_set_pixel_idx(i, r_mod, g_mod, b_mod);
    }
  }
}
