static void effect_impact_rainbow(void) {
  uint8_t boost_mode = effect_param(0, 2u);
  uint8_t pattern_mode = effect_param(15, 0u);
  /* param[1]: boost_decay — controls how fast the impact_boost level decays
   * after a keypress or audio hit.  128 = default rate, higher = faster decay */
  uint8_t boost_decay_param = effect_param(1, 184u);
  uint8_t key_weight = effect_param(2, 96u);
  uint8_t audio_weight = effect_param(3, 88u);
  uint8_t max_boost = effect_param(4, 208u);
  uint8_t angle = effect_param(5, 16u);
  uint8_t sat_scale = effect_param(6, 255u);
  uint8_t drift = (uint8_t)(1u + ((uint16_t)effect_param(7, 168u) * 5u) / 255u);
  uint8_t audio_sensitivity = effect_param(12, 168u);
  led_hsv_t base_hsv = effect_primary_hsv(255u, 0u, 0u);
  uint8_t sat = mxfx_scale8(base_hsv.s, sat_scale);
  uint16_t bass_energy = 0u;
  uint8_t bass_drive = 0u;
  uint8_t bass_delta = 0u;
  static uint8_t bass_env = 0u;
  static uint8_t kick_flash = 0u;
  uint8_t decay_step = (uint8_t)(1u + (((uint16_t)boost_decay_param * 11u) / 255u));
    uint8_t impact_trigger =
      (uint8_t)(16u + (((uint16_t)(255u - audio_sensitivity) * 176u) / 255u));
    uint8_t bass_floor =
      (uint8_t)(8u + (((uint16_t)(255u - audio_sensitivity) * 120u) / 255u));
    uint8_t transient_gate =
      (uint8_t)(2u + (((uint16_t)(255u - audio_sensitivity) * 18u) / 255u));

  /* Apply per-frame decay driven by boost_decay_param.  This supplements the
   * time-based decay in reactive_tick_simulation() with a configurable nudge
   * applied each rendered frame (≈ every render_interval ms). */
  if (impact_boost > 0u) {
    uint16_t extra_decay = (uint16_t)(1u +
        (((uint32_t)impact_boost * (uint16_t)(16u + boost_decay_param)) / 1024u));
    impact_boost = (impact_boost > extra_decay) ? (uint16_t)(impact_boost - extra_decay) : 0u;
  }

  if (effect_offset == 0u) {
    bass_env = 0u;
    kick_flash = 0u;
  }

  if (audio_spectrum_valid) {
    for (uint8_t band = 0u; band < 4u; band++) {
      uint8_t weight = (band == 0u) ? 4u : ((band == 1u) ? 3u : ((band == 2u) ? 2u : 1u));
      bass_energy =
          (uint16_t)(bass_energy + (uint16_t)audio_spectrum_levels[band] * weight);
    }

    bass_drive = (uint8_t)(bass_energy / 10u);
    {
      uint8_t prev_env = bass_env;
      if (bass_drive > bass_env) {
        uint8_t rise = (uint8_t)(4u + ((uint16_t)(bass_drive - bass_env) * 120u) / 255u);
        uint16_t next = (uint16_t)bass_env + rise;
        bass_env = (next > bass_drive) ? bass_drive : (uint8_t)next;
      } else {
        uint8_t fall = (uint8_t)(2u + ((uint16_t)(255u - boost_decay_param) / 12u));
        bass_env = (bass_env > fall) ? (uint8_t)(bass_env - fall) : 0u;
      }
      bass_delta = (bass_drive > prev_env) ? (uint8_t)(bass_drive - prev_env) : 0u;
    }

    {
        bool kick_trigger = (audio_impact_level > impact_trigger) ||
                            (bass_delta > transient_gate && bass_drive > bass_floor);

      if (kick_trigger) {
          uint8_t kick_strength = 0u;
          if (audio_impact_level > impact_trigger) {
            uint16_t den = (uint16_t)(255u - impact_trigger);
            uint16_t num = (uint16_t)(audio_impact_level - impact_trigger) * 255u;
            kick_strength = (den > 0u) ? (uint8_t)(num / den) : 255u;
          }

          if (bass_delta > transient_gate) {
            uint16_t den = (uint16_t)(255u - transient_gate);
            uint16_t num = (uint16_t)(bass_delta - transient_gate) * 255u;
            uint8_t transient = (den > 0u) ? (uint8_t)(num / den) : 255u;
            transient = mxfx_qadd8(transient, 32u);
            if (transient > kick_strength) {
              kick_strength = transient;
            }
        }

        if (kick_strength > kick_flash) {
          kick_flash = kick_strength;
        }
      } else {
        uint8_t decay = (uint8_t)(2u + decay_step);
        kick_flash = (kick_flash > decay) ? (uint8_t)(kick_flash - decay) : 0u;
      }
    }
  } else {
    bass_env = 0u;
    kick_flash = 0u;
  }

  uint8_t audio_level = 0u;
  uint8_t bass_level = 0u;
  uint16_t key_component = ((uint32_t)impact_boost * key_weight) / 255u;

  if (audio_spectrum_valid) {
    if (audio_impact_level > impact_trigger) {
      uint16_t den = (uint16_t)(255u - impact_trigger);
      uint16_t num = (uint16_t)(audio_impact_level - impact_trigger) * 255u;
      audio_level = (den > 0u) ? (uint8_t)(num / den) : 255u;
    }
    if (bass_drive > bass_floor) {
      uint16_t den = (uint16_t)(255u - bass_floor);
      uint16_t num = (uint16_t)(bass_drive - bass_floor) * 255u;
      bass_level = (den > 0u) ? (uint8_t)(num / den) : 255u;
    }
  }

  uint16_t audio_component =
      audio_spectrum_valid ? ((uint32_t)audio_level * 8u * audio_weight) / 255u
                           : 0u;
  uint16_t bass_component =
      audio_spectrum_valid ? ((uint32_t)bass_level * 3u * audio_weight) / 255u : 0u;
  uint16_t kick_component =
      audio_spectrum_valid ? ((uint32_t)kick_flash * 5u * audio_weight) / 255u : 0u;
  uint16_t boost = key_component + audio_component + bass_component + kick_component;
  uint8_t white_mix = 0u;
  uint8_t speed_boost = 0u;
  int16_t dir_x = (int16_t)triangle_wave_u8(angle) - 128;
  int16_t dir_y = (int16_t)triangle_wave_u8((uint8_t)(angle + 64u)) - 128;
  uint8_t phase_advance;
  uint8_t phase;

  if (pattern_mode > 3u) {
    pattern_mode = 0u;
  }

  if (boost > max_boost * 4u) {
    boost = (uint16_t)(max_boost * 4u);
  }

  if (boost_mode == 0u || boost_mode == 2u) {
    uint16_t combined_speed_boost =
        (uint16_t)((boost * 4u) / 255u) + mxfx_scale8(kick_flash, 3u);
    speed_boost = (combined_speed_boost > 255u) ? 255u : (uint8_t)combined_speed_boost;
    if (speed_boost > max_boost) {
      speed_boost = max_boost;
    }
  }
  if (boost_mode == 1u || boost_mode == 2u) {
    white_mix = (uint8_t)((boost * 255u) / (uint16_t)(max_boost * 4u + 1u));
    {
      uint8_t kick_mix = mxfx_scale8(kick_flash, max_boost);
      if (kick_mix > white_mix) {
        white_mix = kick_mix;
      }
    }
    if (white_mix > max_boost) {
      white_mix = max_boost;
    }
  }

  phase_advance = (uint8_t)(drift + speed_boost);
  phase = (uint8_t)(effect_offset * phase_advance);

  for (uint8_t i = 0u; i < LED_MATRIX_NUM_LEDS; i++) {
    int16_t x = (int16_t)scale_coord_to_u8(led_pos_x[i], LED_LAYOUT_MAX_X) - 128;
    int16_t y = (int16_t)scale_coord_to_u8(led_pos_y[i], LED_LAYOUT_MAX_Y) - 128;
    uint8_t hue = base_hsv.h;
    uint8_t r = 0u;
    uint8_t g = 0u;
    uint8_t b = 0u;

    switch (pattern_mode) {
    case 1u: {
      uint8_t ang = pseudo_angle_u8(x, y);
      hue = (uint8_t)(base_hsv.h + ang + angle + phase);
      break;
    }
    case 2u: {
      uint8_t dist = mxfx_distance_u8(x, y);
      uint8_t ang = pseudo_angle_u8(x, y);
      hue = (uint8_t)(base_hsv.h + dist + phase -
                      mxfx_scale8((uint8_t)(ang + angle), 192u));
      break;
    }
    case 3u: {
      uint8_t dist = mxfx_distance_u8(x, y);
      hue = (uint8_t)(base_hsv.h + dist + phase + angle);
      break;
    }
    default: {
      int32_t projected = ((int32_t)x * dir_x) + ((int32_t)y * dir_y);
      hue = (uint8_t)(base_hsv.h + 128 + (projected / 512) + phase);
      break;
    }
    }

    led_matrix_hsv_to_rgb(hue, sat, base_hsv.v, &r, &g, &b);

    if (white_mix > 0u) {
      mix_rgb_towards_white(&r, &g, &b, white_mix);
    }

    led_matrix_set_pixel_idx(i, r, g, b);
  }
}


