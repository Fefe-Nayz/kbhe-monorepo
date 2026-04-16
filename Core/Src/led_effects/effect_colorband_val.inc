static void effect_colorband_val(void) {
  uint8_t angle = effect_param(0, 0u);
  bool value_mode = effect_param_flag(1, true);
  led_hsv_t base_hsv = effect_primary_hsv(255u, 0u, 0u);
  uint8_t time = mxfx_runner_time_i();
  int16_t dir_x = (int16_t)mxfx_cos8(angle) - 128;
  int16_t dir_y = (int16_t)mxfx_sin8(angle) - 128;

  for (uint8_t i = 0u; i < LED_MATRIX_NUM_LEDS; i++) {
    int16_t x = (int16_t)mxfx_led_coord_x(i) - (int16_t)mxfx_CENTER_X;
    int16_t y = (int16_t)mxfx_led_coord_y(i) - (int16_t)mxfx_CENTER_Y;
    uint8_t axis = mxfx_project_axis_u8(x, y, dir_x, dir_y);
    uint8_t band_pos = (uint8_t)(mxfx_scale8(axis, 228u) + 28u);
    uint8_t r = 0u;
    uint8_t g = 0u;
    uint8_t b = 0u;
    uint8_t base_channel = value_mode ? base_hsv.v : base_hsv.s;
    int16_t channel =
        (int16_t)base_channel - (int16_t)abs_u8_diff(band_pos, time) * 8;
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
