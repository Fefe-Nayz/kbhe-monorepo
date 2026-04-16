static void effect_digital_rain(void) {
  enum {
    mxfx_DIGITAL_RAIN_COLS = 16u,
    mxfx_DIGITAL_RAIN_ROWS = 9u,
  };

  static uint8_t frame[mxfx_DIGITAL_RAIN_ROWS][mxfx_DIGITAL_RAIN_COLS];
  static uint8_t drop = 0u;
  static uint8_t decay = 0u;
  static bool initialized = false;

  uint8_t trail = effect_param(0, 64u);
  uint8_t head = effect_param(1, 160u);
  uint8_t density = effect_param(2, 96u);
  bool white_heads = effect_param_flag(3, true);
  uint8_t hue_bias = effect_param(4, 0u);
  uint8_t max_intensity = (head == 0u) ? 1u : head;
  uint8_t pure_green_intensity = (uint8_t)(((uint16_t)max_intensity * 3u) >> 2);
  uint8_t max_brightness_boost =
      (uint8_t)(((uint16_t)max_intensity * 3u) >> 2);
  uint8_t decay_ticks =
      (uint8_t)(1u + (((uint16_t)(255u - trail) * (255u / max_intensity)) / 255u));
  uint8_t drop_ticks =
      (uint8_t)(8u + mxfx_scale16by8(20u, (uint8_t)(255u - effect_animation_speed())));
  uint8_t drops_divisor = (uint8_t)(density / 4u);

  if (!initialized || effect_offset == 0u) {
    memset(frame, 0, sizeof(frame));
    drop = 0u;
    decay = 0u;
    initialized = true;
  }

  if (drops_divisor < 4u) {
    drops_divisor = 4u;
  }

  decay++;

  if (drop == 0u) {
    for (uint8_t col = 0u; col < mxfx_DIGITAL_RAIN_COLS; col++) {
      if (mxfx_random8_max(drops_divisor) == 0u) {
        frame[0][col] = max_intensity;
      }
    }
  }

  if (decay >= decay_ticks) {
    decay = 0u;
  }

  for (uint8_t row = 0u; row < mxfx_DIGITAL_RAIN_ROWS; row++) {
    for (uint8_t col = 0u; col < mxfx_DIGITAL_RAIN_COLS; col++) {
      if (frame[row][col] > 0u && frame[row][col] < max_intensity) {
        if (decay == 0u) {
          frame[row][col]--;
        }
      }
    }
  }

  if (++drop > drop_ticks) {
    drop = 0u;
    for (uint8_t row = (uint8_t)(mxfx_DIGITAL_RAIN_ROWS - 1u); row > 0u;
         row--) {
      for (uint8_t col = 0u; col < mxfx_DIGITAL_RAIN_COLS; col++) {
        if (row == (uint8_t)(mxfx_DIGITAL_RAIN_ROWS - 1u) &&
            frame[row][col] == max_intensity) {
          frame[row][col] = (uint8_t)(max_intensity - 1u);
        }
        if (frame[row - 1u][col] >= max_intensity) {
          frame[row - 1u][col] = (uint8_t)(max_intensity - 1u);
          frame[row][col] = max_intensity;
        }
      }
    }
  }

  for (uint8_t i = 0u; i < LED_MATRIX_NUM_LEDS; i++) {
    uint8_t x = mxfx_led_coord_x(i);
    uint8_t y = mxfx_led_coord_y(i);
    uint8_t col = (uint8_t)(((uint16_t)x * (mxfx_DIGITAL_RAIN_COLS - 1u)) /
                            mxfx_COORD_MAX_X);
    uint8_t row = (uint8_t)(((uint16_t)y * (mxfx_DIGITAL_RAIN_ROWS - 1u)) /
                            mxfx_COORD_MAX_Y);
    uint8_t intensity = frame[row][col];
    uint8_t r = 0u;
    uint8_t g = 0u;
    uint8_t b = 0u;

    if (intensity == 0u) {
      led_matrix_set_pixel_idx(i, 0u, 0u, 0u);
      continue;
    }

    if (intensity > pure_green_intensity && white_heads) {
      uint8_t boost =
          (uint8_t)(((uint16_t)max_brightness_boost *
                     (intensity - pure_green_intensity)) /
                    ((max_intensity > pure_green_intensity)
                         ? (uint8_t)(max_intensity - pure_green_intensity)
                         : 1u));
      r = boost;
      g = max_intensity;
      b = boost;
    } else {
      uint16_t green_u16 =
          ((uint16_t)max_intensity * intensity) /
          ((pure_green_intensity == 0u) ? 1u : pure_green_intensity);
      uint8_t green = clamp_u16_to_u8(green_u16);
      led_matrix_hsv_to_rgb((uint8_t)(96u + hue_bias), 255u, green, &r, &g,
                            &b);
    }

    led_matrix_set_pixel_idx(i, r, g, b);
  }
}
