static void effect_rainbow_pinwheels(void) {
  uint8_t center1_x = effect_param_coord(0, mxfx_COORD_MAX_X, 64u);
  uint8_t center1_y = effect_param_coord(1, mxfx_COORD_MAX_Y, 128u);
  uint8_t center2_x = effect_param_coord(2, mxfx_COORD_MAX_X, 192u);
  uint8_t center2_y = effect_param_coord(3, mxfx_COORD_MAX_Y, 128u);
  bool reverse_1 = effect_param_flag(4, false);
  bool reverse_2 = effect_param_flag(5, false);
  led_hsv_t base_hsv = effect_primary_hsv(255u, 0u, 0u);
  uint8_t time = mxfx_runner_time_sin_cos();
  uint8_t phase_1 = reverse_1 ? (uint8_t)(0u - time) : time;
  uint8_t phase_2 = reverse_2 ? (uint8_t)(0u - time) : time;
  int8_t cos_1 = (int8_t)mxfx_cos8(phase_1) - 128;
  int8_t sin_1 = (int8_t)mxfx_sin8(phase_1) - 128;
  int8_t cos_2 = (int8_t)mxfx_cos8(phase_2) - 128;
  int8_t sin_2 = (int8_t)mxfx_sin8(phase_2) - 128;

  if (center1_x == 0u && center1_y == 0u && center2_x == 0u &&
      center2_y == 0u && !reverse_1 && !reverse_2) {
    center1_x = mxfx_COORD_MAX_X / 4u;
    center1_y = mxfx_CENTER_Y;
    center2_x = (uint8_t)((mxfx_COORD_MAX_X * 3u) / 4u);
    center2_y = mxfx_CENTER_Y;
  }

  for (uint8_t i = 0u; i < LED_MATRIX_NUM_LEDS; i++) {
    int16_t x = (int16_t)mxfx_led_coord_x(i);
    int16_t y = (int16_t)mxfx_led_coord_y(i);
    int16_t dx1 = x - (int16_t)center1_x;
    int16_t dy1 = y - (int16_t)center1_y;
    int16_t dx2 = x - (int16_t)center2_x;
    int16_t dy2 = y - (int16_t)center2_y;
    uint8_t dist1 = mxfx_distance_u8(dx1, dy1);
    uint8_t dist2 = mxfx_distance_u8(dx2, dy2);
    int16_t arm1 = 56 - (int16_t)abs_u8_diff((uint8_t)x, center1_x);
    int16_t arm2 = 56 - (int16_t)abs_u8_diff((uint8_t)x, center2_x);
    int16_t term1 =
      (int16_t)(((dy1 * 3 * cos_1) + (arm1 * 3 * sin_1)) / 128) +
      (int16_t)phase_1;
    int16_t term2 =
      (int16_t)(((dy2 * 3 * cos_2) + (arm2 * 3 * sin_2)) / 128) +
      (int16_t)phase_2;
    int16_t selected = (dist1 <= dist2) ? term1 : term2;
    uint8_t r = 0u;
    uint8_t g = 0u;
    uint8_t b = 0u;
    led_hsv_t hsv = base_hsv;

    hsv.h = (uint8_t)(hsv.h + selected);

    led_matrix_hsv_to_rgb(hsv.h, hsv.s, hsv.v, &r, &g, &b);
    led_matrix_set_pixel_idx(i, r, g, b);
  }
}
