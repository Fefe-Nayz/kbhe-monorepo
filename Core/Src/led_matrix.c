/*
 * led_matrix.c
 * RGB LED strip/matrix driver using WS2812
 */

#include "led_matrix.h"
#include "analog/analog.h"
#include "led_indicator.h"
#include "main.h"
#include "settings.h"
#include "trigger/trigger.h"
// #include "trigger.h"
#include "ws2812.h"
#include <string.h>
#include <math.h>
#include <stdlib.h>


//--------------------------------------------------------------------+
// Private Variables
//--------------------------------------------------------------------+

// WS2812 handle - made globally accessible for DMA callbacks
ws2812_handleTypeDef led_ws2812_handle;

// Static matrix pattern (user/software editable, persisted in settings)
static uint8_t pixels_static[LED_MATRIX_DATA_SIZE];

// Runtime frame currently displayed on LEDs (effects/third-party/live state)
static uint8_t pixels_runtime[LED_MATRIX_DATA_SIZE];

// Temporary high-priority output frame that masks runtime pixels while active.
static uint8_t pixels_output_override[LED_MATRIX_DATA_SIZE];
static bool output_override_active = false;

// Current brightness
static uint8_t current_brightness = LED_BRIGHTNESS_DEFAULT;

// Enabled state
static bool display_enabled = true;

// Initialized flag
static bool initialized = false;

// Effect state variables
static led_effect_mode_t current_effect = LED_EFFECT_NONE;
static uint8_t fps_limit = 60; // Default 60 FPS
static uint8_t effect_params[LED_EFFECT_PARAM_COUNT] = {
  [LED_EFFECT_PARAM_SPEED] = 128u,
};
static uint32_t last_effect_tick = 0;
static uint32_t last_render_tick = 0; // Separate tick for FPS limiting
static uint32_t last_sim_tick = 0;    // Simulation updates are FPS-independent
static uint16_t effect_offset = 0;

// Effect renderer context flag: when true, pixel writes target runtime frame only.
static bool effect_render_context = false;

typedef struct {
  bool active;
  uint8_t source_key;
  uint8_t hue;
  uint8_t energy;
  uint16_t age_ms;
} reactive_wave_t;

#define REACTIVE_WAVE_POOL_SIZE 24u
static reactive_wave_t reactive_waves[REACTIVE_WAVE_POOL_SIZE];
static uint16_t reactive_heat_energy[NUM_KEYS] = {0};
static uint16_t reactive_ghost_energy[NUM_KEYS] = {0};
static uint8_t mxfx_typing_heat_energy[NUM_KEYS] = {0};
static uint32_t mxfx_typing_heat_decay_timer_ms = 0u;
static uint16_t impact_boost = 0u;

#define mxfx_LED_HITS_TO_REMEMBER 8u
#define mxfx_COORD_MAX_X 224u
#define mxfx_COORD_MAX_Y 64u
#define mxfx_CENTER_X (mxfx_COORD_MAX_X / 2u)
#define mxfx_CENTER_Y (mxfx_COORD_MAX_Y / 2u)

typedef struct {
  uint8_t count;
  uint8_t x[mxfx_LED_HITS_TO_REMEMBER];
  uint8_t y[mxfx_LED_HITS_TO_REMEMBER];
  uint8_t index[mxfx_LED_HITS_TO_REMEMBER];
  uint16_t tick[mxfx_LED_HITS_TO_REMEMBER];
} mxfx_last_hit_t;

typedef struct {
  uint8_t h;
  uint8_t s;
  uint8_t v;
} led_hsv_t;

typedef void (*mxfx_reactive_math_f)(const led_hsv_t *base, led_hsv_t *hsv,
                                    uint16_t offset);
typedef void (*mxfx_reactive_splash_math_f)(const led_hsv_t *base,
                                           led_hsv_t *hsv, int16_t dx,
                                           int16_t dy, uint8_t dist,
                                           uint16_t tick);

static mxfx_last_hit_t mxfx_last_hit_tracker = {0};

static uint8_t audio_spectrum_levels[LED_AUDIO_SPECTRUM_BAND_COUNT] = {0};
static uint8_t audio_impact_level = 0u;
static bool audio_spectrum_valid = false;
static uint32_t audio_spectrum_refresh_ms = 0u;

#define AUDIO_SPECTRUM_STALE_MS 500u

// Temporary volume overlay shown on the function row.
static bool volume_overlay_active = false;
static uint8_t volume_overlay_level = 0u;
static uint32_t volume_overlay_expire_ms = 0u;
static bool host_volume_level_valid = false;
static uint8_t host_volume_level = 0u;
static uint32_t host_volume_level_refresh_ms = 0u;

static const uint8_t volume_overlay_keys[] = {
    0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u, 10u, 11u, 12u, 13u
};
#define VOLUME_OVERLAY_KEY_COUNT                                                \
  (sizeof(volume_overlay_keys) / sizeof(volume_overlay_keys[0]))
#define HOST_VOLUME_LEVEL_STALE_MS 1250u
#define HOST_VOLUME_STEP_ESTIMATE 5u

// Logical key order follows the physical keyboard layout (K1..K82).
// WS2812 chain order on the PCB is serpentine, so translate before pushing to
// the driver.
static const uint8_t LOGICAL_LED_INDEX_TO_PHYSICAL_LED_INDEX[NUM_KEYS] = {
    0, 1, 2, 3, 4, 5, 6, 7,
    8, 9, 10, 11, 12, 13, 28, 27,
    26, 25, 24, 23, 22, 21, 20, 19,
    18, 17, 16, 15, 14, 29, 30, 31,
    32, 33, 34, 35, 36, 37, 38, 39,
    40, 41, 42, 43, 57, 56, 55, 54,
    53, 52, 51, 50, 49, 48, 47, 46,
    45, 44, 58, 59, 60, 61, 62, 63,
    64, 65, 66, 67, 68, 69, 70, 71,
    81, 80, 79, 78, 77, 76, 75, 74,
    73, 72
};

#define CAPS_LOCK_LED_INDEX 44u

// Physical LED coordinates derived from the KiCad PCB placement (`75he`).
// Stored in logical key order, normalized to the top-left of the board and
// scaled in 0.1 mm to keep the math integer-only.
static const uint16_t led_pos_x[NUM_KEYS] = {
    0, 238, 429, 619, 810, 1048, 1238, 1429,
    1619, 1857, 2048, 2238, 2429, 2667, 0, 190,
    381, 572, 762, 952, 1143, 1334, 1524, 1715,
    1905, 2096, 2286, 2572, 2905, 48, 286, 476,
    667, 857, 1048, 1238, 1429, 1619, 1810, 2000,
    2191, 2381, 2643, 2905, 71, 333, 524, 714,
    905, 1095, 1286, 1476, 1667, 1857, 2048, 2238,
    2429, 2905, 24, 238, 428, 619, 810, 1000,
    1191, 1381, 1572, 1762, 1953, 2143, 2405, 2715,
    24, 262, 500, 1214, 1905, 2096, 2286, 2524,
    2715, 2905,
};

static const uint16_t led_pos_y[NUM_KEYS] = {
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 238, 238,
    238, 238, 238, 238, 238, 238, 238, 238,
    238, 238, 238, 238, 238, 429, 429, 429,
    429, 429, 429, 429, 429, 429, 429, 429,
    429, 429, 524, 429, 619, 619, 619, 619,
    619, 619, 619, 619, 619, 619, 619, 619,
    619, 619, 810, 810, 810, 810, 810, 810,
    810, 810, 810, 810, 810, 810, 810, 857,
    1000, 1000, 1000, 1000, 1000, 1000, 1000, 1048,
    1048, 1048,
};

#define LED_LAYOUT_MAX_X 2905u
#define LED_LAYOUT_MAX_Y 1048u
#define LED_LAYOUT_CENTER_X (LED_LAYOUT_MAX_X / 2u)
#define LED_LAYOUT_CENTER_Y (LED_LAYOUT_MAX_Y / 2u)

static void effect_plasma(void);
static void effect_fire(void);
static void effect_ocean(void);
static void effect_sparkle(void);
static void effect_breathing_rainbow(void);
static void effect_color_cycle(void);
static void effect_distance_sensor(void);
static void effect_impact_rainbow(void);
static void effect_reactive_ghost(void);
static void effect_audio_spectrum(void);
static void effect_key_state_demo(void);
static void effect_cycle_pinwheel(void);
static void effect_cycle_spiral(void);
static void effect_cycle_out_in_dual(void);
static void effect_rainbow_beacon(void);
static void effect_rainbow_pinwheels(void);
static void effect_rainbow_moving_chevron(void);
static void effect_hue_breathing(void);
static void effect_hue_pendulum(void);
static void effect_hue_wave(void);
static void effect_riverflow(void);
static void effect_solid_color(void);
static void effect_alpha_mods(void);
static void effect_gradient_up_down(void);
static void effect_gradient_left_right(void);
static void effect_breathing(void);
static void effect_colorband_sat(void);
static void effect_colorband_val(void);
static void effect_colorband_pinwheel_sat(void);
static void effect_colorband_pinwheel_val(void);
static void effect_colorband_spiral_sat(void);
static void effect_colorband_spiral_val(void);
static void effect_cycle_all(void);
static void effect_cycle_left_right(void);
static void effect_cycle_up_down(void);
static void effect_cycle_out_in(void);
static void effect_dual_beacon(void);
static void effect_flower_blooming(void);
static void effect_raindrops(void);
static void effect_jellybean_raindrops(void);
static void effect_pixel_rain(void);
static void effect_pixel_flow(void);
static void effect_pixel_fractal(void);
static void effect_typing_heatmap(void);
static void effect_digital_rain(void);
static void effect_solid_reactive_simple(void);
static void effect_solid_reactive(void);
static void effect_solid_reactive_wide(void);
static void effect_solid_reactive_cross(void);
static void effect_solid_reactive_nexus(void);
static void effect_splash(void);
static void effect_solid_splash(void);
static void effect_starlight_smooth(void);
static void effect_starlight(void);
static void effect_starlight_dual_sat(void);
static void effect_starlight_dual_hue(void);
static void effect_solid_reactive_multi_wide(void);
static void effect_solid_reactive_multi_cross(void);
static void effect_solid_reactive_multi_nexus(void);
static void effect_multi_splash(void);
static void effect_solid_multi_splash(void);

static void reactive_tick_simulation(uint16_t delta_ms);
static void led_matrix_rgb_to_hsv(uint8_t r, uint8_t g, uint8_t b,
                                  uint8_t *h, uint8_t *s, uint8_t *v);

//--------------------------------------------------------------------+
// Private Functions
//--------------------------------------------------------------------+

/**
 * @brief Convert x,y to linear index
 */
static inline uint8_t xy_to_index(uint8_t x, uint8_t y) {
  // Standard row-major layout
  // Adjust here if your LED strip has a different wiring pattern
  return y * LED_MATRIX_WIDTH + x;
}

/**
 * @brief Apply brightness to a color value
 */
static inline uint8_t apply_brightness(uint8_t color, uint8_t brightness) {
  return (uint8_t)((uint16_t)color * brightness / 255);
}

static inline uint8_t abs_u8_diff(uint8_t a, uint8_t b) {
  return (a > b) ? (a - b) : (b - a);
}

static inline uint16_t abs_u16_diff(uint16_t a, uint16_t b) {
  return (a > b) ? (a - b) : (b - a);
}

static inline uint8_t clamp_u16_to_u8(uint16_t value) {
  return (uint8_t)((value > 255u) ? 255u : value);
}

static inline uint8_t scale_u8(uint8_t value, uint8_t scale) {
  return (uint8_t)(((uint16_t)value * scale) / 255u);
}

static inline uint8_t scale_coord_to_u8(uint16_t value, uint16_t max_value) {
  if (max_value == 0u) {
    return 0u;
  }

  return (uint8_t)(((uint32_t)value * 255u) / max_value);
}

static inline uint8_t effect_param(uint8_t index, uint8_t fallback) {
  if (index >= LED_EFFECT_PARAM_COUNT) {
    return fallback;
  }
  return effect_params[index];
}

static inline uint8_t effect_animation_speed(void) {
  uint8_t speed = effect_param(LED_EFFECT_PARAM_SPEED, 128u);
  return speed > 0u ? speed : 1u;
}

static inline bool effect_param_flag(uint8_t index, bool fallback) {
  return effect_param(index, fallback ? 1u : 0u) != 0u;
}

static inline void effect_primary_color(uint8_t fallback_r, uint8_t fallback_g,
                                        uint8_t fallback_b, uint8_t *r,
                                        uint8_t *g, uint8_t *b) {
  if (r != NULL) {
    *r = effect_param(LED_EFFECT_PARAM_COLOR_R, fallback_r);
  }
  if (g != NULL) {
    *g = effect_param(LED_EFFECT_PARAM_COLOR_G, fallback_g);
  }
  if (b != NULL) {
    *b = effect_param(LED_EFFECT_PARAM_COLOR_B, fallback_b);
  }
}

static inline void effect_secondary_color(uint8_t fallback_r,
                                          uint8_t fallback_g,
                                          uint8_t fallback_b, uint8_t *r,
                                          uint8_t *g, uint8_t *b) {
  if (r != NULL) {
    *r = effect_param(LED_EFFECT_PARAM_COLOR2_R, fallback_r);
  }
  if (g != NULL) {
    *g = effect_param(LED_EFFECT_PARAM_COLOR2_G, fallback_g);
  }
  if (b != NULL) {
    *b = effect_param(LED_EFFECT_PARAM_COLOR2_B, fallback_b);
  }
}

static inline uint16_t effect_param_coord(uint8_t index, uint16_t max_value,
                                          uint8_t fallback) {
  return (uint16_t)(((uint32_t)effect_param(index, fallback) * max_value) /
                    255u);
}

static inline uint8_t mxfx_qadd8(uint8_t a, uint8_t b) {
  uint16_t sum = (uint16_t)a + b;
  return (uint8_t)((sum > 255u) ? 255u : sum);
}

static inline uint8_t mxfx_scale8(uint8_t value, uint8_t scale) {
  return (uint8_t)(((uint16_t)value * scale) >> 8);
}

static inline uint16_t mxfx_scale16by8(uint16_t value, uint8_t scale) {
  return (uint16_t)(((uint32_t)value * scale) >> 8);
}

static inline uint16_t mxfx_timer16(void) {
  return (uint16_t)HAL_GetTick();
}

static inline uint8_t mxfx_runner_time_i(void) {
  return (uint8_t)mxfx_scale16by8(mxfx_timer16(),
                                 mxfx_qadd8((uint8_t)(effect_animation_speed() / 4u),
                                           1u));
}

static inline uint8_t mxfx_runner_time_dx_dy(void) {
  return (uint8_t)mxfx_scale16by8(mxfx_timer16(),
                                 (uint8_t)(effect_animation_speed() / 2u));
}

static inline uint8_t mxfx_runner_time_sin_cos(void) {
  return (uint8_t)mxfx_scale16by8(mxfx_timer16(),
                                 (uint8_t)(effect_animation_speed() / 4u));
}

static uint8_t mxfx_sin8(uint8_t angle) {
  float radians = ((float)angle / 256.0f) * (2.0f * 3.14159265358979323846f);
  float value = sinf(radians);
  int16_t scaled = (int16_t)(127.5f + (value * 127.5f));
  if (scaled < 0) {
    scaled = 0;
  } else if (scaled > 255) {
    scaled = 255;
  }
  return (uint8_t)scaled;
}

static uint8_t mxfx_cos8(uint8_t angle) {
  float radians = ((float)angle / 256.0f) * (2.0f * 3.14159265358979323846f);
  float value = cosf(radians);
  int16_t scaled = (int16_t)(127.5f + (value * 127.5f));
  if (scaled < 0) {
    scaled = 0;
  } else if (scaled > 255) {
    scaled = 255;
  }
  return (uint8_t)scaled;
}

static inline uint8_t mxfx_random8(void) {
  return (uint8_t)(rand() & 0xFFu);
}

static inline uint8_t mxfx_random8_max(uint8_t max) {
  if (max == 0u) {
    return 0u;
  }
  return (uint8_t)(mxfx_random8() % max);
}

static inline uint8_t mxfx_random8_min_max(uint8_t min, uint8_t max) {
  if (max <= min) {
    return min;
  }
  return (uint8_t)(min + (mxfx_random8() % (uint8_t)(max - min)));
}

static uint16_t mxfx_sqrt32(uint32_t value) {
  uint32_t result = 0u;
  uint32_t bit = 1uL << 30;

  while (bit > value) {
    bit >>= 2;
  }

  while (bit != 0u) {
    if (value >= result + bit) {
      value -= result + bit;
      result = (result >> 1) + bit;
    } else {
      result >>= 1;
    }
    bit >>= 2;
  }

  return (uint16_t)result;
}

static inline uint8_t mxfx_led_coord_x(uint8_t index) {
  return (uint8_t)(((uint32_t)led_pos_x[index] * mxfx_COORD_MAX_X) /
                   LED_LAYOUT_MAX_X);
}

static inline uint8_t mxfx_led_coord_y(uint8_t index) {
  return (uint8_t)(((uint32_t)led_pos_y[index] * mxfx_COORD_MAX_Y) /
                   LED_LAYOUT_MAX_Y);
}

static inline uint8_t mxfx_distance_u8(int16_t dx, int16_t dy) {
  uint16_t adx = (dx < 0) ? (uint16_t)(-dx) : (uint16_t)dx;
  uint16_t ady = (dy < 0) ? (uint16_t)(-dy) : (uint16_t)dy;
  uint16_t dist = mxfx_sqrt32((uint32_t)adx * adx + (uint32_t)ady * ady);
  return (uint8_t)((dist > 255u) ? 255u : dist);
}

static led_hsv_t effect_primary_hsv(uint8_t fallback_r, uint8_t fallback_g,
                                    uint8_t fallback_b) {
  led_hsv_t hsv = {0u, 0u, 0u};
  uint8_t r = effect_params[LED_EFFECT_PARAM_COLOR_R];
  uint8_t g = effect_params[LED_EFFECT_PARAM_COLOR_G];
  uint8_t b = effect_params[LED_EFFECT_PARAM_COLOR_B];

  /* Treat stored (0,0,0) as "not configured" and fall back to the per-call
   * default.  This preserves QMK pixel-perfect behavior for effects that do
   * not have an explicit PCOLOR entry in their schema. */
  if (r == 0u && g == 0u && b == 0u) {
    r = fallback_r;
    g = fallback_g;
    b = fallback_b;
  }

  led_matrix_rgb_to_hsv(r, g, b, &hsv.h, &hsv.s, &hsv.v);
  return hsv;
}

static void mxfx_reactive_record_hit(uint8_t key_index) {
  uint8_t slot = mxfx_last_hit_tracker.count;

  if (slot >= mxfx_LED_HITS_TO_REMEMBER) {
    memmove(&mxfx_last_hit_tracker.x[0], &mxfx_last_hit_tracker.x[1],
            mxfx_LED_HITS_TO_REMEMBER - 1u);
    memmove(&mxfx_last_hit_tracker.y[0], &mxfx_last_hit_tracker.y[1],
            mxfx_LED_HITS_TO_REMEMBER - 1u);
    memmove(&mxfx_last_hit_tracker.index[0], &mxfx_last_hit_tracker.index[1],
            mxfx_LED_HITS_TO_REMEMBER - 1u);
    memmove(&mxfx_last_hit_tracker.tick[0], &mxfx_last_hit_tracker.tick[1],
            (mxfx_LED_HITS_TO_REMEMBER - 1u) * sizeof(uint16_t));
    slot = mxfx_LED_HITS_TO_REMEMBER - 1u;
  }

  mxfx_last_hit_tracker.x[slot] = mxfx_led_coord_x(key_index);
  mxfx_last_hit_tracker.y[slot] = mxfx_led_coord_y(key_index);
  mxfx_last_hit_tracker.index[slot] = key_index;
  mxfx_last_hit_tracker.tick[slot] = 0u;
  mxfx_last_hit_tracker.count = slot + 1u;
}

static void mxfx_reactive_tick_hits(uint16_t delta_ms) {
  for (uint8_t i = 0u; i < mxfx_last_hit_tracker.count; i++) {
    if (UINT16_MAX - delta_ms < mxfx_last_hit_tracker.tick[i]) {
      mxfx_last_hit_tracker.tick[i] = UINT16_MAX;
    } else {
      mxfx_last_hit_tracker.tick[i] =
          (uint16_t)(mxfx_last_hit_tracker.tick[i] + delta_ms);
    }
  }
}

static void mxfx_reactive_render(mxfx_reactive_math_f math_fn) {
  led_hsv_t base_hsv = effect_primary_hsv(255u, 0u, 0u);
  uint8_t speed_scale = mxfx_qadd8(effect_animation_speed(), 1u);
  uint16_t max_tick = (uint16_t)(65535u / speed_scale);

  for (uint8_t i = 0u; i < LED_MATRIX_NUM_LEDS; i++) {
    uint16_t tick = max_tick;
    led_hsv_t hsv = base_hsv;

    for (int8_t j = (int8_t)mxfx_last_hit_tracker.count - 1; j >= 0; j--) {
      if (mxfx_last_hit_tracker.index[(uint8_t)j] == i &&
          mxfx_last_hit_tracker.tick[(uint8_t)j] < tick) {
        tick = mxfx_last_hit_tracker.tick[(uint8_t)j];
        break;
      }
    }

    if (math_fn != NULL) {
      uint16_t offset = mxfx_scale16by8(tick, speed_scale);
      math_fn(&base_hsv, &hsv, offset);
    }

    {
      uint8_t r = 0u;
      uint8_t g = 0u;
      uint8_t b = 0u;
      led_matrix_hsv_to_rgb(hsv.h, hsv.s, hsv.v, &r, &g, &b);
      led_matrix_set_pixel_idx(i, r, g, b);
    }
  }
}

static void mxfx_reactive_splash_render(uint8_t start,
                                       mxfx_reactive_splash_math_f math_fn) {
  led_hsv_t base_hsv = effect_primary_hsv(255u, 0u, 0u);
  uint8_t speed_scale = mxfx_qadd8(effect_animation_speed(), 1u);
  uint8_t count = mxfx_last_hit_tracker.count;
  uint8_t begin = (start < count) ? start : count;

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
      math_fn(&base_hsv, &hsv, dx, dy, dist, tick);
    }

    hsv.v = mxfx_scale8(hsv.v, base_hsv.v);

    {
      uint8_t r = 0u;
      uint8_t g = 0u;
      uint8_t b = 0u;
      led_matrix_hsv_to_rgb(hsv.h, hsv.s, hsv.v, &r, &g, &b);
      led_matrix_set_pixel_idx(i, r, g, b);
    }
  }
}

static uint8_t pseudo_angle_u8(int16_t x, int16_t y) {
  uint16_t ax = (x < 0) ? (uint16_t)(-x) : (uint16_t)x;
  uint16_t ay = (y < 0) ? (uint16_t)(-y) : (uint16_t)y;
  uint16_t sum = (uint16_t)(ax + ay);
  uint8_t octant = (sum == 0u) ? 0u : (uint8_t)(((uint32_t)ay * 64u) / sum);

  if (x >= 0 && y >= 0) {
    return octant;
  }
  if (x < 0 && y >= 0) {
    return (uint8_t)(128u - octant);
  }
  if (x < 0 && y < 0) {
    return (uint8_t)(128u + octant);
  }

  return (uint8_t)(255u - octant);
}

static inline uint16_t led_layout_distance(uint8_t a, uint8_t b) {
  uint16_t dx = abs_u16_diff(led_pos_x[a], led_pos_x[b]);
  uint16_t dy = abs_u16_diff(led_pos_y[a], led_pos_y[b]);
  uint16_t major = (dx > dy) ? dx : dy;
  uint16_t minor = (dx > dy) ? dy : dx;
  return major + (minor / 2u);
}

static inline void add_scaled_rgb(uint16_t *r_acc, uint16_t *g_acc,
                                  uint16_t *b_acc, uint8_t r, uint8_t g,
                                  uint8_t b, uint8_t intensity) {
  *r_acc += ((uint16_t)r * intensity) / 255u;
  *g_acc += ((uint16_t)g * intensity) / 255u;
  *b_acc += ((uint16_t)b * intensity) / 255u;
}

static inline void mix_rgb_towards_white(uint8_t *r, uint8_t *g, uint8_t *b,
                                         uint8_t amount) {
  *r = (uint8_t)(*r + (((uint16_t)(255u - *r) * amount) / 255u));
  *g = (uint8_t)(*g + (((uint16_t)(255u - *g) * amount) / 255u));
  *b = (uint8_t)(*b + (((uint16_t)(255u - *b) * amount) / 255u));
}

static inline bool volume_overlay_is_expired(uint32_t now_ms) {
  return volume_overlay_active &&
         ((int32_t)(now_ms - volume_overlay_expire_ms) >= 0);
}

static inline uint8_t triangle_wave_u8(uint8_t phase) {
  if (phase < 128u) {
    return (uint8_t)(phase << 1);
  }

  return (uint8_t)((255u - phase) << 1);
}

static bool volume_overlay_slot_for_index(uint8_t index, uint8_t *slot_out) {
  for (uint8_t slot = 0u; slot < (uint8_t)VOLUME_OVERLAY_KEY_COUNT; slot++) {
    if (volume_overlay_keys[slot] == index) {
      if (slot_out != NULL) {
        *slot_out = slot;
      }
      return true;
    }
  }

  return false;
}

static uint8_t volume_overlay_fill_scale(uint8_t slot) {
  uint32_t scaled =
      (uint32_t)volume_overlay_level * (uint32_t)VOLUME_OVERLAY_KEY_COUNT;
  uint32_t filled = (scaled + 254u) / 255u;

  if (slot < filled) {
    return 255u;
  }
  if (slot == filled) {
    return (uint8_t)(scaled % 255u);
  }

  return 0u;
}

static void volume_overlay_effect_palette_color(led_effect_mode_t effect_mode,
                                                uint8_t slot, uint8_t *r,
                                                uint8_t *g, uint8_t *b) {
  uint8_t phase = (uint8_t)(((uint16_t)slot * 255u) /
                            ((VOLUME_OVERLAY_KEY_COUNT > 1u)
                                 ? (uint16_t)(VOLUME_OVERLAY_KEY_COUNT - 1u)
                                 : 1u));
  uint8_t anim_phase = (uint8_t)(phase + (uint8_t)(effect_offset * 3u));
  uint8_t wave = triangle_wave_u8((uint8_t)(effect_offset + phase));
  uint8_t color_r = 255u;
  uint8_t color_g = 0u;
  uint8_t color_b = 0u;

  effect_primary_color(255u, 0u, 0u, &color_r, &color_g, &color_b);

  switch (effect_mode) {
  case LED_EFFECT_STATIC_MATRIX: {
    uint8_t key = volume_overlay_keys[slot];
    *r = pixels_static[key * 3u + 0u];
    *g = pixels_static[key * 3u + 1u];
    *b = pixels_static[key * 3u + 2u];
    if (*r == 0u && *g == 0u && *b == 0u) {
      *r = color_r;
      *g = color_g;
      *b = color_b;
    }
    break;
  }
  case LED_EFFECT_SOLID_COLOR:
    *r = color_r;
    *g = color_g;
    *b = color_b;
    break;
  case LED_EFFECT_BREATHING: {
    uint8_t scale = (uint8_t)(32u + (((uint16_t)wave * 223u) / 255u));
    *r = scale_u8(color_r, scale);
    *g = scale_u8(color_g, scale);
    *b = scale_u8(color_b, scale);
    break;
  }
  case LED_EFFECT_FIRE:
    led_matrix_hsv_to_rgb((uint8_t)(6u + (phase / 10u)), 255u,
                          (uint8_t)(160u + (wave / 3u)), r, g, b);
    break;
  case LED_EFFECT_OCEAN:
    led_matrix_hsv_to_rgb((uint8_t)(132u + (phase / 6u)), 220u,
                          (uint8_t)(150u + (wave / 2u)), r, g, b);
    break;
  case LED_EFFECT_DIGITAL_RAIN:
    if (((slot + (effect_offset / 8u)) % 5u) == 0u) {
      *r = 220u;
      *g = 255u;
      *b = 220u;
    } else {
      led_matrix_hsv_to_rgb(96u, 255u, (uint8_t)(72u + (wave / 2u)), r, g, b);
    }
    break;
  case LED_EFFECT_SPARKLE:
    if (((slot + (effect_offset / 4u)) % 4u) == 0u) {
      *r = 255u;
      *g = 255u;
      *b = 255u;
    } else {
      led_matrix_hsv_to_rgb((uint8_t)(anim_phase + 24u), 170u, 180u, r, g, b);
    }
    break;
  case LED_EFFECT_CYCLE_LEFT_RIGHT:
  case LED_EFFECT_GRADIENT_LEFT_RIGHT:
  case LED_EFFECT_BREATHING_RAINBOW:
  case LED_EFFECT_COLOR_CYCLE:
    led_matrix_hsv_to_rgb(anim_phase, 255u, 255u, r, g, b);
    break;
  case LED_EFFECT_PLASMA:
  case LED_EFFECT_DISTANCE_SENSOR:
  default:
    led_matrix_hsv_to_rgb((uint8_t)(anim_phase + (slot * 9u)), 220u,
                          (uint8_t)(180u + (wave / 3u)), r, g, b);
    break;
  }
}

static void volume_overlay_base_color(uint8_t slot, uint8_t *r, uint8_t *g,
                                      uint8_t *b) {
  settings_rotary_encoder_t rotary = {0};
  uint8_t hue = (uint8_t)(((uint16_t)slot * 255u) /
                          ((VOLUME_OVERLAY_KEY_COUNT > 1u)
                               ? (uint16_t)(VOLUME_OVERLAY_KEY_COUNT - 1u)
                               : 1u));

  settings_get_rotary_encoder(&rotary);

  switch ((rotary_progress_style_t)rotary.progress_style) {
  case ROTARY_PROGRESS_STYLE_RAINBOW:
    led_matrix_hsv_to_rgb((uint8_t)(hue + (uint8_t)(effect_offset * 4u)), 255u,
                          255u, r, g, b);
    break;
  case ROTARY_PROGRESS_STYLE_EFFECT_PALETTE:
    volume_overlay_effect_palette_color(
        (led_effect_mode_t)rotary.progress_effect_mode, slot, r, g, b);
    break;
  case ROTARY_PROGRESS_STYLE_SOLID:
  default:
    *r = rotary.progress_color_r;
    *g = rotary.progress_color_g;
    *b = rotary.progress_color_b;
    break;
  }
}

static bool volume_overlay_color_for_index(uint8_t index, uint32_t now_ms,
                                           uint8_t *r, uint8_t *g, uint8_t *b) {
  uint8_t slot = 0u;
  uint8_t scale = 0u;

  if (!volume_overlay_active) {
    return false;
  }
  if (volume_overlay_is_expired(now_ms)) {
    volume_overlay_active = false;
    return false;
  }
  if (!volume_overlay_slot_for_index(index, &slot)) {
    return false;
  }

  volume_overlay_base_color(slot, r, g, b);
  scale = volume_overlay_fill_scale(slot);
  *r = scale_u8(*r, scale);
  *g = scale_u8(*g, scale);
  *b = scale_u8(*b, scale);
  return true;
}

/**
 * @brief Push one runtime pixel to WS2812 buffer
 */
static inline void push_runtime_pixel_to_ws2812_at(uint8_t index,
                                                   uint32_t now_ms) {
  uint8_t physical_index = LOGICAL_LED_INDEX_TO_PHYSICAL_LED_INDEX[index];
  const uint8_t *source_frame =
      output_override_active ? pixels_output_override : pixels_runtime;
  uint8_t source_r = source_frame[index * 3 + 0];
  uint8_t source_g = source_frame[index * 3 + 1];
  uint8_t source_b = source_frame[index * 3 + 2];

  if (volume_overlay_color_for_index(index, now_ms, &source_r, &source_g,
                                     &source_b)) {
    // Progress overlay fully overrides the top row while active.
  } else if (index == CAPS_LOCK_LED_INDEX && led_indicator_is_caps_lock()) {
    source_r = 255u;
    source_g = 255u;
    source_b = 255u;
  }

  uint8_t r = apply_brightness(source_r, current_brightness);
  uint8_t g = apply_brightness(source_g, current_brightness);
  uint8_t b = apply_brightness(source_b, current_brightness);
  setLedValues(&led_ws2812_handle, physical_index, r, g, b);
}

static inline void push_runtime_pixel_to_ws2812(uint8_t index) {
  push_runtime_pixel_to_ws2812_at(index, HAL_GetTick());
}

/**
 * @brief Update ws2812 driver with current pixel data
 */
static void update_ws2812(void) {
  if (!initialized || !display_enabled)
    return;

  // Apply brightness and copy to WS2812 buffer
  uint32_t now_ms = HAL_GetTick();
  for (uint8_t i = 0; i < LED_MATRIX_NUM_LEDS; i++) {
    push_runtime_pixel_to_ws2812_at(i, now_ms);
  }

  ws2812_show(&led_ws2812_handle);
}

static void sync_runtime_from_static(void) {
  memcpy(pixels_runtime, pixels_static, LED_MATRIX_DATA_SIZE);
  if (initialized && display_enabled) {
    update_ws2812();
  }
}

static void render_current_effect_frame(void) {
  effect_render_context = true;
  switch (current_effect) {
  case LED_EFFECT_PLASMA:
    effect_plasma();
    break;
  case LED_EFFECT_FIRE:
    effect_fire();
    break;
  case LED_EFFECT_OCEAN:
    effect_ocean();
    break;
  case LED_EFFECT_SPARKLE:
    effect_sparkle();
    break;
  case LED_EFFECT_BREATHING_RAINBOW:
    effect_breathing_rainbow();
    break;
  case LED_EFFECT_COLOR_CYCLE:
    effect_color_cycle();
    break;
  case LED_EFFECT_DISTANCE_SENSOR:
    effect_distance_sensor();
    break;
  case LED_EFFECT_IMPACT_RAINBOW:
    effect_impact_rainbow();
    break;
  case LED_EFFECT_REACTIVE_GHOST:
    effect_reactive_ghost();
    break;
  case LED_EFFECT_AUDIO_SPECTRUM:
    effect_audio_spectrum();
    break;
  case LED_EFFECT_KEY_STATE_DEMO:
    effect_key_state_demo();
    break;
  case LED_EFFECT_CYCLE_PINWHEEL:
    effect_cycle_pinwheel();
    break;
  case LED_EFFECT_CYCLE_SPIRAL:
    effect_cycle_spiral();
    break;
  case LED_EFFECT_CYCLE_OUT_IN_DUAL:
    effect_cycle_out_in_dual();
    break;
  case LED_EFFECT_RAINBOW_BEACON:
    effect_rainbow_beacon();
    break;
  case LED_EFFECT_RAINBOW_PINWHEELS:
    effect_rainbow_pinwheels();
    break;
  case LED_EFFECT_RAINBOW_MOVING_CHEVRON:
    effect_rainbow_moving_chevron();
    break;
  case LED_EFFECT_HUE_BREATHING:
    effect_hue_breathing();
    break;
  case LED_EFFECT_HUE_PENDULUM:
    effect_hue_pendulum();
    break;
  case LED_EFFECT_HUE_WAVE:
    effect_hue_wave();
    break;
  case LED_EFFECT_RIVERFLOW:
    effect_riverflow();
    break;
  case LED_EFFECT_SOLID_COLOR:
    effect_solid_color();
    break;
  case LED_EFFECT_ALPHA_MODS:
    effect_alpha_mods();
    break;
  case LED_EFFECT_GRADIENT_UP_DOWN:
    effect_gradient_up_down();
    break;
  case LED_EFFECT_GRADIENT_LEFT_RIGHT:
    effect_gradient_left_right();
    break;
  case LED_EFFECT_BREATHING:
    effect_breathing();
    break;
  case LED_EFFECT_COLORBAND_SAT:
    effect_colorband_sat();
    break;
  case LED_EFFECT_COLORBAND_VAL:
    effect_colorband_val();
    break;
  case LED_EFFECT_COLORBAND_PINWHEEL_SAT:
    effect_colorband_pinwheel_sat();
    break;
  case LED_EFFECT_COLORBAND_PINWHEEL_VAL:
    effect_colorband_pinwheel_val();
    break;
  case LED_EFFECT_COLORBAND_SPIRAL_SAT:
    effect_colorband_spiral_sat();
    break;
  case LED_EFFECT_COLORBAND_SPIRAL_VAL:
    effect_colorband_spiral_val();
    break;
  case LED_EFFECT_CYCLE_ALL:
    effect_cycle_all();
    break;
  case LED_EFFECT_CYCLE_LEFT_RIGHT:
    effect_cycle_left_right();
    break;
  case LED_EFFECT_CYCLE_UP_DOWN:
    effect_cycle_up_down();
    break;
  case LED_EFFECT_CYCLE_OUT_IN:
    effect_cycle_out_in();
    break;
  case LED_EFFECT_DUAL_BEACON:
    effect_dual_beacon();
    break;
  case LED_EFFECT_FLOWER_BLOOMING:
    effect_flower_blooming();
    break;
  case LED_EFFECT_RAINDROPS:
    effect_raindrops();
    break;
  case LED_EFFECT_JELLYBEAN_RAINDROPS:
    effect_jellybean_raindrops();
    break;
  case LED_EFFECT_PIXEL_RAIN:
    effect_pixel_rain();
    break;
  case LED_EFFECT_PIXEL_FLOW:
    effect_pixel_flow();
    break;
  case LED_EFFECT_PIXEL_FRACTAL:
    effect_pixel_fractal();
    break;
  case LED_EFFECT_TYPING_HEATMAP:
    effect_typing_heatmap();
    break;
  case LED_EFFECT_DIGITAL_RAIN:
    effect_digital_rain();
    break;
  case LED_EFFECT_SOLID_REACTIVE_SIMPLE:
    effect_solid_reactive_simple();
    break;
  case LED_EFFECT_SOLID_REACTIVE:
    effect_solid_reactive();
    break;
  case LED_EFFECT_SOLID_REACTIVE_WIDE:
    effect_solid_reactive_wide();
    break;
  case LED_EFFECT_SOLID_REACTIVE_MULTI_WIDE:
    effect_solid_reactive_multi_wide();
    break;
  case LED_EFFECT_SOLID_REACTIVE_CROSS:
    effect_solid_reactive_cross();
    break;
  case LED_EFFECT_SOLID_REACTIVE_MULTI_CROSS:
    effect_solid_reactive_multi_cross();
    break;
  case LED_EFFECT_SOLID_REACTIVE_NEXUS:
    effect_solid_reactive_nexus();
    break;
  case LED_EFFECT_SOLID_REACTIVE_MULTI_NEXUS:
    effect_solid_reactive_multi_nexus();
    break;
  case LED_EFFECT_SPLASH:
    effect_splash();
    break;
  case LED_EFFECT_MULTI_SPLASH:
    effect_multi_splash();
    break;
  case LED_EFFECT_SOLID_SPLASH:
    effect_solid_splash();
    break;
  case LED_EFFECT_SOLID_MULTI_SPLASH:
    effect_solid_multi_splash();
    break;
  case LED_EFFECT_STARLIGHT_SMOOTH:
    effect_starlight_smooth();
    break;
  case LED_EFFECT_STARLIGHT:
    effect_starlight();
    break;
  case LED_EFFECT_STARLIGHT_DUAL_SAT:
    effect_starlight_dual_sat();
    break;
  case LED_EFFECT_STARLIGHT_DUAL_HUE:
    effect_starlight_dual_hue();
    break;
  default:
    break;
  }
  effect_render_context = false;

  if (initialized && display_enabled) {
    update_ws2812();
  }
}

//--------------------------------------------------------------------+
// Public Functions
//--------------------------------------------------------------------+

bool led_matrix_init(void *htim, uint32_t channel) {
  TIM_HandleTypeDef *timer = (TIM_HandleTypeDef *)htim;

  // Initialize WS2812 driver
  if (ws2812_init(&led_ws2812_handle, timer, channel, LED_MATRIX_NUM_LEDS) !=
      WS2812_Ok) {
    return false;
  }

  // Clear pixels
  memset(pixels_static, 0, sizeof(pixels_static));
  memset(pixels_runtime, 0, sizeof(pixels_runtime));
  memset(pixels_output_override, 0, sizeof(pixels_output_override));
  output_override_active = false;

  // Reset brightness
  current_brightness = LED_BRIGHTNESS_DEFAULT;
  display_enabled = true;
  initialized = true;
  last_sim_tick = 0;

  // Clear all LEDs
  zeroLedValues(&led_ws2812_handle);

  return true;
}

void led_matrix_set_pixel(uint8_t x, uint8_t y, uint8_t r, uint8_t g,
                          uint8_t b) {
  if (x >= LED_MATRIX_WIDTH || y >= LED_MATRIX_HEIGHT)
    return;

  uint8_t idx = xy_to_index(x, y);
  led_matrix_set_pixel_idx(idx, r, g, b);
}

void led_matrix_get_pixel(uint8_t x, uint8_t y, uint8_t *r, uint8_t *g,
                          uint8_t *b) {
  if (x >= LED_MATRIX_WIDTH || y >= LED_MATRIX_HEIGHT)
    return;

  uint8_t idx = xy_to_index(x, y);
  if (r)
    *r = pixels_static[idx * 3 + 0];
  if (g)
    *g = pixels_static[idx * 3 + 1];
  if (b)
    *b = pixels_static[idx * 3 + 2];
}

void led_matrix_set_pixel_idx(uint8_t index, uint8_t r, uint8_t g, uint8_t b) {
  if (index >= LED_MATRIX_NUM_LEDS)
    return;

  if (effect_render_context) {
    pixels_runtime[index * 3 + 0] = r;
    pixels_runtime[index * 3 + 1] = g;
    pixels_runtime[index * 3 + 2] = b;
    if (initialized && display_enabled) {
      push_runtime_pixel_to_ws2812(index);
    }
    return;
  }

  if (current_effect == LED_EFFECT_STATIC_MATRIX) {
    // Editable matrix mode: update both saved pattern and displayed frame.
    pixels_static[index * 3 + 0] = r;
    pixels_static[index * 3 + 1] = g;
    pixels_static[index * 3 + 2] = b;
    pixels_runtime[index * 3 + 0] = r;
    pixels_runtime[index * 3 + 1] = g;
    pixels_runtime[index * 3 + 2] = b;
    if (initialized && display_enabled) {
      push_runtime_pixel_to_ws2812(index);
      ws2812_show(&led_ws2812_handle);
    }
    return;
  }

  if (current_effect == LED_EFFECT_THIRD_PARTY) {
    // Third-party live mode: keep runtime editable externally, do not alter
    // persisted matrix pattern.
    pixels_runtime[index * 3 + 0] = r;
    pixels_runtime[index * 3 + 1] = g;
    pixels_runtime[index * 3 + 2] = b;
    if (initialized && display_enabled) {
      push_runtime_pixel_to_ws2812(index);
      ws2812_show(&led_ws2812_handle);
    }
    return;
  }

  // Animated effects: keep saved matrix pattern current, runtime is driven by
  // effect renderer and must not be overwritten here.
  pixels_static[index * 3 + 0] = r;
  pixels_static[index * 3 + 1] = g;
  pixels_static[index * 3 + 2] = b;
}

void led_matrix_clear(void) {
  if (effect_render_context) {
    memset(pixels_runtime, 0, sizeof(pixels_runtime));
    return;
  }

  if (current_effect == LED_EFFECT_STATIC_MATRIX) {
    memset(pixels_static, 0, sizeof(pixels_static));
    memset(pixels_runtime, 0, sizeof(pixels_runtime));
    if (initialized && display_enabled) {
      zeroLedValues(&led_ws2812_handle);
      ws2812_show(&led_ws2812_handle);
    }
    return;
  }

  if (current_effect == LED_EFFECT_THIRD_PARTY) {
    memset(pixels_runtime, 0, sizeof(pixels_runtime));
    if (initialized && display_enabled) {
      zeroLedValues(&led_ws2812_handle);
      ws2812_show(&led_ws2812_handle);
    }
    return;
  }

  memset(pixels_static, 0, sizeof(pixels_static));
}

void led_matrix_fill(uint8_t r, uint8_t g, uint8_t b) {
  for (uint8_t i = 0; i < LED_MATRIX_NUM_LEDS; i++) {
    led_matrix_set_pixel_idx(i, r, g, b);
  }
}

void led_matrix_set_brightness(uint8_t brightness) {
  current_brightness = brightness;

  // Re-apply brightness to all pixels
  if (initialized) {
    update_ws2812();
  }
}

uint8_t led_matrix_get_brightness(void) { return current_brightness; }

void led_matrix_set_enabled(bool enabled) {
  display_enabled = enabled;

  if (initialized) {
    if (enabled) {
      update_ws2812();
    } else {
      zeroLedValues(&led_ws2812_handle);
      ws2812_show(&led_ws2812_handle);
    }
  }
}

bool led_matrix_is_enabled(void) { return display_enabled; }

void led_matrix_update(void) { update_ws2812(); }

const uint8_t *led_matrix_get_raw_data(void) { return pixels_runtime; }

void led_matrix_set_raw_data(const uint8_t *data) {
  if (data == NULL)
    return;

  memcpy(pixels_static, data, LED_MATRIX_DATA_SIZE);

  // Only drive runtime directly when matrix mode is active.
  if (current_effect == LED_EFFECT_STATIC_MATRIX) {
    sync_runtime_from_static();
  }
}

led_matrix_data_t led_matrix_get_data(void) {
  led_matrix_data_t data;
  memcpy(data.pixels, pixels_static, LED_MATRIX_DATA_SIZE);
  data.brightness = current_brightness;
  data.enabled = display_enabled ? 1 : 0;
  data.reserved[0] = 0;
  data.reserved[1] = 0;
  return data;
}

void led_matrix_load_data(const led_matrix_data_t *data) {
  if (data == NULL)
    return;

  memcpy(pixels_static, data->pixels, LED_MATRIX_DATA_SIZE);
  current_brightness = data->brightness;
  display_enabled = data->enabled ? true : false;

  if (current_effect == LED_EFFECT_STATIC_MATRIX) {
    memcpy(pixels_runtime, pixels_static, LED_MATRIX_DATA_SIZE);
  }

  if (initialized) {
    if (display_enabled) {
      update_ws2812();
    } else {
      zeroLedValues(&led_ws2812_handle);
      ws2812_show(&led_ws2812_handle);
    }
  }
}

void led_matrix_hsv_to_rgb(uint8_t h, uint8_t s, uint8_t v, uint8_t *r,
                           uint8_t *g, uint8_t *b) {
  uint8_t region, remainder, p, q, t;

  if (s == 0) {
    *r = *g = *b = v;
    return;
  }

  region = h / 43;
  remainder = (h - (region * 43)) * 6;

  p = (v * (255 - s)) >> 8;
  q = (v * (255 - ((s * remainder) >> 8))) >> 8;
  t = (v * (255 - ((s * (255 - remainder)) >> 8))) >> 8;

  switch (region) {
  case 0:
    *r = v;
    *g = t;
    *b = p;
    break;
  case 1:
    *r = q;
    *g = v;
    *b = p;
    break;
  case 2:
    *r = p;
    *g = v;
    *b = t;
    break;
  case 3:
    *r = p;
    *g = q;
    *b = v;
    break;
  case 4:
    *r = t;
    *g = p;
    *b = v;
    break;
  default:
    *r = v;
    *g = p;
    *b = q;
    break;
  }
}

static void led_matrix_rgb_to_hsv(uint8_t r, uint8_t g, uint8_t b, uint8_t *h,
                                  uint8_t *s, uint8_t *v) {
  uint8_t rgb_min = r;
  uint8_t rgb_max = r;

  if (g < rgb_min) {
    rgb_min = g;
  }
  if (b < rgb_min) {
    rgb_min = b;
  }
  if (g > rgb_max) {
    rgb_max = g;
  }
  if (b > rgb_max) {
    rgb_max = b;
  }

  *v = rgb_max;

  {
    uint8_t delta = rgb_max - rgb_min;
    if (rgb_max == 0u || delta == 0u) {
      *h = 0u;
      *s = 0u;
      return;
    }

    *s = (uint8_t)(((uint16_t)delta * 255u) / rgb_max);

    {
      int16_t hue;
      if (rgb_max == r) {
        hue = (int16_t)(((int16_t)(g - b) * 43) / delta);
      } else if (rgb_max == g) {
        hue = (int16_t)(85 + (((int16_t)(b - r) * 43) / delta));
      } else {
        hue = (int16_t)(171 + (((int16_t)(r - g) * 43) / delta));
      }

      if (hue < 0) {
        hue += 255;
      } else if (hue > 255) {
        hue -= 255;
      }

      *h = (uint8_t)hue;
    }
  }
}

void led_matrix_test_rainbow(uint8_t offset) {
  for (uint8_t i = 0; i < LED_MATRIX_NUM_LEDS; i++) {
    uint8_t hue = offset + (uint8_t)(i * 3u);
    uint8_t r, g, b;
    led_matrix_hsv_to_rgb(hue, 255, 255, &r, &g, &b);
    led_matrix_set_pixel_idx(i, r, g, b);
  }
}

//--------------------------------------------------------------------+
// LED Effect Implementation
//--------------------------------------------------------------------+

void led_matrix_set_effect(led_effect_mode_t mode) {
  led_effect_mode_t previous_effect = current_effect;
  current_effect = mode;
  effect_offset = 0;
  last_effect_tick = 0;
  last_render_tick = 0;
  last_sim_tick = 0;
  memset(reactive_waves, 0, sizeof(reactive_waves));
  memset(reactive_heat_energy, 0, sizeof(reactive_heat_energy));
  memset(reactive_ghost_energy, 0, sizeof(reactive_ghost_energy));
  memset(mxfx_typing_heat_energy, 0, sizeof(mxfx_typing_heat_energy));
  mxfx_typing_heat_decay_timer_ms = 0u;
  memset(&mxfx_last_hit_tracker, 0, sizeof(mxfx_last_hit_tracker));
  impact_boost = 0u;

  // Restoring matrix mode must restore the exact saved pattern.
  if (mode == LED_EFFECT_STATIC_MATRIX && previous_effect != LED_EFFECT_STATIC_MATRIX) {
    sync_runtime_from_static();
    return;
  }

  if (initialized && display_enabled && mode != LED_EFFECT_THIRD_PARTY &&
      mode != LED_EFFECT_STATIC_MATRIX) {
    render_current_effect_frame();
  }
}

led_effect_mode_t led_matrix_get_effect(void) { return current_effect; }

void led_matrix_set_effect_color(uint8_t r, uint8_t g, uint8_t b) {
  if (LED_EFFECT_PARAM_COUNT <= LED_EFFECT_PARAM_COLOR_B) {
    return;
  }

  effect_params[LED_EFFECT_PARAM_COLOR_R] = r;
  effect_params[LED_EFFECT_PARAM_COLOR_G] = g;
  effect_params[LED_EFFECT_PARAM_COLOR_B] = b;
}

void led_matrix_set_effect_params(const uint8_t *params) {
  if (params == NULL) {
    return;
  }

  memcpy(effect_params, params, LED_EFFECT_PARAM_COUNT);
}

void led_matrix_get_effect_params(uint8_t *params) {
  if (params == NULL) {
    return;
  }

  memcpy(params, effect_params, LED_EFFECT_PARAM_COUNT);
}

void led_matrix_set_effect_speed(uint8_t speed) {
  effect_params[LED_EFFECT_PARAM_SPEED] = speed > 0u ? speed : 1u;
}

uint8_t led_matrix_get_effect_speed(void) {
  return effect_animation_speed();
}

void led_matrix_set_fps_limit(uint8_t fps) { fps_limit = fps; }

uint8_t led_matrix_get_fps_limit(void) { return fps_limit; }

void led_matrix_set_diagnostic_mode(uint8_t mode) {
  (void)mode;
}

uint8_t led_matrix_get_diagnostic_mode(void) { return 0u; }

void led_matrix_key_event(uint8_t key_index, bool pressed) {
  if (key_index >= NUM_KEYS)
    return;
  if (!pressed) {
    return;
  }

  mxfx_reactive_record_hit(key_index);

  uint8_t anchor_hue = 0u;
  uint8_t ignored_sat = 0u;
  uint8_t ignored_value = 0u;
  uint8_t x_bias = scale_coord_to_u8(led_pos_x[key_index], LED_LAYOUT_MAX_X);
  uint8_t y_bias = scale_coord_to_u8(led_pos_y[key_index], LED_LAYOUT_MAX_Y);
  uint8_t color_r = 255u;
  uint8_t color_g = 0u;
  uint8_t color_b = 0u;
  uint8_t slot = 0xFFu;

  effect_primary_color(255u, 0u, 0u, &color_r, &color_g, &color_b);
  led_matrix_rgb_to_hsv(color_r, color_g, color_b, &anchor_hue, &ignored_sat,
                        &ignored_value);

  for (uint8_t i = 0u; i < REACTIVE_WAVE_POOL_SIZE; i++) {
    if (!reactive_waves[i].active) {
      slot = i;
      break;
    }
  }
  if (slot == 0xFFu) {
    /* Pool is full.  Prefer evicting the oldest wave from the SAME source key
     * so that spamming one key produces overlapping rings that all stay visible,
     * rather than the latest press silently clobbering an unrelated wave. */
    uint16_t oldest_same_age = 0u;
    for (uint8_t i = 0u; i < REACTIVE_WAVE_POOL_SIZE; i++) {
      if (reactive_waves[i].source_key == key_index &&
          reactive_waves[i].age_ms >= oldest_same_age) {
        oldest_same_age = reactive_waves[i].age_ms;
        slot = i;
      }
    }
    /* No same-key wave found — fall back to evicting the globally oldest. */
    if (slot == 0xFFu) {
      uint16_t oldest_age = 0u;
      for (uint8_t i = 0u; i < REACTIVE_WAVE_POOL_SIZE; i++) {
        if (reactive_waves[i].age_ms >= oldest_age) {
          oldest_age = reactive_waves[i].age_ms;
          slot = i;
        }
      }
    }
  }

  reactive_waves[slot].active = true;
  reactive_waves[slot].source_key = key_index;
  reactive_waves[slot].hue =
      (uint8_t)(anchor_hue + (x_bias / 7u) + (y_bias / 11u));
  reactive_waves[slot].energy = 255u;
  reactive_waves[slot].age_ms = 0u;

  if (current_effect == LED_EFFECT_TYPING_HEATMAP) {
    uint8_t origin_x = mxfx_led_coord_x(key_index);
    uint8_t origin_y = mxfx_led_coord_y(key_index);

    // param 0 (heat_gain=224): energy per keypress. 1+(gain>>3) -> at 224 ~=29 (near original 32).
    uint8_t gain_energy = (uint8_t)(1u + (effect_param(0, 224u) >> 3u));
    // param 2 (diffusion=88): max neighbor spread amount 0..16. At 255 -> 16 (original).
    uint8_t max_spread = (uint8_t)(((uint16_t)effect_param(2, 88u) * 16u) / 255u);

    mxfx_typing_heat_energy[key_index] =
        mxfx_qadd8(mxfx_typing_heat_energy[key_index], gain_energy);

    for (uint8_t j = 0u; j < NUM_KEYS; j++) {
      if (j == key_index) {
        continue;
      }

      {
        int16_t dx = (int16_t)origin_x - (int16_t)mxfx_led_coord_x(j);
        int16_t dy = (int16_t)origin_y - (int16_t)mxfx_led_coord_y(j);
        uint8_t distance = mxfx_distance_u8(dx, dy);

        if (distance <= 40u && max_spread > 0u) {
          uint8_t amount = (uint8_t)(40u - distance);
          if (amount > max_spread) {
            amount = max_spread;
          }
          mxfx_typing_heat_energy[j] =
              mxfx_qadd8(mxfx_typing_heat_energy[j], amount);
        }
      }
    }
  }

  {
    uint16_t heat = (uint16_t)(reactive_heat_energy[key_index] + 320u);
    reactive_heat_energy[key_index] = (heat > 1024u) ? 1024u : heat;

    /* Heatmap-style keypress spread: add proportional energy to nearby keys at
     * press time rather than diffusing during rendering.  This gives the
     * sharp, localised spread seen in typing_heatmap.
     * HEATMAP_SPREAD_DIST: max spread radius in 0.1 mm units (~52 mm). */
    if (current_effect == LED_EFFECT_TYPING_HEATMAP) {
      /* Spread radius ~52 mm and max bleed energy at distance=0 */
      const uint16_t spread_dist   = 520u;
      const uint16_t spread_energy = 192u;
      for (uint8_t j = 0u; j < NUM_KEYS; j++) {
        if (j == key_index) {
          continue;
        }
        uint16_t dist = led_layout_distance(key_index, j);
        if (dist < spread_dist) {
          uint16_t falloff = (uint16_t)(((uint32_t)spread_energy *
                                         (spread_dist - dist)) /
                                        spread_dist);
          uint16_t new_heat = reactive_heat_energy[j] + falloff;
          reactive_heat_energy[j] = (new_heat > 1024u) ? 1024u : new_heat;
        }
      }
    }
  }
  {
    uint16_t ghost = (uint16_t)(reactive_ghost_energy[key_index] + 420u);
    reactive_ghost_energy[key_index] = (ghost > 1024u) ? 1024u : ghost;
  }

  impact_boost = (impact_boost > 820u) ? 1024u : (uint16_t)(impact_boost + 204u);
}

static void reactive_tick_simulation(uint16_t delta_ms) {
  uint32_t now_ms = HAL_GetTick();

  if (delta_ms == 0u) {
    return;
  }
  if (delta_ms > 180u) {
    delta_ms = 180u;
  }

  mxfx_reactive_tick_hits(delta_ms);

  if (audio_spectrum_valid &&
      (uint32_t)(now_ms - audio_spectrum_refresh_ms) > AUDIO_SPECTRUM_STALE_MS) {
    audio_spectrum_valid = false;
    memset(audio_spectrum_levels, 0, sizeof(audio_spectrum_levels));
    audio_impact_level = 0u;
  }

  if (audio_spectrum_valid) {
    uint16_t boosted = (uint16_t)audio_impact_level * 3u;
    if (boosted > impact_boost) {
      impact_boost = (uint16_t)((impact_boost + boosted) / 2u);
    }
  }

  if (impact_boost > 0u) {
    uint16_t decay = (uint16_t)(1u + (delta_ms * 3u) / 10u);
    impact_boost = (impact_boost > decay) ? (uint16_t)(impact_boost - decay) : 0u;
  }

  for (uint8_t i = 0u; i < REACTIVE_WAVE_POOL_SIZE; i++) {
    reactive_wave_t *wave = &reactive_waves[i];
    uint32_t next_age;

    if (!wave->active) {
      continue;
    }

    next_age = (uint32_t)wave->age_ms + delta_ms;
    if (next_age >= 1600u) {
      wave->active = false;
      wave->energy = 0u;
      wave->age_ms = 0u;
      continue;
    }

    wave->age_ms = (uint16_t)next_age;
    wave->energy = (uint8_t)(((1600u - next_age) * 255u) / 1600u);
  }

  for (uint8_t key = 0u; key < NUM_KEYS; key++) {
    if (reactive_heat_energy[key] > 0u) {
      uint16_t heat_decay = (uint16_t)(1u + (delta_ms * 5u) / 6u);
      reactive_heat_energy[key] =
          (reactive_heat_energy[key] > heat_decay)
              ? (uint16_t)(reactive_heat_energy[key] - heat_decay)
              : 0u;
    }

    if (reactive_ghost_energy[key] > 0u) {
      uint16_t ghost_decay = (uint16_t)(1u + (delta_ms * 7u) / 8u);
      reactive_ghost_energy[key] =
          (reactive_ghost_energy[key] > ghost_decay)
              ? (uint16_t)(reactive_ghost_energy[key] - ghost_decay)
              : 0u;
    }
  }
}

void led_matrix_set_progress_overlay(uint8_t level) {
  volume_overlay_active = true;
  volume_overlay_level = level;
  volume_overlay_expire_ms = HAL_GetTick() + 1200u;

  if (initialized && display_enabled) {
    update_ws2812();
  }
}

void led_matrix_clear_progress_overlay(void) {
  volume_overlay_active = false;

  if (initialized && display_enabled) {
    update_ws2812();
  }
}

void led_matrix_set_volume_overlay(uint8_t level) {
  led_matrix_set_progress_overlay(level);
}

void led_matrix_clear_volume_overlay(void) {
  led_matrix_clear_progress_overlay();
}

void led_matrix_set_host_volume_level(uint8_t level) {
  host_volume_level = level;
  host_volume_level_valid = true;
  host_volume_level_refresh_ms = HAL_GetTick();
}

void led_matrix_clear_host_volume_level(void) {
  host_volume_level_valid = false;
}

void led_matrix_show_host_volume_overlay(void) {
  uint32_t now_ms = HAL_GetTick();

  if (!host_volume_level_valid) {
    return;
  }

  if ((uint32_t)(now_ms - host_volume_level_refresh_ms) > HOST_VOLUME_LEVEL_STALE_MS) {
    host_volume_level_valid = false;
    return;
  }

  led_matrix_set_progress_overlay(host_volume_level);
}

void led_matrix_nudge_host_volume_overlay(int8_t direction, uint8_t steps) {
  uint32_t now_ms = HAL_GetTick();
  uint8_t base_level = 0u;
  int16_t signed_level = 0;
  uint16_t delta = 0u;

  if (steps == 0u || direction == 0) {
    led_matrix_show_host_volume_overlay();
    return;
  }

  if (host_volume_level_valid &&
      (uint32_t)(now_ms - host_volume_level_refresh_ms) <=
          HOST_VOLUME_LEVEL_STALE_MS) {
    base_level = host_volume_level;
  } else if (volume_overlay_active && !volume_overlay_is_expired(now_ms)) {
    base_level = volume_overlay_level;
  } else {
    led_matrix_show_host_volume_overlay();
    return;
  }

  delta = (uint16_t)steps * HOST_VOLUME_STEP_ESTIMATE;
  signed_level = (int16_t)base_level;
  if (direction > 0) {
    signed_level += (int16_t)delta;
  } else {
    signed_level -= (int16_t)delta;
  }

  if (signed_level < 0) {
    signed_level = 0;
  } else if (signed_level > 255) {
    signed_level = 255;
  }

  host_volume_level = (uint8_t)signed_level;
  host_volume_level_valid = true;
  host_volume_level_refresh_ms = now_ms;
  led_matrix_set_progress_overlay(host_volume_level);
}

void led_matrix_set_audio_spectrum(const uint8_t *bands, uint8_t band_count,
                                   uint8_t impact_level) {
  if (bands == NULL || band_count == 0u) {
    return;
  }

  if (band_count > LED_AUDIO_SPECTRUM_BAND_COUNT) {
    band_count = LED_AUDIO_SPECTRUM_BAND_COUNT;
  }

  memset(audio_spectrum_levels, 0, sizeof(audio_spectrum_levels));
  memcpy(audio_spectrum_levels, bands, band_count);
  audio_impact_level = impact_level;
  audio_spectrum_valid = true;
  audio_spectrum_refresh_ms = HAL_GetTick();
}

void led_matrix_clear_audio_spectrum(void) {
  memset(audio_spectrum_levels, 0, sizeof(audio_spectrum_levels));
  audio_impact_level = 0u;
  audio_spectrum_valid = false;
  audio_spectrum_refresh_ms = 0u;
}

void led_matrix_set_live_frame(const uint8_t *data) {
  if (data == NULL) {
    return;
  }

  memcpy(pixels_runtime, data, LED_MATRIX_DATA_SIZE);

  if (initialized && display_enabled) {
    update_ws2812();
  }
}

void led_matrix_set_output_override_frame(const uint8_t *data) {
  if (data == NULL) {
    return;
  }

  memcpy(pixels_output_override, data, LED_MATRIX_DATA_SIZE);
  output_override_active = true;

  if (initialized && display_enabled) {
    update_ws2812();
  }
}

void led_matrix_clear_output_override_frame(void) {
  output_override_active = false;

  if (initialized && display_enabled) {
    update_ws2812();
  }
}

/**
 * @brief Rainbow Wave effect - diagonal rainbow cycling
 */
// Effect implementations are split by file for maintainability.
#include "led_effects/effect_plasma.inc"
#include "led_effects/effect_fire.inc"
#include "led_effects/effect_ocean.inc"
#include "led_effects/effect_sparkle.inc"
#include "led_effects/effect_breathing_rainbow.inc"
#include "led_effects/effect_color_cycle.inc"
#include "led_effects/effect_distance_sensor.inc"
#include "led_effects/effect_impact_rainbow.inc"
#include "led_effects/effect_reactive_ghost.inc"
#include "led_effects/effect_audio_spectrum.inc"
#include "led_effects/effect_key_state_demo.inc"
#include "led_effects/effect_cycle_pinwheel.inc"
#include "led_effects/effect_cycle_spiral.inc"
#include "led_effects/effect_cycle_out_in_dual.inc"
#include "led_effects/effect_rainbow_beacon.inc"
#include "led_effects/effect_rainbow_pinwheels.inc"
#include "led_effects/effect_rainbow_moving_chevron.inc"
#include "led_effects/effect_hue_breathing.inc"
#include "led_effects/effect_hue_pendulum.inc"
#include "led_effects/effect_hue_wave.inc"
#include "led_effects/effect_riverflow.inc"
#include "led_effects/effect_solid_color.inc"
#include "led_effects/effect_alpha_mods.inc"
#include "led_effects/effect_gradient_up_down.inc"
#include "led_effects/effect_gradient_left_right.inc"
#include "led_effects/effect_breathing.inc"
#include "led_effects/effect_colorband_sat.inc"
#include "led_effects/effect_colorband_val.inc"
#include "led_effects/effect_colorband_pinwheel_sat.inc"
#include "led_effects/effect_colorband_pinwheel_val.inc"
#include "led_effects/effect_colorband_spiral_sat.inc"
#include "led_effects/effect_colorband_spiral_val.inc"
#include "led_effects/effect_cycle_all.inc"
#include "led_effects/effect_cycle_left_right.inc"
#include "led_effects/effect_cycle_up_down.inc"
#include "led_effects/effect_cycle_out_in.inc"
#include "led_effects/effect_dual_beacon.inc"
#include "led_effects/effect_flower_blooming.inc"
#include "led_effects/effect_raindrops.inc"
#include "led_effects/effect_jellybean_raindrops.inc"
#include "led_effects/effect_pixel_rain.inc"
#include "led_effects/effect_pixel_flow.inc"
#include "led_effects/effect_pixel_fractal.inc"
#include "led_effects/effect_typing_heatmap.inc"
#include "led_effects/effect_digital_rain.inc"
#include "led_effects/effect_solid_reactive_simple.inc"
#include "led_effects/effect_solid_reactive.inc"
#include "led_effects/effect_solid_reactive_wide.inc"
#include "led_effects/effect_solid_reactive_cross.inc"
#include "led_effects/effect_solid_reactive_nexus.inc"
#include "led_effects/effect_splash.inc"
#include "led_effects/effect_solid_splash.inc"
#include "led_effects/effect_starlight_smooth.inc"
#include "led_effects/effect_starlight.inc"
#include "led_effects/effect_starlight_dual_sat.inc"
#include "led_effects/effect_starlight_dual_hue.inc"

void led_matrix_effect_tick(uint32_t tick) {
  if (!initialized || !display_enabled) {
    return;
  }

  if (volume_overlay_is_expired(tick)) {
    volume_overlay_active = false;
    update_ws2812();
  }

  // Calculate time deltas
  uint32_t effect_delta = tick - last_effect_tick;
  uint32_t render_delta = tick - last_render_tick;
  uint32_t sim_delta = 0u;

  if (last_sim_tick == 0u) {
    last_sim_tick = tick;
  } else {
    sim_delta = tick - last_sim_tick;
    last_sim_tick = tick;
  }

  if (sim_delta > 0u) {
    reactive_tick_simulation(
        (uint16_t)((sim_delta > 65535u) ? 65535u : sim_delta));
  }

  // Effect animation speed (controls how fast effect_offset increments)
  // speed 1 = 200ms interval, speed 255 = 5ms interval
  uint8_t speed = effect_animation_speed();
  uint32_t effect_interval = 200u - ((uint32_t)speed * 195u / 255u);
  if (effect_interval < 5)
    effect_interval = 5;

  // Update effect_offset based on effect speed (independent of render rate)
  // This ensures animation speed stays constant regardless of FPS limit
  if (effect_delta >= effect_interval) {
    // Calculate how many steps to advance (handles frame skipping)
    uint16_t steps = effect_delta / effect_interval;
    effect_offset += steps;
    last_effect_tick =
        tick -
        (effect_delta % effect_interval); // Keep remainder for smoother timing
  }

  // FPS limit controls how often we actually render
  // fps_limit = 0 means unlimited (render every call)
  if (fps_limit > 0) {
    uint32_t min_render_interval = 1000 / fps_limit;
    if (render_delta < min_render_interval) {
      return; // Skip this render frame
    }
  }

  last_render_tick = tick;

  // Matrix static mode and third-party mode do not run internal animations.
  if (current_effect == LED_EFFECT_STATIC_MATRIX ||
      current_effect == LED_EFFECT_THIRD_PARTY) {
    return;
  }

  // Now render the effect using current effect_offset
  render_current_effect_frame();
}

//--------------------------------------------------------------------+
// Per-effect parameter schema (Phase-2A)
//--------------------------------------------------------------------+

// Descriptor initialiser helpers.
#define PDESC_U8(id_, min_, max_, def_, step_) \
  { (id_), LED_PARAM_TYPE_U8, (min_), (max_), (def_), (step_) }
#define PDESC_BOOL(id_, def_) \
  { (id_), LED_PARAM_TYPE_BOOL, 0u, 1u, (def_), 1u }
#define PDESC_HUE(id_, def_) \
  { (id_), LED_PARAM_TYPE_HUE, 0u, 255u, (def_), 1u }
#define PDESC_COLOR(id_, dr_, dg_, db_) \
  { (id_),     LED_PARAM_TYPE_COLOR, 0u, 255u, (dr_), 0u }, \
  { (id_)+1u,  LED_PARAM_TYPE_COLOR, 0u, 255u, (dg_), 0u }, \
  { (id_)+2u,  LED_PARAM_TYPE_COLOR, 0u, 255u, (db_), 0u }
#define PDESC_SPEED(def_) \
  PDESC_U8(LED_EFFECT_PARAM_SPEED, 1u, 255u, (def_), 1u)

// Shared sets reused by many effects.
#define PSET_SPEED50  PDESC_SPEED(50u)
#define PSET_SPEED128 PDESC_SPEED(128u)
#define PSET_COLOR1(r,g,b) \
  PDESC_COLOR(LED_EFFECT_PARAM_COLOR_R, (r), (g), (b))
#define PSET_COLOR12(r1,g1,b1, r2,g2,b2) \
  PDESC_COLOR(LED_EFFECT_PARAM_COLOR_R,  (r1),(g1),(b1)), \
  PDESC_COLOR(LED_EFFECT_PARAM_COLOR2_R, (r2),(g2),(b2))

uint8_t led_matrix_get_effect_schema(uint8_t effect_mode,
                                     led_param_desc_t *out) {
  if (out == NULL || effect_mode >= (uint8_t)LED_EFFECT_MAX) {
    return 0u;
  }

  // Temporary stack buffer; we copy into *out and count non-NONE entries.
  led_param_desc_t buf[LED_EFFECT_PARAM_COUNT];
  uint8_t n = 0u;

  // Zero-initialise (type = NONE).
  for (uint8_t i = 0u; i < LED_EFFECT_PARAM_COUNT; i++) {
    buf[i].id          = i;
    buf[i].type        = LED_PARAM_TYPE_NONE;
    buf[i].min         = 0u;
    buf[i].max         = 0u;
    buf[i].default_val = 0u;
    buf[i].step        = 0u;
  }

// Inline helper to append a descriptor into buf[] and count it.
#define P(desc) do { led_param_desc_t _d = desc; buf[_d.id] = _d; n++; } while(0)
// Inline helper for COLOR triplets (3 slots).
#define PCOLOR(base_id, dr, dg, db) do { \
    buf[(base_id)+0u] = (led_param_desc_t){(base_id)+0u, LED_PARAM_TYPE_COLOR, 0u, 255u, (dr), 0u}; \
    buf[(base_id)+1u] = (led_param_desc_t){(base_id)+1u, LED_PARAM_TYPE_COLOR, 0u, 255u, (dg), 0u}; \
    buf[(base_id)+2u] = (led_param_desc_t){(base_id)+2u, LED_PARAM_TYPE_COLOR, 0u, 255u, (db), 0u}; \
    n += 3u; \
  } while(0)

  switch (effect_mode) {
  case LED_EFFECT_NONE:
    // Static matrix: no animated params.
    break;

  case LED_EFFECT_PLASMA:
    P(PDESC_U8(0, 0u, 255u, 96u,  8u));  // Motion depth
    P(PDESC_U8(1, 0u, 255u, 192u, 8u));  // Saturation
    P(PDESC_U8(2, 0u, 255u, 128u, 8u));  // Radial warp
    P(PDESC_U8(3, 0u, 255u, 255u, 8u));  // Value
    P(PSET_SPEED50);
    break;

  case LED_EFFECT_FIRE:
    P(PDESC_U8(0, 0u, 255u, 160u, 8u));  // Heat boost
    P(PDESC_U8(1, 0u, 255u, 96u,  8u));  // Ember floor
    P(PDESC_U8(2, 0u, 255u, 96u,  8u));  // Cooling
    P(PDESC_BOOL(3, 0u));                 // Palette (0=fire, 1=ice)
    P(PSET_SPEED50);
    break;

  case LED_EFFECT_OCEAN:
    P(PDESC_U8(0, 0u, 255u, 160u, 8u));  // Hue bias
    P(PDESC_U8(1, 0u, 255u, 64u,  8u));  // Depth dimming
    P(PDESC_BOOL(2, 1u));                 // Foam highlight
    P(PDESC_U8(3, 0u, 255u, 160u, 8u));  // Crest speed
    P(PSET_SPEED50);
    break;

  case LED_EFFECT_SPARKLE:
    P(PDESC_U8(0, 0u, 255u, 48u,  4u));  // Density
    P(PDESC_U8(1, 0u, 255u, 224u, 8u));  // Sparkle brightness
    P(PDESC_U8(2, 0u, 255u, 160u, 8u));  // Rainbow mix
    P(PDESC_U8(3, 0u, 255u, 24u,  4u));  // Ambient glow
    PCOLOR(LED_EFFECT_PARAM_COLOR_R, 255u, 210u, 96u);
    P(PSET_SPEED50);
    break;

  case LED_EFFECT_BREATHING_RAINBOW:
    P(PDESC_U8(0, 0u, 255u, 24u,  4u));  // Brightness floor
    P(PDESC_U8(1, 0u, 255u, 192u, 8u));  // Hue drift
    P(PDESC_U8(2, 0u, 255u, 255u, 8u));  // Saturation
    P(PSET_SPEED50);
    break;

  case LED_EFFECT_COLOR_CYCLE:
    P(PDESC_U8(0, 0u, 255u, 64u,  4u));  // Hue step
    P(PDESC_U8(1, 0u, 255u, 255u, 8u));  // Saturation
    P(PDESC_U8(2, 0u, 255u, 255u, 8u));  // Value
    P(PDESC_U8(3, 0u, 255u, 0u,   4u));  // Effect-color mix
    P(PSET_SPEED50);
    break;

  case LED_EFFECT_DISTANCE_SENSOR:
    P(PDESC_U8(0, 0u, 255u, 32u,  4u));  // Brightness floor
    P(PDESC_HUE(1, 170u));               // Hue span
    P(PDESC_U8(2, 0u, 255u, 255u, 8u));  // Saturation
    P(PDESC_BOOL(3, 0u));                 // Reverse gradient
    break;

  case LED_EFFECT_IMPACT_RAINBOW:
    P(PDESC_U8(0, 0u, 2u,   2u,   1u));  // Boost mode
    P(PDESC_U8(1, 0u, 255u, 184u, 8u));  // Boost decay
    P(PDESC_U8(2, 0u, 255u, 96u,  8u));  // Key boost
    P(PDESC_U8(3, 0u, 255u, 88u,  8u));  // Audio boost
    P(PDESC_U8(4, 0u, 255u, 208u, 8u));  // Max boost
    P(PDESC_U8(5, 0u, 255u, 16u,  4u));  // Angle
    P(PDESC_U8(6, 0u, 255u, 255u, 8u));  // Saturation
    P(PDESC_U8(7, 0u, 255u, 168u, 8u));  // Wave drift
    P(PSET_SPEED50);
    break;

  case LED_EFFECT_REACTIVE_GHOST:
    P(PDESC_U8(0, 0u, 255u, 100u, 4u));  // Decay
    P(PDESC_U8(1, 0u, 255u, 156u, 4u));  // Spread
    P(PDESC_U8(2, 0u, 255u, 184u, 4u));  // Trail
    P(PDESC_U8(3, 0u, 255u, 208u, 8u));  // Gain
    PCOLOR(LED_EFFECT_PARAM_COLOR_R, 72u, 180u, 255u);
    P(PSET_SPEED50);
    break;

  case LED_EFFECT_AUDIO_SPECTRUM:
    P(PDESC_HUE(0, 176u));               // Hue span
    P(PDESC_U8(1, 0u, 255u, 32u,  4u));  // Base floor
    P(PDESC_U8(2, 0u, 255u, 208u, 8u));  // Peak gain
    P(PDESC_BOOL(3, 1u));                 // Mirror
    P(PDESC_U8(4, 0u, 255u, 172u, 8u));  // Decay
    P(PDESC_U8(5, 0u, 3u,   0u,   1u));  // Visualizer mode
    P(PDESC_U8(6, 0u, 255u, 236u, 4u));  // Contrast
    PCOLOR(LED_EFFECT_PARAM_COLOR_R, 64u, 255u, 160u);
    break;

  case LED_EFFECT_KEY_STATE_DEMO:
    P(PDESC_BOOL(0, 0u));                 // Invert mapping
    P(PDESC_U8(1, 0u, 255u, 255u, 8u));  // Pressed brightness
    P(PDESC_U8(2, 0u, 255u, 96u,  8u));  // Released brightness
    PCOLOR(LED_EFFECT_PARAM_COLOR_R,  0u, 255u, 0u);
    PCOLOR(LED_EFFECT_PARAM_COLOR2_R, 255u, 0u, 0u);
    break;

  // --- Fused effects ---
  case LED_EFFECT_SOLID_COLOR:
    P(PDESC_U8(0, 0u, 255u, 255u, 8u));  // Brightness trim
    PCOLOR(LED_EFFECT_PARAM_COLOR_R, 255u, 0u, 0u);
    break;

  case LED_EFFECT_BREATHING:
    P(PDESC_U8(0, 0u, 255u, 24u,  4u));  // Brightness floor
    P(PDESC_U8(1, 0u, 255u, 255u, 8u));  // Brightness ceiling
    P(PDESC_U8(2, 0u, 255u, 48u,  4u));  // Plateau
    PCOLOR(LED_EFFECT_PARAM_COLOR_R, 255u, 0u, 0u);
    P(PSET_SPEED50);
    break;

  case LED_EFFECT_GRADIENT_LEFT_RIGHT:
    P(PDESC_U8(0, 0u, 255u, 160u, 8u));  // Horizontal scale
    P(PDESC_U8(1, 0u, 255u, 120u, 8u));  // Vertical scale
    P(PDESC_U8(2, 0u, 255u, 144u, 8u));  // Saturation
    P(PDESC_U8(3, 0u, 255u, 255u, 8u));  // Value
    P(PSET_SPEED50);
    break;

  case LED_EFFECT_GRADIENT_UP_DOWN:
    P(PDESC_U8(0, 0u, 255u, 160u, 8u));  // Vertical hue spread
    P(PDESC_U8(1, 0u, 255u, 144u, 8u));  // Saturation
    P(PDESC_U8(2, 0u, 255u, 255u, 8u));  // Value
    PCOLOR(LED_EFFECT_PARAM_COLOR_R, 255u, 0u, 0u);
    P(PSET_SPEED50);
    break;

  case LED_EFFECT_CYCLE_LEFT_RIGHT:
    P(PDESC_U8(0, 0u, 255u, 160u, 8u));  // Horizontal hue spread
    P(PDESC_U8(1, 0u, 255u, 96u,  8u));  // Vertical hue contribution
    // param 2: removed (was drift multiplier — caused jump on uint8 time wrap)
    P(PDESC_U8(3, 0u, 255u, 255u, 8u));  // Saturation
    P(PDESC_U8(4, 0u, 255u, 0u,   4u));  // Gradient tilt angle
    P(PDESC_U8(5, 0u, 255u, 0u,   4u));  // Sinusoidal warp
    P(PSET_SPEED50);
    break;

  case LED_EFFECT_DIGITAL_RAIN:
    P(PDESC_U8(0, 0u, 255u, 64u,  4u));  // Trail length
    P(PDESC_U8(1, 0u, 255u, 160u, 8u));  // Head size
    P(PDESC_U8(2, 0u, 255u, 96u,  8u));  // Density
    P(PDESC_BOOL(3, 1u));                 // White heads
    P(PDESC_HUE(4, 0u));                 // Hue bias
    P(PSET_SPEED50);
    break;

  case LED_EFFECT_TYPING_HEATMAP:
    P(PDESC_U8(0, 0u, 255u, 224u, 8u));  // Heat gain
    P(PDESC_U8(1, 0u, 255u, 168u, 8u));  // Decay
    P(PDESC_U8(2, 0u, 255u, 88u,  8u));  // Diffusion
    P(PDESC_U8(3, 0u, 255u, 48u,  4u));  // Floor
    P(PSET_SPEED50);
    break;

  case LED_EFFECT_SPLASH:
  case LED_EFFECT_MULTI_SPLASH:
    P(PDESC_U8(0, 0u, 255u, 72u,  4u));  // Decay / lifetime
    P(PDESC_U8(1, 0u, 255u, 128u, 8u));  // Spread / radius
    P(PDESC_U8(2, 0u, 255u, 8u,   4u));  // Base glow
    P(PDESC_BOOL(3, 1u));                 // White core / burst
    P(PDESC_U8(4, 0u, 255u, 224u, 8u));  // Gain
    P(PDESC_BOOL(5, 0u));                 // Palette (0=custom, 1=rainbow)
    P(PDESC_BOOL(6, 0u));                 // Mode (0=QMK, 1=physics ring+trail)
    PCOLOR(LED_EFFECT_PARAM_COLOR_R, 0u, 255u, 96u);
    P(PSET_SPEED50);
    break;

  case LED_EFFECT_SOLID_SPLASH:
  case LED_EFFECT_SOLID_MULTI_SPLASH:
    PCOLOR(LED_EFFECT_PARAM_COLOR_R, 255u, 0u, 0u);
    P(PSET_SPEED50);
    break;

  case LED_EFFECT_SOLID_REACTIVE_SIMPLE:
  case LED_EFFECT_SOLID_REACTIVE:
  case LED_EFFECT_SOLID_REACTIVE_WIDE:
  case LED_EFFECT_SOLID_REACTIVE_CROSS:
  case LED_EFFECT_SOLID_REACTIVE_NEXUS:
  case LED_EFFECT_SOLID_REACTIVE_MULTI_WIDE:
  case LED_EFFECT_SOLID_REACTIVE_MULTI_CROSS:
  case LED_EFFECT_SOLID_REACTIVE_MULTI_NEXUS:
    PCOLOR(LED_EFFECT_PARAM_COLOR_R, 255u, 0u, 0u);
    P(PSET_SPEED50);
    break;

  case LED_EFFECT_STARLIGHT_SMOOTH:
  case LED_EFFECT_STARLIGHT:
    PCOLOR(LED_EFFECT_PARAM_COLOR_R, 255u, 0u, 0u);
    P(PSET_SPEED50);
    break;

  case LED_EFFECT_STARLIGHT_DUAL_SAT:
    P(PDESC_U8(0, 0u, 255u, 31u, 4u));  // Saturation spread
    PCOLOR(LED_EFFECT_PARAM_COLOR_R, 255u, 0u, 0u);
    P(PSET_SPEED50);
    break;

  case LED_EFFECT_STARLIGHT_DUAL_HUE:
    P(PDESC_U8(0, 0u, 255u, 31u, 4u));  // Hue spread
    PCOLOR(LED_EFFECT_PARAM_COLOR_R, 255u, 0u, 0u);
    P(PSET_SPEED50);
    break;

  case LED_EFFECT_HUE_BREATHING:
    P(PDESC_U8(0, 0u, 255u, 12u, 4u));  // Hue swing range
    PCOLOR(LED_EFFECT_PARAM_COLOR_R, 255u, 0u, 0u);
    P(PSET_SPEED50);
    break;

  case LED_EFFECT_HUE_PENDULUM:
    P(PDESC_U8(0, 0u, 255u, 12u, 4u));  // Hue swing range
    PCOLOR(LED_EFFECT_PARAM_COLOR_R, 255u, 0u, 0u);
    P(PSET_SPEED50);
    break;

  case LED_EFFECT_HUE_WAVE:
    P(PDESC_U8(0, 0u, 255u, 24u, 4u));  // Wave amplitude
    PCOLOR(LED_EFFECT_PARAM_COLOR_R, 255u, 0u, 0u);
    P(PSET_SPEED50);
    break;

  case LED_EFFECT_ALPHA_MODS:
    P(PDESC_U8(0, 0u, 255u, 0u, 4u));   // Hue offset (0=dynamic via speed)
    PCOLOR(LED_EFFECT_PARAM_COLOR_R, 255u, 0u, 0u);
    P(PSET_SPEED50);
    break;

  case LED_EFFECT_PIXEL_FRACTAL:
    P(PDESC_U8(0, 0u, 255u, 255u, 8u));  // Saturation
    P(PDESC_U8(1, 0u, 255u, 255u, 8u));  // Value
    PCOLOR(LED_EFFECT_PARAM_COLOR_R, 255u, 0u, 0u);
    P(PSET_SPEED50);
    break;

  // Effects with speed + color (hue/sat/val via PCOLOR):
  case LED_EFFECT_CYCLE_ALL:
  case LED_EFFECT_CYCLE_UP_DOWN:
  case LED_EFFECT_CYCLE_OUT_IN:
  case LED_EFFECT_CYCLE_PINWHEEL:
  case LED_EFFECT_CYCLE_SPIRAL:
  case LED_EFFECT_CYCLE_OUT_IN_DUAL:
  case LED_EFFECT_RAINBOW_BEACON:
  case LED_EFFECT_RAINBOW_PINWHEELS:
  case LED_EFFECT_RAINBOW_MOVING_CHEVRON:
  case LED_EFFECT_DUAL_BEACON:
  case LED_EFFECT_FLOWER_BLOOMING:
  case LED_EFFECT_RIVERFLOW:
  case LED_EFFECT_COLORBAND_SAT:
  case LED_EFFECT_COLORBAND_VAL:
  case LED_EFFECT_COLORBAND_PINWHEEL_SAT:
  case LED_EFFECT_COLORBAND_PINWHEEL_VAL:
  case LED_EFFECT_COLORBAND_SPIRAL_SAT:
  case LED_EFFECT_COLORBAND_SPIRAL_VAL:
  case LED_EFFECT_RAINDROPS:
  case LED_EFFECT_JELLYBEAN_RAINDROPS:
  case LED_EFFECT_PIXEL_RAIN:
  case LED_EFFECT_PIXEL_FLOW:
    PCOLOR(LED_EFFECT_PARAM_COLOR_R, 255u, 0u, 0u);  // Base color (QMK default: red)
    P(PSET_SPEED50);
    break;

  // Effects with no tunable params (live/hardware driven):
  case LED_EFFECT_THIRD_PARTY:
    break;

  default:
    break;
  }

#undef P
#undef PCOLOR

  // Copy buffer to output.
  for (uint8_t i = 0u; i < LED_EFFECT_PARAM_COUNT; i++) {
    out[i] = buf[i];
  }

  return n;
}


