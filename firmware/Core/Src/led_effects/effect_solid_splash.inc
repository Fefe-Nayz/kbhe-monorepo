static void effect_solid_splash_math(const led_hsv_t *base,
                                         led_hsv_t *hsv, int16_t dx,
                                         int16_t dy, uint8_t dist,
                                         uint16_t tick) {
  uint16_t effect = (uint16_t)(tick - dist);
  (void)base;
  (void)dx;
  (void)dy;

  if (effect > 255u) {
    effect = 255u;
  }

  hsv->v = mxfx_qadd8(hsv->v, (uint8_t)(255u - effect));
}

static void effect_solid_splash_render_random(bool multi) {
  uint16_t accum_r[NUM_KEYS];
  uint16_t accum_g[NUM_KEYS];
  uint16_t accum_b[NUM_KEYS];
  uint8_t base_r = 255u;
  uint8_t base_g = 0u;
  uint8_t base_b = 0u;
  uint8_t base_h = 0u;
  uint8_t base_s = 255u;
  uint8_t base_v = 255u;
  uint8_t speed_scale = mxfx_qadd8(effect_animation_speed(), 1u);
  uint8_t youngest_slot = 0xFFu;

  effect_primary_color(255u, 0u, 0u, &base_r, &base_g, &base_b);
  led_matrix_rgb_to_hsv(base_r, base_g, base_b, &base_h, &base_s, &base_v);
  (void)base_h;

  if (!multi) {
    uint16_t youngest_age = 0xFFFFu;
    for (uint8_t wi = 0u; wi < REACTIVE_WAVE_POOL_SIZE; wi++) {
      if (reactive_waves[wi].active && reactive_waves[wi].age_ms < youngest_age) {
        youngest_age = reactive_waves[wi].age_ms;
        youngest_slot = wi;
      }
    }
  }

  for (uint8_t li = 0u; li < NUM_KEYS; li++) {
    accum_r[li] = 0u;
    accum_g[li] = 0u;
    accum_b[li] = 0u;
  }

  for (uint8_t wi = 0u; wi < REACTIVE_WAVE_POOL_SIZE; wi++) {
    reactive_wave_t *wave = &reactive_waves[wi];
    if (!wave->active || wave->source_key >= NUM_KEYS) {
      continue;
    }
    if (!multi && wi != youngest_slot) {
      continue;
    }

    uint16_t tick = mxfx_scale16by8(wave->age_ms, speed_scale);
    uint8_t wave_hue = wave->hue;

    for (uint8_t li = 0u; li < NUM_KEYS; li++) {
      int16_t dx = (int16_t)mxfx_led_coord_x(li) -
                   (int16_t)mxfx_led_coord_x(wave->source_key);
      int16_t dy = (int16_t)mxfx_led_coord_y(li) -
                   (int16_t)mxfx_led_coord_y(wave->source_key);
      uint8_t dist = mxfx_distance_u8(dx, dy);
      uint16_t effect = (tick > dist) ? (uint16_t)(tick - dist) : 255u;
      uint8_t value;
      uint8_t r = 0u;
      uint8_t g = 0u;
      uint8_t b = 0u;

      if (effect > 255u) {
        effect = 255u;
      }

      value = (uint8_t)(255u - effect);
      if (value == 0u) {
        continue;
      }

      led_matrix_hsv_to_rgb(wave_hue, base_s, value, &r, &g, &b);
      add_scaled_rgb(&accum_r[li], &accum_g[li], &accum_b[li], r, g, b, base_v);
    }
  }

  for (uint8_t li = 0u; li < NUM_KEYS; li++) {
    led_matrix_set_pixel_idx(li, clamp_u16_to_u8(accum_r[li]),
                             clamp_u16_to_u8(accum_g[li]),
                             clamp_u16_to_u8(accum_b[li]));
  }
}

static void effect_solid_splash(void) {
  bool multi = effect_param_flag(1u, false);

  if (effect_param_flag(0u, false)) {
    effect_solid_splash_render_random(multi);
    return;
  }

  {
    uint8_t start = (multi || mxfx_last_hit_tracker.count == 0u)
                        ? 0u
                        : (uint8_t)(mxfx_last_hit_tracker.count - 1u);
    mxfx_reactive_splash_render(start, effect_solid_splash_math);
  }
}

static void effect_solid_multi_splash(void) {
  bool multi = effect_param_flag(1u, true);

  if (effect_param_flag(0u, false)) {
    effect_solid_splash_render_random(multi);
    return;
  }

  {
    uint8_t start = (multi || mxfx_last_hit_tracker.count == 0u)
                        ? 0u
                        : (uint8_t)(mxfx_last_hit_tracker.count - 1u);
    mxfx_reactive_splash_render(start, effect_solid_splash_math);
  }
}
