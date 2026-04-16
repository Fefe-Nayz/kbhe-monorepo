static void effect_pixel_fractal(void) {
  enum {
    mxfx_PIXEL_FRACTAL_ROWS = 9u,
    mxfx_PIXEL_FRACTAL_HALF_COLS = 8u,
    mxfx_PIXEL_FRACTAL_COLS = mxfx_PIXEL_FRACTAL_HALF_COLS * 2u,
  };

  static bool led[mxfx_PIXEL_FRACTAL_ROWS][mxfx_PIXEL_FRACTAL_HALF_COLS];
  static uint32_t wait_until_ms = 0u;
  uint8_t sat = effect_param(0, 255u);
  uint8_t val = effect_param(1, 255u);
  led_hsv_t base_hsv = effect_primary_hsv(255u, 0u, 0u);
  uint32_t now_ms = HAL_GetTick();
  uint32_t interval =
      3000u /
      mxfx_scale16by8(mxfx_qadd8(effect_animation_speed(), 16u), 16u);

  if (effect_offset == 0u) {
    memset(led, 0, sizeof(led));
    wait_until_ms = 0u;
  }

  if (now_ms >= wait_until_ms) {
    for (uint8_t row = 0u; row < mxfx_PIXEL_FRACTAL_ROWS; row++) {
      for (uint8_t col = 0u; col < (uint8_t)(mxfx_PIXEL_FRACTAL_HALF_COLS - 1u);
           col++) {
        led[row][col] = led[row][col + 1u];
      }

      led[row][mxfx_PIXEL_FRACTAL_HALF_COLS - 1u] =
          (mxfx_random8() & 3u) == 0u;
    }

    wait_until_ms = now_ms + interval;
  }

  base_hsv.s = sat;
  base_hsv.v = val;

  for (uint8_t i = 0u; i < LED_MATRIX_NUM_LEDS; i++) {
    uint8_t x = mxfx_led_coord_x(i);
    uint8_t y = mxfx_led_coord_y(i);
    uint8_t row = (uint8_t)(((uint16_t)y * (mxfx_PIXEL_FRACTAL_ROWS - 1u)) /
                            mxfx_COORD_MAX_Y);
    uint8_t col = (uint8_t)(((uint16_t)x * (mxfx_PIXEL_FRACTAL_COLS - 1u)) /
                            mxfx_COORD_MAX_X);
    uint8_t mirror_col = col;
    uint8_t r = 0u;
    uint8_t g = 0u;
    uint8_t b = 0u;

    if (mirror_col >= mxfx_PIXEL_FRACTAL_HALF_COLS) {
      mirror_col =
          (uint8_t)((mxfx_PIXEL_FRACTAL_COLS - 1u) - mirror_col);
    }

    if (led[row][mirror_col]) {
      led_matrix_hsv_to_rgb(base_hsv.h, base_hsv.s, base_hsv.v, &r, &g, &b);
    }

    led_matrix_set_pixel_idx(i, r, g, b);
  }
}
