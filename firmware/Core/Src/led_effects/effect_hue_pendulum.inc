static void effect_hue_pendulum(void) {
  uint8_t swing = effect_param(0, 12u);     // Hue swing range
  uint8_t angle = effect_param(1, 0u);      // Motion axis angle (0=horizontal)
  led_hsv_t base_hsv = effect_primary_hsv(255u, 0u, 0u);
  uint8_t time = mxfx_runner_time_i();
  uint8_t wave = mxfx_sin8(time);
  int16_t dir_x = (int16_t)mxfx_cos8(angle) - 128;
  int16_t dir_y = (int16_t)mxfx_sin8(angle) - 128;

  for (uint8_t i = 0u; i < LED_MATRIX_NUM_LEDS; i++) {
    int16_t x = (int16_t)mxfx_led_coord_x(i) - (int16_t)mxfx_CENTER_X;
    int16_t y = (int16_t)mxfx_led_coord_y(i) - (int16_t)mxfx_CENTER_Y;
    int16_t projection =
        (int16_t)(((int32_t)x * dir_x + (int32_t)y * dir_y) >> 7) + 128;
    uint16_t pendulum;
    uint8_t r = 0u;
    uint8_t g = 0u;
    uint8_t b = 0u;
    led_hsv_t hsv = base_hsv;

    if (projection < 0) {
      projection = 0;
    } else if (projection > 255) {
      projection = 255;
    }

    pendulum = (uint16_t)abs_u8_diff((uint8_t)projection, wave) << 1u;
    if (pendulum > 255u) {
      pendulum = 255u;
    }

    hsv.h = (uint8_t)(hsv.h + mxfx_scale8((uint8_t)pendulum, swing));
    led_matrix_hsv_to_rgb(hsv.h, hsv.s, hsv.v, &r, &g, &b);
    led_matrix_set_pixel_idx(i, r, g, b);
  }
}
