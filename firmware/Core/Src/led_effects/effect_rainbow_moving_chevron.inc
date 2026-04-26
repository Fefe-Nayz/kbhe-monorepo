static void effect_rainbow_moving_chevron(void) {
  uint8_t size = effect_param(0, 128u);   // Chevron arm size/intensity
  uint8_t angle = effect_param(1, 0u);    // Travel axis angle
  bool reverse = effect_param_flag(2, false);

  // Legacy profiles had these newly-introduced slots at zero.
  if (size == 0u && angle == 0u && !reverse) {
    size = 128u;
  }

  led_hsv_t base_hsv = effect_primary_hsv(255u, 0u, 0u);
  uint8_t time = mxfx_runner_time_i();
  int16_t dir_x = (int16_t)mxfx_cos8(angle) - 128;
  int16_t dir_y = (int16_t)mxfx_sin8(angle) - 128;

  for (uint8_t i = 0u; i < LED_MATRIX_NUM_LEDS; i++) {
    int16_t x = (int16_t)mxfx_led_coord_x(i) - (int16_t)mxfx_CENTER_X;
    int16_t y = (int16_t)mxfx_led_coord_y(i) - (int16_t)mxfx_CENTER_Y;
    int16_t along =
        (int16_t)(((int32_t)x * dir_x + (int32_t)y * dir_y) >> 7) + 128;
    int16_t across =
        (int16_t)(((int32_t)x * (-(int32_t)dir_y) + (int32_t)y * dir_x) >> 7) +
        128;
    uint16_t chevron;
    uint8_t phase;
    uint8_t r = 0u;
    uint8_t g = 0u;
    uint8_t b = 0u;
    led_hsv_t hsv = base_hsv;

    if (along < 0) {
      along = 0;
    } else if (along > 255) {
      along = 255;
    }

    if (across < 0) {
      across = 0;
    } else if (across > 255) {
      across = 255;
    }

    chevron = ((uint16_t)abs_u8_diff((uint8_t)across, 128u) * size) >> 7u;
    if (chevron > 255u) {
      chevron = 255u;
    }

    phase = reverse ? (uint8_t)(time - (uint8_t)along)
                    : (uint8_t)((uint8_t)along - time);
    hsv.h = (uint8_t)(hsv.h + phase + (uint8_t)chevron);
    led_matrix_hsv_to_rgb(hsv.h, hsv.s, hsv.v, &r, &g, &b);
    led_matrix_set_pixel_idx(i, r, g, b);
  }
}
