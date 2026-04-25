static void effect_cycle_out_in(void) {
  uint8_t spread = effect_param(0, 192u);   // Radial hue spread
  uint8_t center_x = effect_param_coord(1, mxfx_COORD_MAX_X, 128u); // Center X
  uint8_t center_y = effect_param_coord(2, mxfx_COORD_MAX_Y, 128u); // Center Y
  bool reverse = effect_param_flag(3, false); // Flow direction

  // Legacy profiles had these newly-introduced slots at zero.
  if (spread == 0u && center_x == 0u && center_y == 0u) {
    spread = 192u;
    center_x = mxfx_CENTER_X;
    center_y = mxfx_CENTER_Y;
  }

  led_hsv_t base_hsv = effect_primary_hsv(255u, 0u, 0u);
  uint8_t time = mxfx_runner_time_dx_dy();

  for (uint8_t i = 0u; i < LED_MATRIX_NUM_LEDS; i++) {
    int16_t dx = (int16_t)mxfx_led_coord_x(i) - (int16_t)center_x;
    int16_t dy = (int16_t)mxfx_led_coord_y(i) - (int16_t)center_y;
    uint8_t dist = mxfx_distance_u8(dx, dy);
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
