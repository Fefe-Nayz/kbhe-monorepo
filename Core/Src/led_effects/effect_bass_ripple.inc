/* Bass-ripple effect — audio-triggered expanding rings.
 *
 * Renders like splash/multi-splash (physics mode) but triggered by bass
 * transients from the audio spectrum data instead of key presses.  Up to
 * BASS_RIPPLE_POOL_SIZE rings can coexist simultaneously.  Rings always
 * originate from the physical centre of the keyboard.
 *
 * Params (deliberately identical to splash so the same schema is re-usable):
 *   0  decay / lifetime  (72 = default, higher = shorter life)
 *   1  spread            (128 = default ring & trail scale)
 *   2  base_glow         (ambient floor brightness, 0 = default off)
 *   3  white_core        (bright core burst, bool, default true)
 *   4  gain              (overall brightness, 224 = default)
 *   5  rainbow           (cycle hue each new wave, bool, default false)
 *   6  bass_sensitivity  (trigger threshold, 128 = medium)
 *   7  random_per_beat   (random hue each beat, bool, default false)
 *   LED_EFFECT_PARAM_COLOR_R/G/B  colour
 *   LED_EFFECT_PARAM_SPEED        ring expansion speed
 */

#define BASS_RIPPLE_POOL_SIZE 8u

typedef struct {
  bool     active;
  uint32_t spawn_ms;
  uint8_t  hue;
  uint8_t  energy;
} bass_ripple_wave_t;

static bass_ripple_wave_t bass_ripple_waves[BASS_RIPPLE_POOL_SIZE];
static uint8_t  bass_ripple_env        = 0u;
static uint8_t  bass_ripple_hue_idx   = 0u;
static uint32_t bass_ripple_last_spawn = 0u;
static uint8_t  bass_ripple_prev_level = 0u;
static bool     bass_ripple_inited    = false;

/* Chebyshev distance from physical keyboard centre to key li. */
static inline uint16_t bass_ripple_center_dist(uint8_t li) {
  uint16_t cx = LED_LAYOUT_MAX_X / 2u;
  uint16_t cy = LED_LAYOUT_MAX_Y / 2u;
  uint16_t dx = (led_pos_x[li] > cx) ? (led_pos_x[li] - cx) : (cx - led_pos_x[li]);
  uint16_t dy = (led_pos_y[li] > cy) ? (led_pos_y[li] - cy) : (cy - led_pos_y[li]);
  uint16_t major = (dx > dy) ? dx : dy;
  uint16_t minor = (dx > dy) ? dy : dx;
  return (uint16_t)(major + minor / 2u);
}

static void effect_bass_ripple(void) {
  uint8_t  decay_param  = effect_param(0, 72u);
  uint8_t  spread_param = effect_param(1, 128u);
  uint8_t  base_glow    = effect_param(2, 0u);
  bool     white_core   = effect_param_flag(3, true);
  uint8_t  gain_param   = effect_param(4, 224u);
  bool     rainbow      = effect_param_flag(5, false);
  uint8_t  sensitivity  = effect_param(6, 128u);
  bool     random_per_beat = effect_param_flag(7, false);

  if (!bass_ripple_inited || effect_offset == 0u) {
    memset(bass_ripple_waves, 0, sizeof(bass_ripple_waves));
    bass_ripple_env        = 0u;
    bass_ripple_hue_idx    = 0u;
    bass_ripple_last_spawn = 0u;
    bass_ripple_prev_level = 0u;
    bass_ripple_inited     = true;
  }

  uint32_t now_ms = HAL_GetTick();

  /* ── Bass detection & wave spawning ─────────────────────────────────── */
  if (audio_spectrum_valid) {
    /* Weighted low-band sum (sub-bass → low-mid) used as fallback/context. */
    uint16_t bass_energy = 0u;
    for (uint8_t band = 0u; band < 5u; band++) {
      uint8_t w = (band == 0u) ? 5u :
                  ((band == 1u) ? 4u :
                  ((band == 2u) ? 3u :
                  ((band == 3u) ? 2u : 1u)));
      bass_energy = (uint16_t)(bass_energy + (uint16_t)audio_spectrum_levels[band] * w);
    }
    uint8_t bass_drive = (uint8_t)(bass_energy / 15u);
    uint8_t drive = bass_drive;
    uint8_t env_rise = 0u;
    uint8_t trigger_threshold;
    uint8_t rise_gate;
    uint8_t combined_level;
    uint8_t combined_rise;

    /* Host now sends analog bass intensity in audio_impact_level. */
    if (audio_impact_level > drive) {
      drive = audio_impact_level;
    }

    /* Envelope follower: fast attack, slower release for stable thresholding. */
    uint8_t prev_env = bass_ripple_env;
    if (drive > bass_ripple_env) {
      uint8_t rise = (uint8_t)(4u + ((uint16_t)(drive - bass_ripple_env) * 132u) / 255u);
      uint16_t next = (uint16_t)bass_ripple_env + rise;
      bass_ripple_env = (next > drive) ? drive : (uint8_t)next;
    } else {
      bass_ripple_env = (bass_ripple_env > 2u) ? (uint8_t)(bass_ripple_env - 2u) : 0u;
    }

    env_rise = (bass_ripple_env > prev_env)
                   ? (uint8_t)(bass_ripple_env - prev_env)
                   : 0u;

    combined_level = (bass_ripple_env > audio_impact_level)
               ? bass_ripple_env
               : audio_impact_level;
    combined_rise = (combined_level > bass_ripple_prev_level)
              ? (uint8_t)(combined_level - bass_ripple_prev_level)
              : 0u;

    /* Sensitivity controls threshold directly: higher sensitivity -> lower threshold. */
    trigger_threshold =
      (uint8_t)(12u + (((uint16_t)(255u - sensitivity) * 208u) / 255u));
    rise_gate = (uint8_t)(1u + (((uint16_t)(255u - sensitivity) * 20u) / 255u));

    bool kick_trigger = (combined_level >= trigger_threshold) &&
              (combined_rise >= rise_gate);

    /* Debounce: at most one new wave per 80 ms. */
    if (kick_trigger &&
        (uint32_t)(now_ms - bass_ripple_last_spawn) >= 80u) {
      uint8_t overshoot = (combined_level > trigger_threshold)
              ? (uint8_t)(combined_level - trigger_threshold)
                              : 0u;
      uint16_t strength = (uint16_t)combined_level * 2u +
                          (uint16_t)overshoot * 2u +
                          (uint16_t)env_rise * 4u +
                          (uint16_t)combined_rise * 8u;
      uint16_t wave_energy_u16 = (uint16_t)(110u + (strength / 2u));
      uint8_t wave_energy = (wave_energy_u16 > 255u)
                ? 255u
                : (uint8_t)wave_energy_u16;

      /* Find a free slot; evict the oldest active wave if full. */
      uint8_t  slot       = 0u;
      bool     found      = false;
      uint32_t oldest_age = 0u;

      for (uint8_t i = 0u; i < BASS_RIPPLE_POOL_SIZE; i++) {
        if (!bass_ripple_waves[i].active) {
          slot  = i;
          found = true;
          break;
        }
        uint32_t age = now_ms - bass_ripple_waves[i].spawn_ms;
        if (age > oldest_age) {
          oldest_age = age;
          slot       = i;
        }
      }
      (void)found;

      uint8_t wave_hue;
      if (random_per_beat || rainbow) {
        wave_hue = bass_ripple_hue_idx;
        bass_ripple_hue_idx = (uint8_t)(bass_ripple_hue_idx + 43u);
      } else {
        uint8_t base_r = 0u, base_g = 255u, base_b = 96u;
        effect_primary_color(0u, 255u, 96u, &base_r, &base_g, &base_b);
        uint8_t s_dummy = 0u, v_dummy = 0u;
        led_matrix_rgb_to_hsv(base_r, base_g, base_b, &wave_hue, &s_dummy, &v_dummy);
      }

      bass_ripple_waves[slot].active   = true;
      bass_ripple_waves[slot].spawn_ms = now_ms;
      bass_ripple_waves[slot].hue      = wave_hue;
      bass_ripple_waves[slot].energy   = wave_energy;
      bass_ripple_last_spawn           = now_ms;
    }

    bass_ripple_prev_level = combined_level;
  } else {
    /* No audio — drain envelope. */
    bass_ripple_env = (bass_ripple_env > 2u) ? (uint8_t)(bass_ripple_env - 2u) : 0u;
    bass_ripple_prev_level = bass_ripple_env;
  }

  /* ── Rendering parameters (mirrors splash_physics) ───────────────────── */
  uint16_t lifetime_ms   = (uint16_t)(320u + (((uint32_t)(255u - decay_param)  * 930u)  / 255u));
  uint16_t ring_width    = (uint16_t)(10u  + (((uint32_t)spread_param          * 12u)   / 255u));
  uint16_t trail_length  = (uint16_t)(28u  + (((uint32_t)spread_param          * 52u)   / 255u));
  uint16_t core_radius   = (uint16_t)(12u  + (((uint32_t)spread_param          * 8u)    / 255u));
  uint16_t spread_div    = (uint16_t)(7u   + (((uint32_t)spread_param          * 17u)   / 255u));
  uint16_t gain          = gain_param;
  uint16_t trail_gain    = (gain > 48u) ? (gain - 48u) : 0u;
  uint16_t core_gain     = gain + 32u;
  uint16_t wave_speed_q8 = (uint16_t)(84u  + (((uint32_t)effect_animation_speed() * 172u) / 255u));

  uint8_t base_r = 0u, base_g = 255u, base_b = 96u;
  effect_primary_color(0u, 255u, 96u, &base_r, &base_g, &base_b);
  uint8_t anchor_hue = 0u, anchor_sat = 0u, v_dummy = 0u;
  led_matrix_rgb_to_hsv(base_r, base_g, base_b, &anchor_hue, &anchor_sat, &v_dummy);
  uint8_t ring_sat  = (anchor_sat > 176u) ? anchor_sat : 176u;
  uint8_t trail_sat = (ring_sat > 56u) ? (uint8_t)(ring_sat - 56u) : 120u;

  /* ── Accumulate pixel contributions ─────────────────────────────────── */
  uint16_t accum_r[NUM_KEYS];
  uint16_t accum_g[NUM_KEYS];
  uint16_t accum_b[NUM_KEYS];
  uint8_t  core_peak[NUM_KEYS];

  uint16_t floor_r = (uint16_t)(((uint32_t)base_r * base_glow) / 255u);
  uint16_t floor_g = (uint16_t)(((uint32_t)base_g * base_glow) / 255u);
  uint16_t floor_b = (uint16_t)(((uint32_t)base_b * base_glow) / 255u);

  for (uint8_t i = 0u; i < NUM_KEYS; i++) {
    accum_r[i]   = floor_r;
    accum_g[i]   = floor_g;
    accum_b[i]   = floor_b;
    core_peak[i] = 0u;
  }

  for (uint8_t wi = 0u; wi < BASS_RIPPLE_POOL_SIZE; wi++) {
    bass_ripple_wave_t *wave = &bass_ripple_waves[wi];
    if (!wave->active) { continue; }

    uint32_t age_raw = now_ms - wave->spawn_ms;
    if (age_raw >= lifetime_ms) {
      wave->active = false;
      continue;
    }
    uint16_t age_ms = (uint16_t)age_raw;

    uint8_t energy = (uint8_t)(((uint32_t)(lifetime_ms - age_ms) * wave->energy) / lifetime_ms);
    if (energy == 0u) { continue; }

    uint16_t front = (uint16_t)(((uint32_t)age_ms * wave_speed_q8) >> 8u);

    uint8_t ring_hue  = wave->hue;
    uint8_t trail_hue = (uint8_t)(ring_hue + 18u);
    uint8_t rr = 0u, rg = 0u, rb = 0u;
    uint8_t tr = 0u, tg = 0u, tb = 0u;
    led_matrix_hsv_to_rgb(ring_hue,  ring_sat,  255u, &rr, &rg, &rb);
    led_matrix_hsv_to_rgb(trail_hue, trail_sat, 255u, &tr, &tg, &tb);

    for (uint8_t li = 0u; li < NUM_KEYS; li++) {
      uint16_t raw_dist = bass_ripple_center_dist(li);
      uint16_t distance = raw_dist / spread_div;

      uint16_t ring_delta = (front > distance) ? (front - distance) : (distance - front);
      uint16_t ring_raw   = 0u;
      uint16_t trail_raw  = 0u;
      uint16_t core_raw   = 0u;

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

      uint8_t rv = clamp_u16_to_u8((uint16_t)(((uint32_t)ring_raw  * gain)       / 255u));
      uint8_t tv = clamp_u16_to_u8((uint16_t)(((uint32_t)trail_raw * trail_gain) / 255u));
      uint8_t cv = clamp_u16_to_u8((uint16_t)(((uint32_t)core_raw  * core_gain)  / 255u));

      if (rv > 0u) { add_scaled_rgb(&accum_r[li], &accum_g[li], &accum_b[li], rr, rg, rb, rv); }
      if (tv > 0u) { add_scaled_rgb(&accum_r[li], &accum_g[li], &accum_b[li], tr, tg, tb, tv); }
      if (cv > 0u) {
        add_scaled_rgb(&accum_r[li], &accum_g[li], &accum_b[li], base_r, base_g, base_b, cv);
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
