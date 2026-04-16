/* Splash / multi-splash reactive effect.
 *
 * Param 6 (mode) selects the rendering engine:
 *   0 = QMK-style: simple HSV distance fade (the original QMK splash look)
 *   1 = Physics:   ring + trail + core simulation (the old custom reactive look)
 *
 * Shared params (both modes):
 *   0  decay/lifetime – fade speed (72=original QMK rate / ~770ms physics life)
 *   1  spread         – wave radius scale (128=original)
 *   2  base_glow      – ambient floor brightness
 *   3  white_core     – bright burst/core at wave front (bool, default true)
 *   4  gain           – overall brightness scale (224=original)
 *   5  palette        – false=custom hue, true=rainbow angle
 *   6  mode           – 0=QMK, 1=physics (default 0)
 *   LED_EFFECT_PARAM_COLOR_R/G/B – color (default green #00FF60)
 */

/* ─── QMK mode (original splash_math) ─────────────────────────────────────── */

static void effect_splash_math_qmk(const led_hsv_t *base, led_hsv_t *hsv,
                                   int16_t dx, int16_t dy, uint8_t dist,
                                   uint16_t tick) {
  uint8_t decay      = effect_param(0, 72u);
  uint8_t spread     = effect_param(1, 128u);
  uint8_t base_glow  = effect_param(2, 8u);
  bool    white_core = effect_param_flag(3, true);
  uint8_t gain       = effect_param(4, 224u);
  bool    palette    = effect_param_flag(5, false);
  (void)base;

  uint16_t eff_dist = (spread == 0u) ? 255u
                    : (uint16_t)(((uint32_t)dist * 128u) / spread);
  if (eff_dist > 255u) { eff_dist = 255u; }

  if (tick <= (uint16_t)eff_dist) {
    hsv->v = mxfx_qadd8(hsv->v, base_glow);
    return;
  }

  uint16_t age = tick - (uint16_t)eff_dist;

  uint16_t effect_val;
  if (decay == 0u) {
    effect_val = 0u;
  } else {
    effect_val = (uint16_t)(((uint32_t)age * decay) / 72u);
    if (effect_val > 255u) { effect_val = 255u; }
  }

  uint8_t brightness = (uint8_t)(255u - (uint8_t)effect_val);
  brightness = mxfx_scale8(brightness, gain);
  brightness = mxfx_qadd8(brightness, base_glow);

  if (white_core && age < 32u) {
    brightness = mxfx_qadd8(brightness, (uint8_t)(255u - (uint8_t)(age * 8u)));
  }

  if (palette) {
    hsv->h = (uint8_t)(hsv->h + pseudo_angle_u8(dx, dy));
  } else {
    uint8_t hue_shift = (age > 255u) ? 255u : (uint8_t)age;
    hsv->h = (uint8_t)(hsv->h + hue_shift);
  }

  hsv->v = mxfx_qadd8(hsv->v, brightness);
}

static void effect_splash_qmk(uint8_t start_index) {
  uint8_t count = mxfx_last_hit_tracker.count;
  uint8_t begin = (start_index < count) ? start_index : count;
  led_hsv_t base_hsv = effect_primary_hsv(0u, 255u, 96u);
  uint8_t speed_scale = mxfx_qadd8(effect_animation_speed(), 1u);

  for (uint8_t i = 0u; i < LED_MATRIX_NUM_LEDS; i++) {
    led_hsv_t hsv = base_hsv;
    int16_t x = mxfx_led_coord_x(i);
    int16_t y = mxfx_led_coord_y(i);
    hsv.v = 0u;

    for (uint8_t j = begin; j < count; j++) {
      int16_t dx = (int16_t)(x - mxfx_last_hit_tracker.x[j]);
      int16_t dy = (int16_t)(y - mxfx_last_hit_tracker.y[j]);
      uint8_t dist = mxfx_distance_u8(dx, dy);
      uint16_t tick = mxfx_scale16by8(mxfx_last_hit_tracker.tick[j], speed_scale);
      effect_splash_math_qmk(&base_hsv, &hsv, dx, dy, dist, tick);
    }

    hsv.v = mxfx_scale8(hsv.v, base_hsv.v);

    uint8_t r = 0u, g = 0u, b = 0u;
    led_matrix_hsv_to_rgb(hsv.h, hsv.s, hsv.v, &r, &g, &b);
    led_matrix_set_pixel_idx(i, r, g, b);
  }
}

/* ─── Physics mode (ring + trail + core) ──────────────────────────────────── */

static void effect_splash_physics(bool multi) {
  uint16_t accum_r[NUM_KEYS];
  uint16_t accum_g[NUM_KEYS];
  uint16_t accum_b[NUM_KEYS];
  uint8_t  core_peak[NUM_KEYS];

  uint8_t  decay_param  = effect_param(0, 72u);
  uint8_t  spread_param = effect_param(1, 128u);
  uint8_t  base_glow    = effect_param(2, 8u);
  bool     white_core   = effect_param_flag(3, true);
  uint8_t  gain_param   = effect_param(4, 224u);
  bool     rainbow      = effect_param_flag(5, false);

  uint16_t lifetime_ms = (uint16_t)(320u +
      (((uint32_t)(255u - decay_param) * 930u) / 255u));

  uint16_t ring_width   = (uint16_t)(10u + (((uint32_t)spread_param * 12u) / 255u));
  uint16_t trail_length = (uint16_t)(28u + (((uint32_t)spread_param * 52u) / 255u));
  uint16_t core_radius  = (uint16_t)(12u + (((uint32_t)spread_param * 8u)  / 255u));
  uint16_t spread_div   = (uint16_t)(7u  + (((uint32_t)spread_param * 17u) / 255u));
  uint16_t gain         = (uint16_t)(160u + (((uint32_t)gain_param * 160u) / 255u));
  uint16_t wave_speed_q8 =
      (uint16_t)(84u + (((uint32_t)effect_animation_speed() * 172u) / 255u));

  uint8_t anchor_hue = 0u, anchor_sat = 0u, ignored_v = 0u;
  uint8_t base_r = 0u, base_g = 255u, base_b = 96u;
  effect_primary_color(0u, 255u, 96u, &base_r, &base_g, &base_b);
  led_matrix_rgb_to_hsv(base_r, base_g, base_b, &anchor_hue, &anchor_sat, &ignored_v);
  uint8_t ring_sat  = (anchor_sat > 176u) ? anchor_sat : 176u;
  uint8_t trail_sat = (ring_sat > 56u) ? (uint8_t)(ring_sat - 56u) : 120u;

  uint16_t floor_r = (uint16_t)(((uint32_t)base_r * base_glow) / 255u);
  uint16_t floor_g = (uint16_t)(((uint32_t)base_g * base_glow) / 255u);
  uint16_t floor_b = (uint16_t)(((uint32_t)base_b * base_glow) / 255u);

  for (uint8_t i = 0u; i < NUM_KEYS; i++) {
    accum_r[i]   = floor_r;
    accum_g[i]   = floor_g;
    accum_b[i]   = floor_b;
    core_peak[i] = 0u;
  }

  uint8_t youngest_slot = 0xFFu;
  if (!multi) {
    uint16_t youngest_age = 0xFFFFu;
    for (uint8_t w = 0u; w < REACTIVE_WAVE_POOL_SIZE; w++) {
      if (reactive_waves[w].active && reactive_waves[w].age_ms < youngest_age) {
        youngest_age  = reactive_waves[w].age_ms;
        youngest_slot = w;
      }
    }
  }

  for (uint8_t wi = 0u; wi < REACTIVE_WAVE_POOL_SIZE; wi++) {
    reactive_wave_t *wave = &reactive_waves[wi];
    if (!wave->active || wave->source_key >= NUM_KEYS) { continue; }
    if (!multi && wi != youngest_slot)                 { continue; }

    uint8_t  src    = wave->source_key;
    uint16_t age_ms = wave->age_ms;
    if (age_ms >= lifetime_ms) { continue; }

    uint8_t energy = (uint8_t)(((uint32_t)(lifetime_ms - age_ms) * 255u) / lifetime_ms);
    if (energy == 0u) { continue; }

    uint16_t front = (uint16_t)(((uint32_t)age_ms * wave_speed_q8) >> 8u);

    for (uint8_t li = 0u; li < NUM_KEYS; li++) {
      uint8_t ring_hue;
      if (rainbow) {
        int16_t dx = (int16_t)led_pos_x[li] - (int16_t)led_pos_x[src];
        int16_t dy = (int16_t)led_pos_y[li] - (int16_t)led_pos_y[src];
        ring_hue = (uint8_t)(pseudo_angle_u8(dx, dy) + effect_offset);
      } else {
        ring_hue = anchor_hue;
      }
      uint8_t trail_hue = (uint8_t)(ring_hue + 18u);

      uint8_t rr = 0u, rg = 0u, rb = 0u;
      uint8_t tr = 0u, tg = 0u, tb = 0u;
      led_matrix_hsv_to_rgb(ring_hue,  ring_sat,  255u, &rr, &rg, &rb);
      led_matrix_hsv_to_rgb(trail_hue, trail_sat, 255u, &tr, &tg, &tb);

      uint16_t distance   = led_layout_distance(li, src) / spread_div;
      uint16_t ring_delta = (front > distance) ? (front - distance) : (distance - front);

      uint16_t ring_raw = 0u, trail_raw = 0u, core_raw = 0u;

      if (ring_delta < ring_width) {
        ring_raw = ((ring_width - ring_delta) * energy) / ring_width;
      }
      if (front >= distance) {
        uint16_t td = front - distance;
        if (td < trail_length) {
          trail_raw = ((trail_length - td) * energy) / trail_length;
        }
      }
      if (distance < core_radius && age_ms < 160u) {
        uint16_t spatial  = ((core_radius - distance) * 255u) / core_radius;
        uint16_t temporal = 255u - (uint16_t)(((uint32_t)age_ms * 255u) / 160u);
        core_raw = (spatial * temporal) / 255u;
      }

      uint8_t rv  = clamp_u16_to_u8((uint16_t)(((uint32_t)ring_raw  * gain)        / 255u));
      uint8_t tv  = clamp_u16_to_u8((uint16_t)(((uint32_t)trail_raw * (gain - 48u)) / 255u));
      uint8_t cv  = clamp_u16_to_u8((uint16_t)(((uint32_t)core_raw  * (gain + 32u)) / 255u));

      if (rv > 0u) { add_scaled_rgb(&accum_r[li], &accum_g[li], &accum_b[li], rr, rg, rb, rv); }
      if (tv > 0u) { add_scaled_rgb(&accum_r[li], &accum_g[li], &accum_b[li], tr, tg, tb, tv); }
      if (cv > 0u) {
        add_scaled_rgb(&accum_r[li], &accum_g[li], &accum_b[li],
                       effect_param(LED_EFFECT_PARAM_COLOR_R, 0u),
                       effect_param(LED_EFFECT_PARAM_COLOR_G, 255u),
                       effect_param(LED_EFFECT_PARAM_COLOR_B, 96u), cv);
        if (cv > core_peak[li]) { core_peak[li] = cv; }
      }
    }
  }

  for (uint8_t li = 0u; li < NUM_KEYS; li++) {
    uint8_t r = clamp_u16_to_u8(accum_r[li]);
    uint8_t g = clamp_u16_to_u8(accum_g[li]);
    uint8_t b = clamp_u16_to_u8(accum_b[li]);
    if (white_core && core_peak[li] > 96u) {
      uint8_t wm = (uint8_t)(((uint16_t)(core_peak[li] - 96u) * 255u) / 159u);
      mix_rgb_towards_white(&r, &g, &b, wm);
    }
    led_matrix_set_pixel_idx(li, r, g, b);
  }
}

/* ─── Entry points ─────────────────────────────────────────────────────────── */

static void effect_splash(void) {
  bool physics = effect_param_flag(6, false);
  if (physics) {
    effect_splash_physics(false);
  } else {
    uint8_t count = mxfx_last_hit_tracker.count;
    uint8_t start = (count > 0u) ? (uint8_t)(count - 1u) : 0u;
    effect_splash_qmk(start);
  }
}

static void effect_multi_splash(void) {
  bool physics = effect_param_flag(6, false);
  if (physics) {
    effect_splash_physics(true);
  } else {
    effect_splash_qmk(0u);
  }
}
