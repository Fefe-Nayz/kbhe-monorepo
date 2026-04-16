static void effect_impact_rainbow(void) {
  uint8_t boost_mode = effect_param(0, 2u);
  /* param[1]: boost_decay — controls how fast the impact_boost level decays
   * after a keypress or audio hit.  128 = default rate, higher = faster decay */
  uint8_t boost_decay_param = effect_param(1, 184u);
  uint8_t key_weight = effect_param(2, 96u);
  uint8_t audio_weight = effect_param(3, 88u);
  uint8_t max_boost = effect_param(4, 208u);
  uint8_t angle = effect_param(5, 16u);
  uint8_t sat = effect_param(6, 255u);
  uint8_t drift = (uint8_t)(1u + ((uint16_t)effect_param(7, 168u) * 5u) / 255u);

  /* Apply per-frame decay driven by boost_decay_param.  This supplements the
   * time-based decay in reactive_tick_simulation() with a configurable nudge
   * applied each rendered frame (≈ every render_interval ms). */
  if (boost_decay_param > 128u && impact_boost > 0u) {
    uint16_t extra_decay = (uint16_t)(((uint32_t)(boost_decay_param - 128u) * impact_boost) / 512u);
    impact_boost = (impact_boost > extra_decay) ? (uint16_t)(impact_boost - extra_decay) : 0u;
  }

  uint16_t key_component = ((uint32_t)impact_boost * key_weight) / 255u;
  uint16_t audio_component =
      audio_spectrum_valid ? ((uint32_t)audio_impact_level * 6u * audio_weight) / 255u
                           : 0u;
  uint16_t boost = key_component + audio_component;
  uint8_t white_mix = 0u;
  uint8_t speed_boost = 0u;
  int16_t dir_x = (int16_t)triangle_wave_u8(angle) - 128;
  int16_t dir_y = (int16_t)triangle_wave_u8((uint8_t)(angle + 64u)) - 128;

  if (boost > max_boost * 4u) {
    boost = (uint16_t)(max_boost * 4u);
  }

  if (boost_mode == 0u || boost_mode == 2u) {
    speed_boost = (uint8_t)((boost * 4u) / 255u);
  }
  if (boost_mode == 1u || boost_mode == 2u) {
    white_mix = (uint8_t)((boost * 255u) / (uint16_t)(max_boost * 4u + 1u));
  }

  for (uint8_t i = 0u; i < LED_MATRIX_NUM_LEDS; i++) {
    int16_t x = (int16_t)scale_coord_to_u8(led_pos_x[i], LED_LAYOUT_MAX_X) - 128;
    int16_t y = (int16_t)scale_coord_to_u8(led_pos_y[i], LED_LAYOUT_MAX_Y) - 128;
    int32_t projected = ((int32_t)x * dir_x) + ((int32_t)y * dir_y);
    uint8_t hue = (uint8_t)(128 + (projected / 512) +
                            (uint8_t)(effect_offset * (drift + speed_boost)));
    uint8_t r = 0u;
    uint8_t g = 0u;
    uint8_t b = 0u;
    led_matrix_hsv_to_rgb(hue, sat, 255u, &r, &g, &b);

    if (white_mix > 0u) {
      mix_rgb_towards_white(&r, &g, &b, white_mix);
    }

    led_matrix_set_pixel_idx(i, r, g, b);
  }
}


