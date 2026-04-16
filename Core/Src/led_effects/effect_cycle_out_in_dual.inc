static void effect_cycle_out_in_dual(void) {
  led_hsv_t base_hsv = effect_primary_hsv(255u, 0u, 0u);
  uint8_t time = mxfx_runner_time_dx_dy();

  for (uint8_t i = 0u; i < LED_MATRIX_NUM_LEDS; i++) {
    int16_t dx = (int16_t)mxfx_led_coord_x(i) - (int16_t)mxfx_CENTER_X;
    int16_t dy = (int16_t)mxfx_led_coord_y(i) - (int16_t)mxfx_CENTER_Y;
    int16_t dual_dx = (int16_t)(mxfx_CENTER_X / 2u) -
                      (int16_t)abs_u8_diff((uint8_t)(dx + mxfx_CENTER_X),
                                           mxfx_CENTER_X);
    uint8_t dist = mxfx_distance_u8(dual_dx, dy);
    uint8_t r = 0u;
    uint8_t g = 0u;
    uint8_t b = 0u;
    led_hsv_t hsv = base_hsv;

    hsv.h = (uint8_t)(3u * dist + time);
    led_matrix_hsv_to_rgb(hsv.h, hsv.s, hsv.v, &r, &g, &b);
    led_matrix_set_pixel_idx(i, r, g, b);
  }
}
