static void effect_cycle_out_in_dual(void) {
  uint8_t spread = effect_param(0, 224u);   // Radial hue spread
  uint8_t center1_x = effect_param_coord(1, mxfx_COORD_MAX_X, 64u);  // Center 1 X
  uint8_t center1_y = effect_param_coord(2, mxfx_COORD_MAX_Y, 128u); // Center 1 Y
  uint8_t center2_x = effect_param_coord(3, mxfx_COORD_MAX_X, 192u); // Center 2 X
  uint8_t center2_y = effect_param_coord(4, mxfx_COORD_MAX_Y, 128u); // Center 2 Y
  bool reverse_1 = effect_param_flag(5, false); // Center 1 flow direction
  bool reverse_2 = effect_param_flag(6, false); // Center 2 flow direction

  // Legacy profiles had these newly-introduced slots at zero.
  if (spread == 0u && center1_x == 0u && center1_y == 0u && center2_x == 0u &&
      center2_y == 0u) {
    spread = 224u;
    center1_x = mxfx_COORD_MAX_X / 4u;
    center1_y = mxfx_CENTER_Y;
    center2_x = (uint8_t)((mxfx_COORD_MAX_X * 3u) / 4u);
    center2_y = mxfx_CENTER_Y;
  }

  led_hsv_t base_hsv = effect_primary_hsv(255u, 0u, 0u);
  uint8_t time = mxfx_runner_time_dx_dy();

  for (uint8_t i = 0u; i < LED_MATRIX_NUM_LEDS; i++) {
    int16_t x = (int16_t)mxfx_led_coord_x(i);
    int16_t y = (int16_t)mxfx_led_coord_y(i);
    uint8_t dist1 = mxfx_distance_u8((int16_t)(x - (int16_t)center1_x),
                                     (int16_t)(y - (int16_t)center1_y));
    uint8_t dist2 = mxfx_distance_u8((int16_t)(x - (int16_t)center2_x),
                                     (int16_t)(y - (int16_t)center2_y));
    bool use_1 = dist1 <= dist2;
    bool reverse = use_1 ? reverse_1 : reverse_2;
    uint8_t dist = use_1 ? dist1 : dist2;
    uint16_t radial = ((uint16_t)dist * spread) >> 7u;
    uint8_t radial_phase;
    uint8_t r = 0u;
    uint8_t g = 0u;
    uint8_t b = 0u;
    led_hsv_t hsv = base_hsv;

    if (radial > 255u) {
      radial = 255u;
    }

    radial_phase = reverse ? (uint8_t)(0u - (uint8_t)radial)
                           : (uint8_t)radial;
    hsv.h = (uint8_t)(hsv.h + radial_phase + time);
    led_matrix_hsv_to_rgb(hsv.h, hsv.s, hsv.v, &r, &g, &b);
    led_matrix_set_pixel_idx(i, r, g, b);
  }
}
