static void effect_audio_spectrum(void) {
  uint8_t hue_span = effect_param(0, 128u);
  uint8_t background_glow = effect_param(1, 0u);
  uint8_t peak_gain = effect_param(2, 255u);
  bool mirror = effect_param_flag(3, false);
  uint8_t release = effect_param(4, 104u);
  uint8_t visualizer_mode = effect_param(5, 0u);
  uint8_t contrast = effect_param(6, 236u);
  uint8_t base_r = 64u;
  uint8_t base_g = 255u;
  uint8_t base_b = 160u;
  uint8_t base_hue = 0u;
  uint8_t tmp = 0u;
  uint8_t spectrum[LED_AUDIO_SPECTRUM_BAND_COUNT];
  uint8_t glow_floor = 0u;
  uint8_t glow_boost = 0u;
  uint8_t center_y_norm;
  uint16_t band_energy_sum = 0u;
  uint8_t band_energy_max = 0u;
  uint16_t bass_energy = 0u;
  static uint8_t band_level[LED_AUDIO_SPECTRUM_BAND_COUNT];
  static uint8_t band_peak[LED_AUDIO_SPECTRUM_BAND_COUNT];
  static uint8_t band_peak_hold[LED_AUDIO_SPECTRUM_BAND_COUNT];
  static uint8_t glow_pulse = 0u;
  static bool initialized = false;

  if (!initialized || effect_offset == 0u) {
    memset(band_level, 0, sizeof(band_level));
    memset(band_peak, 0, sizeof(band_peak));
    memset(band_peak_hold, 0, sizeof(band_peak_hold));
    glow_pulse = 0u;
    initialized = true;
  }

  effect_primary_color(64u, 255u, 160u, &base_r, &base_g, &base_b);
  led_matrix_rgb_to_hsv(base_r, base_g, base_b, &base_hue, &tmp, &tmp);

  memcpy(spectrum, audio_spectrum_levels, sizeof(spectrum));
  if (!audio_spectrum_valid) {
    memset(spectrum, 0, sizeof(spectrum));
  }

  for (uint8_t band = 0u; band < LED_AUDIO_SPECTRUM_BAND_COUNT; band++) {
    uint8_t target = scale_u8(spectrum[band], peak_gain);
    uint8_t gate = (uint8_t)(8u + ((uint16_t)(255u - contrast) / 7u));
    uint8_t curved;
    uint8_t punch;

    if (target <= gate) {
      target = 0u;
    } else {
      target = (uint8_t)(((uint16_t)(target - gate) * 255u) / (255u - gate));
    }

    // Push low bins down harder to keep dark regions dark when contrast is high.
    curved = scale_u8(target, target);
    punch = scale_u8(curved, target);
    target = (uint8_t)((((uint16_t)curved * (255u - contrast)) +
              ((uint16_t)punch * contrast)) /
                       255u);

    if (target > band_level[band]) {
      uint8_t delta = (uint8_t)(target - band_level[band]);
      uint8_t rise = (uint8_t)(10u + scale_u8(delta, 236u));
      uint16_t next = (uint16_t)band_level[band] + rise;
      band_level[band] = (next > target) ? target : (uint8_t)next;
    } else {
      uint8_t fall = (uint8_t)(1u + ((uint16_t)(255u - release) / 16u));
      band_level[band] =
          (band_level[band] > fall) ? (uint8_t)(band_level[band] - fall) : 0u;
    }

    if (band_level[band] >= band_peak[band]) {
      band_peak[band] = band_level[band];
      band_peak_hold[band] = (uint8_t)(2u + (release / 64u));
    } else if (band_peak_hold[band] > 0u) {
      band_peak_hold[band]--;
    } else {
      uint8_t peak_fall = (uint8_t)(1u + ((uint16_t)(255u - release) / 20u));
      band_peak[band] = (band_peak[band] > peak_fall)
                            ? (uint8_t)(band_peak[band] - peak_fall)
                            : 0u;
    }

    band_energy_sum = (uint16_t)(band_energy_sum + band_level[band]);
    if (band_level[band] > band_energy_max) {
      band_energy_max = band_level[band];
    }
    if (band < 3u) {
      uint8_t weight = (band == 0u) ? 4u : ((band == 1u) ? 3u : 2u);
      bass_energy =
          (uint16_t)(bass_energy + (uint16_t)band_level[band] * weight);
    }
  }

  if (background_glow == 0u) {
    glow_pulse = 0u;
    glow_floor = 0u;
    glow_boost = 0u;
  } else {
    uint8_t band_energy_avg =
        (uint8_t)(band_energy_sum / LED_AUDIO_SPECTRUM_BAND_COUNT);
    uint8_t bass_drive = (uint8_t)(bass_energy / 9u);
    uint8_t pulse_target =
        (uint8_t)((((uint16_t)bass_drive * 3u) +
                   ((uint16_t)band_energy_max * 2u) +
                   (uint16_t)band_energy_avg +
                   ((uint16_t)audio_impact_level * 2u)) /
                  8u);

    pulse_target = scale_u8(pulse_target, background_glow);
    if (pulse_target > glow_pulse) {
      uint8_t delta = (uint8_t)(pulse_target - glow_pulse);
      uint8_t rise = (uint8_t)(2u + scale_u8(delta, 180u));
      uint16_t next = (uint16_t)glow_pulse + rise;
      glow_pulse = (next > pulse_target) ? pulse_target : (uint8_t)next;
    } else {
      uint8_t fall = (uint8_t)(1u + ((uint16_t)(255u - release) / 18u));
      glow_pulse = (glow_pulse > fall) ? (uint8_t)(glow_pulse - fall) : 0u;
    }

    // Background stays subtle; lit bars get most of the rhythm pulse.
    glow_floor = scale_u8(glow_pulse, 18u);
    glow_boost = scale_u8(glow_pulse, 96u);
  }

  center_y_norm = scale_coord_to_u8(LED_LAYOUT_CENTER_Y, LED_LAYOUT_MAX_Y);

  for (uint8_t i = 0u; i < LED_MATRIX_NUM_LEDS; i++) {
    uint8_t x_norm = scale_coord_to_u8(led_pos_x[i], LED_LAYOUT_MAX_X);
    uint8_t y_norm = scale_coord_to_u8(led_pos_y[i], LED_LAYOUT_MAX_Y);
    uint8_t axis_norm = (visualizer_mode == 2u) ? (uint8_t)(255u - y_norm) : x_norm;
    uint16_t axis_warp = axis_norm;
    uint16_t band_pos;
    uint8_t band_lo;
    uint8_t band_hi;
    uint8_t band_mix;
    uint16_t level_interp;
    uint16_t peak_interp;
    uint8_t level;
    uint8_t peak;
    uint8_t y_from_bottom = (uint8_t)(255u - y_norm);
    uint8_t x_from_left = x_norm;
    uint16_t center_distance_u16 =
      (uint16_t)abs_u8_diff(y_norm, center_y_norm) * 2u;
    uint8_t distance_to_center =
      (center_distance_u16 > 255u) ? 255u : (uint8_t)center_distance_u16;
    const uint8_t vertical_bins = 7u;
    const uint8_t horizontal_bins = 12u;
    const uint8_t center_bins = 7u;
    uint8_t row_bin_from_bottom =
      (uint8_t)(((uint16_t)y_from_bottom * (vertical_bins - 1u) + 127u) /
            255u);
    uint8_t col_bin_from_left =
      (uint8_t)(((uint16_t)x_from_left * (horizontal_bins - 1u) + 127u) /
            255u);
    uint8_t center_bin =
      (uint8_t)(((uint16_t)distance_to_center * (center_bins - 1u) + 127u) /
            255u);
    uint8_t level_rows;
    uint8_t peak_rows;
    uint8_t level_cols;
    uint8_t peak_cols;
    uint8_t level_center;
    uint8_t peak_center;
    bool lit = false;
    bool peak_marker = false;
    uint8_t value = 0u;
    uint8_t hue;
    uint8_t sat = 255u;
    uint8_t r = 0u;
    uint8_t g = 0u;
    uint8_t b = 0u;
    uint8_t mode = (uint8_t)(visualizer_mode & 0x03u);

    if (mirror && axis_norm >= 128u) {
      axis_warp = (uint16_t)(255u - axis_warp);
    }

    band_pos = axis_warp * (LED_AUDIO_SPECTRUM_BAND_COUNT - 1u);
    band_lo = (uint8_t)(band_pos / 255u);
    band_mix = (uint8_t)(band_pos % 255u);
    band_hi = (band_lo < (LED_AUDIO_SPECTRUM_BAND_COUNT - 1u))
                  ? (uint8_t)(band_lo + 1u)
                  : band_lo;

    level_interp =
        ((uint16_t)band_level[band_lo] * (uint16_t)(255u - band_mix)) +
        ((uint16_t)band_level[band_hi] * (uint16_t)band_mix);
    peak_interp =
        ((uint16_t)band_peak[band_lo] * (uint16_t)(255u - band_mix)) +
        ((uint16_t)band_peak[band_hi] * (uint16_t)band_mix);
    level = (uint8_t)(level_interp / 255u);
    peak = (uint8_t)(peak_interp / 255u);
    level_rows =
      (uint8_t)(((uint16_t)level * (vertical_bins - 1u) + 127u) / 255u);
    peak_rows =
      (uint8_t)(((uint16_t)peak * (vertical_bins - 1u) + 127u) / 255u);
    level_cols =
      (uint8_t)(((uint16_t)level * (horizontal_bins - 1u) + 127u) / 255u);
    peak_cols =
      (uint8_t)(((uint16_t)peak * (horizontal_bins - 1u) + 127u) / 255u);
    level_center =
      (uint8_t)(((uint16_t)level * (center_bins - 1u) + 127u) / 255u);
    peak_center =
      (uint8_t)(((uint16_t)peak * (center_bins - 1u) + 127u) / 255u);

    switch (mode) {
    case 1u:
      lit = (center_bin <= level_center);
      peak_marker = (peak > 0u && center_bin == peak_center);
      break;
    case 2u:
      lit = (col_bin_from_left <= level_cols);
      peak_marker = (peak > 0u && col_bin_from_left == peak_cols);
      break;
    case 3u:
      {
        uint8_t focus_level_rows = (uint8_t)((((uint16_t)scale_u8(level, 136u) *
                                               (vertical_bins - 1u)) +
                                              127u) /
                                             255u);
        lit = (row_bin_from_bottom <= focus_level_rows);
      }
      peak_marker = (peak > 0u && row_bin_from_bottom == peak_rows);
      break;
    default:
      lit = (row_bin_from_bottom <= level_rows);
      peak_marker = (peak > 0u && row_bin_from_bottom == peak_rows);
      break;
    }

    if (peak_marker) {
      value = 255u;
      sat = 255u;
    } else if (lit) {
      uint8_t gradient_axis;
      uint8_t vivid_curve;

      if (mode == 2u) {
        gradient_axis =
            (uint8_t)(((uint16_t)col_bin_from_left * 255u) /
                      (horizontal_bins - 1u));
      } else {
        uint8_t row_norm =
            (uint8_t)(((uint16_t)row_bin_from_bottom * 255u) /
                      (vertical_bins - 1u));
        gradient_axis = (uint8_t)(255u - row_norm);
      }

      vivid_curve = scale_u8(gradient_axis, 220u);
      value = (uint8_t)(40u + scale_u8(vivid_curve, 215u));

      if (glow_boost > 0u) {
        uint16_t boosted = (uint16_t)value + glow_boost;
        value = (boosted > 255u) ? 255u : (uint8_t)boosted;
      }
    } else {
      value = (mode == 3u) ? scale_u8(glow_floor, 132u) : glow_floor;
    }

    if (mode == 2u) {
      hue =
          (uint8_t)(base_hue + ((uint16_t)(255u - y_norm) * hue_span) / 255u);
    } else {
      uint8_t hue_axis = x_norm;
      if (mirror && x_norm >= 128u) {
        hue_axis = (uint8_t)(255u - x_norm);
      }
      hue =
          (uint8_t)(base_hue + ((uint16_t)hue_axis * hue_span) / 255u);
    }

    if (peak_marker) {
      hue = (uint8_t)(hue + 18u);
    }

    if (audio_impact_level > 0u && value > 0u) {
      uint8_t impact_boost = scale_u8(audio_impact_level, 88u);
      uint8_t impact_hue_shift = scale_u8(audio_impact_level, 10u);
      uint16_t boosted_value = (uint16_t)value + impact_boost;
      value = (boosted_value > 255u) ? 255u : (uint8_t)boosted_value;
      hue = (uint8_t)(hue + impact_hue_shift);
    }

    led_matrix_hsv_to_rgb(hue, sat, value, &r, &g, &b);

    led_matrix_set_pixel_idx(i, r, g, b);
  }
}


