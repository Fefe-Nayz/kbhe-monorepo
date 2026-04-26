static void effect_typing_heatmap(void) {
  uint8_t decay_speed = effect_param(1, 168u);
  uint8_t floor_brightness = effect_param(3, 48u);
  uint32_t now_ms = HAL_GetTick();

  // decay_speed=168 -> ~25 ms interval (QMK baseline), 255 -> 5 ms (fast).
  // decay_speed=0 means freeze (no decay) by design.
  uint32_t decay_ms = 0u;

  if (decay_speed > 0u) {
    decay_ms = (decay_speed >= 255u)
      ? 5u
      : (uint32_t)(5u + ((uint32_t)(255u - decay_speed) * 59u / 255u));
  }

  if (mxfx_typing_heat_decay_timer_ms == 0u) {
    mxfx_typing_heat_decay_timer_ms = now_ms;
  }

  if (decay_ms > 0u) {
    while ((uint32_t)(now_ms - mxfx_typing_heat_decay_timer_ms) >= decay_ms) {
      mxfx_typing_heat_decay_timer_ms += decay_ms;
      for (uint8_t i = 0u; i < LED_MATRIX_NUM_LEDS; i++) {
        if (mxfx_typing_heat_energy[i] > 0u) {
          mxfx_typing_heat_energy[i]--;
        }
      }
    }
  }

  for (uint8_t i = 0u; i < LED_MATRIX_NUM_LEDS; i++) {
    uint8_t val = mxfx_typing_heat_energy[i];
    uint8_t heat_above = (val > 85u) ? (uint8_t)(val - 85u) : 0u;
    uint8_t thermal = (val > 85u) ? 85u : val;
    uint8_t hue = (uint8_t)(170u - heat_above);
    uint8_t bright = mxfx_scale8((uint8_t)(thermal * 3u), 255u);

    // Floor: minimum ambient glow on inactive keys
    if (bright < floor_brightness) {
      bright = floor_brightness;
      hue = 170u;
    }

    uint8_t r = 0u;
    uint8_t g = 0u;
    uint8_t b = 0u;

    led_matrix_hsv_to_rgb(hue, 255u, bright, &r, &g, &b);
    led_matrix_set_pixel_idx(i, r, g, b);
  }
}
