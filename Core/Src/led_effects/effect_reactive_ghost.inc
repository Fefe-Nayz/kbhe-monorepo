static void effect_reactive_ghost(void) {
  uint16_t accum_r[NUM_KEYS] = {0};
  uint16_t accum_g[NUM_KEYS] = {0};
  uint16_t accum_b[NUM_KEYS] = {0};
  /* param[0]: decay — scales how aggressively energy is compressed before
   * rendering, giving a tunable visual fade speed independent of sim rate */
  uint8_t decay = effect_param(0, 100u);
  uint8_t spread = effect_param(1, 156u);
  uint8_t trail = effect_param(2, 184u);
  uint8_t gain = effect_param(3, 208u);
  uint16_t spread_div = (uint16_t)(8u + ((uint16_t)spread * 12u) / 255u);
  uint16_t trail_len = (uint16_t)(24u + ((uint16_t)trail * 80u) / 255u);
  /* decay_shift: 0-4 right-shifts applied to ghost energy before rendering
   * so high decay = more compressed brightness range = visually faster fade */
  uint8_t decay_shift = (uint8_t)(((uint16_t)(255u - decay) * 4u) / 255u);
  uint8_t ghost_r = 72u;
  uint8_t ghost_g = 180u;
  uint8_t ghost_b = 255u;

  effect_primary_color(72u, 180u, 255u, &ghost_r, &ghost_g, &ghost_b);

  for (uint8_t source = 0u; source < NUM_KEYS; source++) {
    uint16_t energy = reactive_ghost_energy[source];
    if (energy == 0u) {
      continue;
    }

    /* Apply decay param: right-shift energy to reduce its effective range,
     * making higher-decay values produce visually faster fade. */
    uint16_t visible_energy = (uint16_t)(energy >> decay_shift);
    if (visible_energy == 0u) {
      continue;
    }

    for (uint8_t led_idx = 0u; led_idx < NUM_KEYS; led_idx++) {
      uint16_t distance = led_layout_distance(led_idx, source) / spread_div;
      if (distance >= trail_len) {
        continue;
      }

      uint16_t falloff =
          (uint16_t)(((uint32_t)(trail_len - distance) * visible_energy) / trail_len);
      uint8_t intensity =
          clamp_u16_to_u8((uint16_t)(((uint32_t)falloff * gain) / 255u));
      add_scaled_rgb(&accum_r[led_idx], &accum_g[led_idx], &accum_b[led_idx],
                     ghost_r, ghost_g, ghost_b, intensity);
    }
  }

  for (uint8_t led_idx = 0u; led_idx < NUM_KEYS; led_idx++) {
    led_matrix_set_pixel_idx(led_idx, clamp_u16_to_u8(accum_r[led_idx]),
                             clamp_u16_to_u8(accum_g[led_idx]),
                             clamp_u16_to_u8(accum_b[led_idx]));
  }
}


