static void effect_cycle_left_right(void) {
  uint8_t h_scale   = effect_param(0, 160u);  // Horizontal hue spread
  uint8_t v_scale   = effect_param(1, 96u);   // Vertical hue contribution
  /* param 2 reserved (was drift speed — removed to fix animation jump on wrap) */
  uint8_t sat       = effect_param(3, 255u);  // Saturation
  uint8_t angle     = effect_param(4, 0u);    // Gradient tilt (signed shear)
  uint8_t curvature = effect_param(5, 0u);    // Sinusoidal warp per column

  /* Use time directly so drift_offset cycles 0→255→0 without jumps.
   * The old formula (time * drift) >> 8 only cycled 0→(drift-1) causing a
   * visible hue jump each time the uint8 time counter wrapped. */
  uint8_t time         = mxfx_runner_time_i();
  uint8_t drift_offset = time;

  for (uint8_t i = 0u; i < LED_MATRIX_NUM_LEDS; i++) {
    uint8_t x = mxfx_led_coord_x(i);
    uint8_t y = mxfx_led_coord_y(i);

    // Optional tilt: rotate hue axis by shearing X with a Y component
    int16_t rx = (int16_t)x + (((int16_t)(int8_t)angle * (int16_t)y) >> 7);
    if (rx < 0) { rx = 0; }
    if (rx > 255) { rx = 255; }

    uint8_t hue = (uint8_t)(((uint16_t)(uint8_t)rx * h_scale) >> 8u);
    hue = (uint8_t)(hue + (((uint16_t)y * v_scale) >> 8u));
    if (curvature > 0u) {
      hue = (uint8_t)(hue + (mxfx_scale8(mxfx_sin8(x), curvature) >> 1u));
    }
    hue = (uint8_t)(hue - drift_offset);

    uint8_t r = 0u;
    uint8_t g = 0u;
    uint8_t b = 0u;
    led_matrix_hsv_to_rgb(hue, sat, 255u, &r, &g, &b);
    led_matrix_set_pixel_idx(i, r, g, b);
  }
}
