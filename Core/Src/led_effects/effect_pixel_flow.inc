static void effect_pixel_flow(void) {
  enum {
    mxfx_FLOW_KEY_PITCH = 190,
    mxfx_FLOW_VECTOR_SCALE = 128,
    mxfx_FLOW_MAX_STEP_PITCHES = 2,
  };

  static uint8_t colors[LED_MATRIX_NUM_LEDS][3];
  static uint8_t next_colors[LED_MATRIX_NUM_LEDS][3];
  static uint32_t wait_until_ms = 0u;
  int32_t along_value[LED_MATRIX_NUM_LEDS];
  int32_t across_value[LED_MATRIX_NUM_LEDS];
  uint8_t lane_id[LED_MATRIX_NUM_LEDS];
  int16_t upstream_source[LED_MATRIX_NUM_LEDS];

  led_hsv_t base_hsv = effect_primary_hsv(255u, 0u, 0u);
  uint8_t hue_range = effect_param(0, 255u);
  bool reverse = effect_param_flag(1, false);
  uint8_t angle = effect_param(2, 0u);
  int16_t dir_x = (int16_t)mxfx_cos8(angle) - 128;
  int16_t dir_y = (int16_t)mxfx_sin8(angle) - 128;
  int32_t across_min = 0;
  uint32_t lane_pitch =
      (uint32_t)mxfx_FLOW_KEY_PITCH * (uint32_t)mxfx_FLOW_VECTOR_SCALE;
  uint32_t max_forward_gap = lane_pitch * (uint32_t)mxfx_FLOW_MAX_STEP_PITCHES;
  uint32_t now_ms = HAL_GetTick();
  uint16_t interval_scale =
      mxfx_scale16by8(mxfx_qadd8(effect_animation_speed(), 16u), 16u);
  uint32_t interval;

  /* Snap cardinals cleanly so 0/64/128/192 do not drift into adjacent lanes. */
  if (dir_x >= -1 && dir_x <= 1) {
    dir_x = 0;
  }
  if (dir_y >= -1 && dir_y <= 1) {
    dir_y = 0;
  }
  if (dir_x == 0 && dir_y == 0) {
    dir_x = mxfx_FLOW_VECTOR_SCALE;
  }

  if (interval_scale == 0u) {
    interval_scale = 1u;
  }
  interval = 3000u / interval_scale;

  if (effect_offset == 0u) {
    for (uint8_t i = 0u; i < LED_MATRIX_NUM_LEDS; i++) {
      if ((mxfx_random8() & 2u) != 0u) {
        colors[i][0] = 0u;
        colors[i][1] = 0u;
        colors[i][2] = 0u;
      } else {
        int16_t signed_random = (int16_t)mxfx_random8() - 128;
        int16_t hue_offset = (signed_random * hue_range) / 255;
        uint8_t hue = (uint8_t)(base_hsv.h + (int8_t)hue_offset);
        uint8_t sat = mxfx_random8_min_max(127u, 255u);
        led_matrix_hsv_to_rgb(hue, sat, base_hsv.v, &colors[i][0],
                              &colors[i][1], &colors[i][2]);
      }
    }
    wait_until_ms = 0u;
  }

  for (uint8_t i = 0u; i < LED_MATRIX_NUM_LEDS; i++) {
    int16_t x = (int16_t)led_pos_x[i] - (int16_t)LED_LAYOUT_CENTER_X;
    int16_t y = (int16_t)led_pos_y[i] - (int16_t)LED_LAYOUT_CENTER_Y;
    along_value[i] = (int32_t)x * dir_x + (int32_t)y * dir_y;
    across_value[i] = (int32_t)x * dir_y - (int32_t)y * dir_x;

    if (i == 0u || across_value[i] < across_min) {
      across_min = across_value[i];
    }
  }

  for (uint8_t i = 0u; i < LED_MATRIX_NUM_LEDS; i++) {
    uint32_t across_offset = (uint32_t)(across_value[i] - across_min);
    lane_id[i] =
        (uint8_t)((across_offset + (lane_pitch / 2u)) / lane_pitch);
  }

  for (uint8_t i = 0u; i < LED_MATRIX_NUM_LEDS; i++) {
    int16_t best_source = -1;
    uint32_t best_forward_gap = 0xFFFFFFFFu;
    uint32_t best_side_gap = 0xFFFFFFFFu;

    for (uint8_t j = 0u; j < LED_MATRIX_NUM_LEDS; j++) {
      int32_t forward_gap;
      int32_t side_gap;
      uint32_t side_gap_abs;

      if (i == j) {
        continue;
      }
      if (lane_id[i] != lane_id[j]) {
        continue;
      }

      if (reverse) {
        forward_gap = along_value[i] - along_value[j];
      } else {
        forward_gap = along_value[j] - along_value[i];
      }

      if (forward_gap <= 0 || (uint32_t)forward_gap > max_forward_gap) {
        continue;
      }

      side_gap = across_value[i] - across_value[j];
      side_gap_abs = (uint32_t)((side_gap < 0) ? -side_gap : side_gap);

      /* Do not bridge large empty spans: if a lane re-enters the keyboard
       * later, a new particle must be spawned there instead of teleporting. */
      if ((uint32_t)forward_gap < best_forward_gap ||
          ((uint32_t)forward_gap == best_forward_gap &&
           side_gap_abs < best_side_gap)) {
        best_forward_gap = (uint32_t)forward_gap;
        best_side_gap = side_gap_abs;
        best_source = (int16_t)j;
      }
    }

    upstream_source[i] = best_source;
  }

  if (now_ms >= wait_until_ms) {
    for (uint8_t i = 0u; i < LED_MATRIX_NUM_LEDS; i++) {
      int16_t source = upstream_source[i];

      if (source >= 0) {
        next_colors[i][0] = colors[(uint8_t)source][0];
        next_colors[i][1] = colors[(uint8_t)source][1];
        next_colors[i][2] = colors[(uint8_t)source][2];
      } else if ((mxfx_random8() & 2u) != 0u) {
        next_colors[i][0] = 0u;
        next_colors[i][1] = 0u;
        next_colors[i][2] = 0u;
      } else {
        int16_t signed_random = (int16_t)mxfx_random8() - 128;
        int16_t hue_offset = (signed_random * hue_range) / 255;
        uint8_t hue = (uint8_t)(base_hsv.h + (int8_t)hue_offset);
        uint8_t sat = mxfx_random8_min_max(127u, 255u);
        led_matrix_hsv_to_rgb(hue, sat, base_hsv.v, &next_colors[i][0],
                              &next_colors[i][1], &next_colors[i][2]);
      }
    }

    memcpy(colors, next_colors, sizeof(colors));
    wait_until_ms = now_ms + interval;
  }

  for (uint8_t i = 0u; i < LED_MATRIX_NUM_LEDS; i++) {
    led_matrix_set_pixel_idx(i, colors[i][0], colors[i][1], colors[i][2]);
  }
}
