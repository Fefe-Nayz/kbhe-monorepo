static void effect_colorband_pinwheel_val(void) {
  uint8_t center_x = effect_param_coord(0, mxfx_COORD_MAX_X, 128u);
  uint8_t center_y = effect_param_coord(1, mxfx_COORD_MAX_Y, 128u);
  bool reverse = effect_param_flag(2, false);
  bool value_mode = effect_param_flag(3, true);
  led_hsv_t base_hsv = effect_primary_hsv(255u, 0u, 0u);
  uint8_t time = mxfx_runner_time_dx_dy();
  uint8_t phase = reverse ? (uint8_t)(0u - time) : time;

  if (center_x == 0u && center_y == 0u && !reverse) {
    center_x = mxfx_CENTER_X;
    center_y = mxfx_CENTER_Y;
  }

  for (uint8_t i = 0u; i < LED_MATRIX_NUM_LEDS; i++) {
    int16_t dx = (int16_t)mxfx_led_coord_x(i) - (int16_t)center_x;
    int16_t dy = (int16_t)mxfx_led_coord_y(i) - (int16_t)center_y;
    uint8_t band_pos = (uint8_t)(pseudo_angle_u8(dx, dy) * 3u);
    uint8_t r = 0u;
    uint8_t g = 0u;
    uint8_t b = 0u;
    uint8_t base_channel = value_mode ? base_hsv.v : base_hsv.s;
    int16_t channel =
        (int16_t)base_channel - (int16_t)abs_u8_diff(band_pos, phase) * 8;
    led_hsv_t hsv = base_hsv;

    if (value_mode) {
      hsv.v = mxfx_scale8((uint8_t)((channel < 0) ? 0 : channel), base_hsv.v);
    } else {
      hsv.s = mxfx_scale8((uint8_t)((channel < 0) ? 0 : channel), base_hsv.s);
    }
    led_matrix_hsv_to_rgb(hsv.h, hsv.s, hsv.v, &r, &g, &b);
    led_matrix_set_pixel_idx(i, r, g, b);
  }
}
