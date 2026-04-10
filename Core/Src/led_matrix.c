/*
 * led_matrix.c
 * RGB LED strip/matrix driver using WS2812
 */

#include "led_matrix.h"
#include "analog/analog.h"
#include "led_indicator.h"
#include "main.h"
#include "settings.h"
// #include "trigger.h"
#include "ws2812.h"
#include <string.h>


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
static uint8_t effect_color_r = 255;
static uint8_t effect_color_g = 0;
static uint8_t effect_color_b = 0;
static uint8_t effect_speed = 50;
static uint8_t fps_limit = 60; // Default 60 FPS
static uint8_t effect_params[LED_EFFECT_PARAM_COUNT] = {128u, 128u};
static uint32_t last_effect_tick = 0;
static uint32_t last_render_tick = 0; // Separate tick for FPS limiting
static uint16_t effect_offset = 0;

// Effect renderer context flag: when true, pixel writes target runtime frame only.
static bool effect_render_context = false;

// Reactive effect state
static uint8_t key_wave_energy[NUM_KEYS] = {0};
static uint16_t key_wave_age_ms[NUM_KEYS] = {0};
static uint8_t key_wave_hue[NUM_KEYS] = {0};
static uint16_t reactive_frame_delta_ms = 16u;

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

static void effect_rainbow(void);
static void effect_breathing(void);
static void effect_static_rainbow(void);
static void effect_solid(void);
static void effect_plasma(void);
static void effect_fire(void);
static void effect_ocean(void);
static void effect_matrix(void);
static void effect_sparkle(void);
static void effect_breathing_rainbow(void);
static void effect_spiral(void);
static void effect_color_cycle(void);
static void effect_distance_sensor(void);
static void effect_reactive(void);

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

static inline bool effect_param_flag(uint8_t index, bool fallback) {
  return effect_param(index, fallback ? 1u : 0u) != 0u;
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

  switch (effect_mode) {
  case LED_EFFECT_STATIC_MATRIX: {
    uint8_t key = volume_overlay_keys[slot];
    *r = pixels_static[key * 3u + 0u];
    *g = pixels_static[key * 3u + 1u];
    *b = pixels_static[key * 3u + 2u];
    if (*r == 0u && *g == 0u && *b == 0u) {
      *r = effect_color_r;
      *g = effect_color_g;
      *b = effect_color_b;
    }
    break;
  }
  case LED_EFFECT_SOLID:
    *r = effect_color_r;
    *g = effect_color_g;
    *b = effect_color_b;
    break;
  case LED_EFFECT_BREATHING: {
    uint8_t scale = (uint8_t)(32u + (((uint16_t)wave * 223u) / 255u));
    *r = scale_u8(effect_color_r, scale);
    *g = scale_u8(effect_color_g, scale);
    *b = scale_u8(effect_color_b, scale);
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
  case LED_EFFECT_MATRIX:
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
  case LED_EFFECT_RAINBOW:
  case LED_EFFECT_STATIC_RAINBOW:
  case LED_EFFECT_BREATHING_RAINBOW:
  case LED_EFFECT_COLOR_CYCLE:
    led_matrix_hsv_to_rgb(anim_phase, 255u, 255u, r, g, b);
    break;
  case LED_EFFECT_PLASMA:
  case LED_EFFECT_SPIRAL:
  case LED_EFFECT_REACTIVE:
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
  case LED_EFFECT_RAINBOW:
    effect_rainbow();
    break;
  case LED_EFFECT_BREATHING:
    effect_breathing();
    break;
  case LED_EFFECT_STATIC_RAINBOW:
    effect_static_rainbow();
    break;
  case LED_EFFECT_SOLID:
    effect_solid();
    break;
  case LED_EFFECT_PLASMA:
    effect_plasma();
    break;
  case LED_EFFECT_FIRE:
    effect_fire();
    break;
  case LED_EFFECT_OCEAN:
    effect_ocean();
    break;
  case LED_EFFECT_MATRIX:
    effect_matrix();
    break;
  case LED_EFFECT_SPARKLE:
    effect_sparkle();
    break;
  case LED_EFFECT_BREATHING_RAINBOW:
    effect_breathing_rainbow();
    break;
  case LED_EFFECT_SPIRAL:
    effect_spiral();
    break;
  case LED_EFFECT_COLOR_CYCLE:
    effect_color_cycle();
    break;
  case LED_EFFECT_REACTIVE:
    effect_reactive();
    break;
  case LED_EFFECT_DISTANCE_SENSOR:
    effect_distance_sensor();
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
  memset(key_wave_energy, 0, sizeof(key_wave_energy));
  memset(key_wave_age_ms, 0, sizeof(key_wave_age_ms));
  memset(key_wave_hue, 0, sizeof(key_wave_hue));
  reactive_frame_delta_ms = 16u;

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
  effect_color_r = r;
  effect_color_g = g;
  effect_color_b = b;
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
  effect_speed = speed > 0 ? speed : 1;
}

uint8_t led_matrix_get_effect_speed(void) { return effect_speed; }

void led_matrix_set_fps_limit(uint8_t fps) { fps_limit = fps; }

uint8_t led_matrix_get_fps_limit(void) { return fps_limit; }

void led_matrix_set_diagnostic_mode(uint8_t mode) {
  (void)mode;
}

uint8_t led_matrix_get_diagnostic_mode(void) { return 0u; }

void led_matrix_key_event(uint8_t key_index, bool pressed) {
  if (key_index >= NUM_KEYS)
    return;
  if (pressed) {
    uint8_t anchor_hue = 0u;
    uint8_t ignored_sat = 0u;
    uint8_t ignored_value = 0u;
    uint8_t x_bias = scale_coord_to_u8(led_pos_x[key_index], LED_LAYOUT_MAX_X);
    uint8_t y_bias = scale_coord_to_u8(led_pos_y[key_index], LED_LAYOUT_MAX_Y);

    led_matrix_rgb_to_hsv(effect_color_r, effect_color_g, effect_color_b,
                          &anchor_hue, &ignored_sat, &ignored_value);
    key_wave_energy[key_index] = 255;
    key_wave_age_ms[key_index] = 0u;
    key_wave_hue[key_index] =
        (uint8_t)(anchor_hue + (x_bias / 7u) + (y_bias / 11u));
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
static void effect_rainbow(void) {
  uint8_t x_mul =
      (uint8_t)(1u + ((uint16_t)effect_param(0, 160u) * 4u) / 255u);
  uint8_t y_div = (uint8_t)(2u + ((uint16_t)effect_param(1, 96u) * 6u) / 255u);
  uint8_t anim_mul =
      (uint8_t)(1u + ((uint16_t)effect_param(2, 160u) * 5u) / 255u);
  uint8_t sat = effect_param(3, 255u);
  for (uint8_t i = 0; i < LED_MATRIX_NUM_LEDS; i++) {
    uint8_t hue = (uint8_t)(scale_coord_to_u8(led_pos_x[i], LED_LAYOUT_MAX_X) *
                            x_mul) +
                  (scale_coord_to_u8(led_pos_y[i], LED_LAYOUT_MAX_Y) / y_div) +
                  (uint8_t)(effect_offset * anim_mul);
    uint8_t r, g, b;
    led_matrix_hsv_to_rgb(hue, sat, 255, &r, &g, &b);
    led_matrix_set_pixel_idx(i, r, g, b);
  }
}

/**
 * @brief Breathing effect - fade in/out with selected color
 */
static void effect_breathing(void) {
  uint8_t min_brightness = effect_param(0, 24u);
  uint8_t max_brightness = effect_param(1, 255u);
  uint8_t plateau = (uint8_t)(((uint16_t)effect_param(2, 48u) * 20u) / 255u);
  if (max_brightness < min_brightness) {
    uint8_t tmp = max_brightness;
    max_brightness = min_brightness;
    min_brightness = tmp;
  }
  uint8_t breath = (effect_offset % 128);
  if (breath > 64)
    breath = 128 - breath;
  if (plateau > 0u && breath > (uint8_t)(64u - plateau)) {
    breath = 64u;
  }
  uint8_t brightness = (uint8_t)(
      min_brightness +
      (((uint16_t)(max_brightness - min_brightness) * breath) / 64u));

  uint8_t r = (uint8_t)((uint16_t)effect_color_r * brightness / 255);
  uint8_t g = (uint8_t)((uint16_t)effect_color_g * brightness / 255);
  uint8_t b = (uint8_t)((uint16_t)effect_color_b * brightness / 255);

  led_matrix_fill(r, g, b);
}

/**
 * @brief Static Rainbow - static rainbow pattern
 */
static void effect_static_rainbow(void) {
  uint8_t x_mul =
      (uint8_t)(1u + ((uint16_t)effect_param(0, 160u) * 4u) / 255u);
  uint8_t y_div = (uint8_t)(1u + ((uint16_t)effect_param(1, 120u) * 5u) / 255u);
  uint8_t sat = effect_param(2, 144u);
  uint8_t value = effect_param(3, 255u);
  for (uint8_t i = 0; i < LED_MATRIX_NUM_LEDS; i++) {
    uint8_t hue = (uint8_t)(scale_coord_to_u8(led_pos_x[i], LED_LAYOUT_MAX_X) *
                            x_mul) +
                  (scale_coord_to_u8(led_pos_y[i], LED_LAYOUT_MAX_Y) / y_div);
    uint8_t r, g, b;
    led_matrix_hsv_to_rgb(hue, sat, value, &r, &g, &b);
    led_matrix_set_pixel_idx(i, r, g, b);
  }
}

/**
 * @brief Solid color effect
 */
static void effect_solid(void) {
  uint8_t brightness = effect_param(0, 255u);
  led_matrix_fill(
      (uint8_t)(((uint16_t)effect_color_r * brightness) / 255u),
      (uint8_t)(((uint16_t)effect_color_g * brightness) / 255u),
      (uint8_t)(((uint16_t)effect_color_b * brightness) / 255u));
}

/**
 * @brief Plasma effect - psychedelic waves
 */
static void effect_plasma(void) {
  uint8_t anim_mul =
      (uint8_t)(1u + ((uint16_t)effect_param(0, 96u) * 4u) / 255u);
  uint8_t sat = effect_param(1, 192u);
  uint8_t radial_div = (uint8_t)(12u + effect_param(2, 128u));
  uint8_t value = effect_param(3, 255u);
  for (uint8_t i = 0; i < LED_MATRIX_NUM_LEDS; i++) {
    uint16_t x = led_pos_x[i] / 12u;
    uint16_t y = led_pos_y[i] / 12u;
    int16_t cx = (int16_t)(led_pos_x[i] / 10u) - (int16_t)(LED_LAYOUT_CENTER_X / 10u);
    int16_t cy = (int16_t)(led_pos_y[i] / 10u) - (int16_t)(LED_LAYOUT_CENTER_Y / 10u);
    uint8_t v1 = (uint8_t)(x + effect_offset * anim_mul);
    uint8_t v2 = (uint8_t)(y + effect_offset * anim_mul);
    uint8_t v3 = (uint8_t)(((x + y) * 3u) / 2u + effect_offset * (anim_mul + 1u));
    uint16_t radial = (uint16_t)((cx * cx + cy * cy) / radial_div);
    uint8_t hue = (uint8_t)(v1 + v2 + v3 + radial);
    uint8_t r, g, b;
    led_matrix_hsv_to_rgb(hue, sat, value, &r, &g, &b);
    led_matrix_set_pixel_idx(i, r, g, b);
  }
}

/**
 * @brief Fire effect - flames rising from bottom
 */
static void effect_fire(void) {
  uint8_t heat_boost = (uint8_t)(((uint16_t)effect_param(0, 160u) * 64u) / 255u);
  uint8_t ember_floor = (uint8_t)(((uint16_t)effect_param(1, 96u) * 80u) / 255u);
  uint8_t cooling = (uint8_t)(16u + ((uint16_t)effect_param(2, 96u) * 96u) / 255u);
  uint8_t palette = effect_param(3, 0u) % 3u;
  for (uint8_t i = 0; i < LED_MATRIX_NUM_LEDS; i++) {
    uint8_t x_term = scale_coord_to_u8(led_pos_x[i], LED_LAYOUT_MAX_X);
    uint8_t y_term = 255u - scale_coord_to_u8(led_pos_y[i], LED_LAYOUT_MAX_Y);
    uint8_t heat = (uint8_t)((y_term * 3u) / 4u + ((effect_offset + x_term) % 64u) +
                             heat_boost);
    uint8_t r, g, b;
    heat = (heat > cooling) ? (uint8_t)(heat - cooling) : 0u;

    if (palette == 1u) {
      if (heat > 220u) {
        r = 255u;
        g = 200u;
        b = 64u;
      } else if (heat > 170u) {
        r = 255u;
        g = 120u;
        b = 0u;
      } else if (heat > 100u) {
        r = 220u;
        g = 32u;
        b = 0u;
      } else if (heat > ember_floor) {
        r = (uint8_t)(96u + ember_floor);
        g = 0u;
        b = 0u;
      } else {
        r = g = b = 0u;
      }
    } else if (palette == 2u) {
      if (heat > 220u) {
        r = 180u;
        g = 220u;
        b = 255u;
      } else if (heat > 170u) {
        r = 32u;
        g = 160u;
        b = 255u;
      } else if (heat > 100u) {
        r = 0u;
        g = 72u;
        b = 255u;
      } else if (heat > ember_floor) {
        r = 0u;
        g = (uint8_t)(32u + ember_floor / 2u);
        b = (uint8_t)(96u + ember_floor);
      } else {
        r = g = b = 0u;
      }
    } else {
      if (heat > 220u) {
        r = 255u;
        g = 255u;
        b = 160u;
      } else if (heat > 170u) {
        r = 255u;
        g = 170u;
        b = 0u;
      } else if (heat > 100u) {
        r = 255u;
        g = 72u;
        b = 0u;
      } else if (heat > ember_floor) {
        r = (uint8_t)(96u + ember_floor);
        g = 0u;
        b = 0u;
      } else {
        r = g = b = 0u;
      }
    }

    led_matrix_set_pixel_idx(i, r, g, b);
  }
}

/**
 * @brief Ocean waves effect - horizontal waves
 */
static void effect_ocean(void) {
  uint8_t hue_bias =
      (uint8_t)(100u + ((uint16_t)effect_param(0, 160u) * 90u) / 255u);
  uint8_t depth_dim = (uint8_t)(32u + ((uint16_t)effect_param(1, 64u) * 120u) / 255u);
  bool foam_highlight = effect_param_flag(2, true);
  uint8_t speed_mul =
      (uint8_t)(1u + ((uint16_t)effect_param(3, 160u) * 4u) / 255u);
  for (uint8_t i = 0; i < LED_MATRIX_NUM_LEDS; i++) {
    uint8_t x_term = scale_coord_to_u8(led_pos_x[i], LED_LAYOUT_MAX_X);
    uint8_t y_term = scale_coord_to_u8(led_pos_y[i], LED_LAYOUT_MAX_Y);
    uint8_t wave = (uint8_t)(x_term + (effect_offset * speed_mul));
    uint8_t hue = (uint8_t)(hue_bias + (wave / 12u) + (y_term / 16u));
    uint8_t brightness = (uint8_t)(255u - ((uint16_t)y_term * depth_dim / 255u));
    uint8_t r, g, b;
    led_matrix_hsv_to_rgb(hue, 255, brightness, &r, &g, &b);
    if (foam_highlight && ((wave + y_term) & 0x1Fu) < 4u) {
      r = (uint8_t)(r + ((255u - r) / 2u));
      g = (uint8_t)(g + ((255u - g) / 2u));
      b = (uint8_t)(b + ((255u - b) / 2u));
    }
    led_matrix_set_pixel_idx(i, r, g, b);
  }
}

/**
 * @brief Matrix rain effect - digital rain
 */
static void effect_matrix(void) {
  uint8_t trail = (uint8_t)(24u + ((uint16_t)effect_param(0, 64u) * 80u) / 255u);
  uint8_t head = (uint8_t)(4u + ((uint16_t)effect_param(1, 160u) * 10u) / 255u);
  uint8_t density = (uint8_t)(4u + ((uint16_t)effect_param(2, 96u) * 24u) / 255u);
  bool white_heads = effect_param_flag(3, true);
  uint8_t hue_bias = effect_param(4, 0u);
  for (uint8_t i = 0; i < LED_MATRIX_NUM_LEDS; i++) {
    uint8_t col_seed =
        (uint8_t)(scale_coord_to_u8(led_pos_x[i], LED_LAYOUT_MAX_X) / density);
    uint8_t y_term = scale_coord_to_u8(led_pos_y[i], LED_LAYOUT_MAX_Y);
    uint8_t drop_pos = (uint8_t)((effect_offset * 6u + col_seed * 17u) & 0xFFu);
    uint8_t dist = abs_u8_diff(y_term, drop_pos);
    uint8_t r, g, b;

    if (dist < head) {
      if (white_heads) {
        r = 255u;
        g = 255u;
        b = 255u;
      } else {
        led_matrix_hsv_to_rgb((uint8_t)(96u + hue_bias), 255u, 255u, &r, &g, &b);
      }
    } else if (dist < trail) {
      led_matrix_hsv_to_rgb((uint8_t)(96u + hue_bias), 255u,
                            (uint8_t)(255u - ((uint16_t)dist * 255u) / trail),
                            &r, &g, &b);
    } else {
      r = g = b = 0u;
    }

    led_matrix_set_pixel_idx(i, r, g, b);
  }
}

/**
 * @brief Sparkle/Twinkle effect
 */
static void effect_sparkle(void) {
  uint8_t density = (uint8_t)(2u + ((uint16_t)(255u - effect_param(0, 48u)) * 10u) / 255u);
  uint8_t sparkle_value = effect_param(1, 224u);
  uint8_t rainbow_mix = effect_param(2, 160u);
  uint8_t ambient = effect_param(3, 0u);
  for (uint8_t i = 0; i < LED_MATRIX_NUM_LEDS; i++) {
    uint8_t seed = (uint8_t)((led_pos_x[i] / 19u) + (led_pos_y[i] / 23u) + effect_offset) & 0x1Fu;
    uint8_t r, g, b;

    if ((seed % density) == 0u) {
      r = g = b = sparkle_value;
    } else if (seed < density) {
      uint8_t hue = scale_coord_to_u8(led_pos_x[i], LED_LAYOUT_MAX_X) + effect_offset;
      led_matrix_hsv_to_rgb(hue, rainbow_mix, sparkle_value / 2u, &r, &g, &b);
    } else {
      r = 0u;
      g = ambient;
      b = (uint8_t)(ambient / 3u);
    }

    led_matrix_set_pixel_idx(i, r, g, b);
  }
}

/**
 * @brief Breathing rainbow - color cycles while breathing
 */
static void effect_breathing_rainbow(void) {
  uint8_t min_brightness = effect_param(0, 24u);
  uint8_t hue_mul =
      (uint8_t)(1u + ((uint16_t)effect_param(1, 192u) * 4u) / 255u);
  uint8_t sat = effect_param(2, 255u);
  uint8_t breath = (effect_offset % 128);
  if (breath > 64)
    breath = 128 - breath;
  uint8_t brightness =
      (uint8_t)(min_brightness + (((uint16_t)(255u - min_brightness) * breath) / 64u));
  uint8_t hue = (uint8_t)((effect_offset * hue_mul) / 2u);
  uint8_t r, g, b;
  led_matrix_hsv_to_rgb(hue, sat, brightness, &r, &g, &b);
  led_matrix_fill(r, g, b);
}

/**
 * @brief Spiral effect
 */
static void effect_spiral(void) {
  uint8_t twist = (uint8_t)(1u + ((uint16_t)effect_param(0, 160u) * 6u) / 255u);
  uint8_t radial_div = (uint8_t)(48u + effect_param(1, 96u));
  uint8_t orbit_mul =
      (uint8_t)(1u + ((uint16_t)effect_param(2, 128u) * 4u) / 255u);
  uint8_t sat = effect_param(3, 255u);
  for (uint8_t i = 0; i < LED_MATRIX_NUM_LEDS; i++) {
    int16_t cx = (int16_t)led_pos_x[i] - (int16_t)LED_LAYOUT_CENTER_X;
    int16_t cy = (int16_t)led_pos_y[i] - (int16_t)LED_LAYOUT_CENTER_Y;
    uint16_t dist = (uint16_t)((cx * cx + cy * cy) / radial_div);
    uint8_t angle = (uint8_t)(((cx / 12) + (cy / 12)) * twist);
    uint8_t hue = (uint8_t)(dist + angle + effect_offset * orbit_mul);
    uint8_t r, g, b;
    led_matrix_hsv_to_rgb(hue, sat, 255, &r, &g, &b);
    led_matrix_set_pixel_idx(i, r, g, b);
  }
}

/**
 * @brief Solid color cycle - all LEDs same color, slowly changing
 */
static void effect_color_cycle(void) {
  uint8_t hue_step =
      (uint8_t)(1u + ((uint16_t)effect_param(0, 64u) * 4u) / 255u);
  uint8_t saturation = effect_param(1, 255u);
  uint8_t value = effect_param(2, 255u);
  uint8_t color_mix = effect_param(3, 0u);
  uint8_t hue = (uint8_t)((effect_offset * hue_step) / 4u);
  uint8_t r, g, b;
  led_matrix_hsv_to_rgb(hue, saturation, value, &r, &g, &b);
  if (color_mix > 0u) {
    r = (uint8_t)(((uint16_t)r * (255u - color_mix) +
                   (uint16_t)effect_color_r * color_mix) /
                  255u);
    g = (uint8_t)(((uint16_t)g * (255u - color_mix) +
                   (uint16_t)effect_color_g * color_mix) /
                  255u);
    b = (uint8_t)(((uint16_t)b * (255u - color_mix) +
                   (uint16_t)effect_color_b * color_mix) /
                  255u);
  }
  led_matrix_fill(r, g, b);
}

/**
 * @brief Sensor distance effect - each key LED color follows its travel distance
 *
 * Mapping used (for the 6-key prototype):
 * - released (0.00 mm): cool blue/cyan
 * - pressed  (4.00 mm): warm red
 */
static void effect_distance_sensor(void) {
  led_matrix_clear();
  uint8_t min_value = effect_param(0, 32u);
  uint8_t hue_span = effect_param(1, 170u);
  uint8_t sat = effect_param(2, 255u);
  bool reverse = effect_param_flag(3, false);
  if (hue_span == 0u) {
    hue_span = 1u;
  }

  for (uint8_t key = 0; key < NUM_KEYS; key++) {
    uint8_t level = analog_read_normalized_value(key);

    // Hue ramps from cyan/blue to red as distance increases.
    uint8_t hue = reverse
                      ? (uint8_t)(((uint16_t)hue_span * level) / 255u)
                      : (uint8_t)(hue_span -
                                  ((uint16_t)hue_span * level) / 255u);
    // Also increase brightness with travel for better readability.
    uint8_t value = (uint8_t)(min_value + ((uint16_t)(255u - min_value) * level) / 255u);

    uint8_t r, g, b;
    led_matrix_hsv_to_rgb(hue, sat, value, &r, &g, &b);
    led_matrix_set_pixel_idx(key, r, g, b);
  }
}

/**
 * @brief Reactive effect - lights up on key presses
 */
static void effect_reactive(void) {
  uint16_t accum_r[NUM_KEYS];
  uint16_t accum_g[NUM_KEYS];
  uint16_t accum_b[NUM_KEYS];
  uint8_t core_peak[NUM_KEYS];
  uint16_t lifetime_ms =
      (uint16_t)(320u + (((uint32_t)(255u - effect_param(0, 36u)) * 930u) /
                         255u));
  uint16_t spread =
      (uint16_t)(7u + (((uint32_t)effect_param(1, 96u) * 17u) / 255u));
  uint8_t base_glow = effect_param(2, 0u);
  bool white_core = effect_param_flag(3, true);
  uint16_t gain =
      (uint16_t)(160u + (((uint32_t)effect_param(4, 192u) * 160u) / 255u));
  uint16_t ring_width =
      (uint16_t)(10u + (((uint32_t)effect_param(1, 96u) * 12u) / 255u));
  uint16_t trail_length =
      (uint16_t)(28u + (((uint32_t)effect_param(1, 96u) * 52u) / 255u));
  uint16_t core_radius =
      (uint16_t)(12u + (((uint32_t)effect_param(1, 96u) * 8u) / 255u));
  uint16_t wave_speed_q8 =
      (uint16_t)(84u + (((uint32_t)effect_speed * 172u) / 255u));
  uint8_t anchor_sat = 0u;
  uint8_t ignored_hue = 0u;
  uint8_t ignored_value = 0u;
  uint8_t ring_sat;
  uint8_t trail_sat;
  uint8_t base_r;
  uint8_t base_g;
  uint8_t base_b;

  led_matrix_rgb_to_hsv(effect_color_r, effect_color_g, effect_color_b,
                        &ignored_hue, &anchor_sat, &ignored_value);
  ring_sat = (anchor_sat > 176u) ? anchor_sat : 176u;
  trail_sat = (ring_sat > 56u) ? (uint8_t)(ring_sat - 56u) : 120u;
  base_r = scale_u8(effect_color_r, base_glow);
  base_g = scale_u8(effect_color_g, base_glow);
  base_b = scale_u8(effect_color_b, base_glow);

  for (uint8_t led_idx = 0; led_idx < NUM_KEYS; led_idx++) {
    accum_r[led_idx] = base_r;
    accum_g[led_idx] = base_g;
    accum_b[led_idx] = base_b;
    core_peak[led_idx] = 0u;
  }

  for (uint8_t key = 0; key < NUM_KEYS; key++) {
    if (key_wave_energy[key] == 0u) {
      continue;
    }

    uint16_t age_ms = key_wave_age_ms[key];
    if (age_ms >= lifetime_ms) {
      key_wave_energy[key] = 0u;
      key_wave_age_ms[key] = 0u;
      continue;
    }

    uint8_t energy = (uint8_t)(((uint32_t)(lifetime_ms - age_ms) * 255u) /
                               lifetime_ms);
    if (energy == 0u) {
      key_wave_energy[key] = 0u;
      key_wave_age_ms[key] = 0u;
      continue;
    }

    uint16_t front = (uint16_t)(((uint32_t)age_ms * wave_speed_q8) >> 8);
    uint8_t ring_hue = (uint8_t)(key_wave_hue[key] + (front / 6u) +
                                 (uint8_t)(age_ms / 14u));
    uint8_t trail_hue = (uint8_t)(ring_hue + 18u);
    uint8_t ring_r = 0u;
    uint8_t ring_g = 0u;
    uint8_t ring_b = 0u;
    uint8_t trail_r = 0u;
    uint8_t trail_g = 0u;
    uint8_t trail_b = 0u;

    led_matrix_hsv_to_rgb(ring_hue, ring_sat, 255u, &ring_r, &ring_g, &ring_b);
    led_matrix_hsv_to_rgb(trail_hue, trail_sat, 255u, &trail_r, &trail_g,
                          &trail_b);

    for (uint8_t led_idx = 0; led_idx < NUM_KEYS; led_idx++) {
      uint16_t distance = led_layout_distance(led_idx, key) / spread;
      uint16_t ring_delta =
          (front > distance) ? (front - distance) : (distance - front);
      uint16_t ring_raw = 0u;
      uint16_t trail_raw = 0u;
      uint16_t core_raw = 0u;

      if (ring_delta < ring_width) {
        ring_raw = ((ring_width - ring_delta) * energy) / ring_width;
      }

      if (front >= distance) {
        uint16_t trail_depth = front - distance;
        if (trail_depth < trail_length) {
          trail_raw = ((trail_length - trail_depth) * energy) / trail_length;
        }
      }

      if (distance < core_radius && age_ms < 160u) {
        uint16_t spatial = ((core_radius - distance) * 255u) / core_radius;
        uint16_t temporal = 255u - (((uint32_t)age_ms * 255u) / 160u);
        core_raw = (spatial * temporal) / 255u;
      }

      {
        uint8_t ring_value =
            clamp_u16_to_u8((uint16_t)(((uint32_t)ring_raw * gain) / 255u));
        uint8_t trail_value = clamp_u16_to_u8(
            (uint16_t)(((uint32_t)trail_raw * (gain - 48u)) / 255u));
        uint8_t core_value = clamp_u16_to_u8(
            (uint16_t)(((uint32_t)core_raw * (gain + 32u)) / 255u));

        if (ring_value > 0u) {
          add_scaled_rgb(&accum_r[led_idx], &accum_g[led_idx],
                         &accum_b[led_idx], ring_r, ring_g, ring_b,
                         ring_value);
        }
        if (trail_value > 0u) {
          add_scaled_rgb(&accum_r[led_idx], &accum_g[led_idx],
                         &accum_b[led_idx], trail_r, trail_g, trail_b,
                         trail_value);
        }
        if (core_value > 0u) {
          add_scaled_rgb(&accum_r[led_idx], &accum_g[led_idx],
                         &accum_b[led_idx], effect_color_r, effect_color_g,
                         effect_color_b, core_value);
          if (core_value > core_peak[led_idx]) {
            core_peak[led_idx] = core_value;
          }
        }
      }
    }
  }

  for (uint8_t led_idx = 0; led_idx < NUM_KEYS; led_idx++) {
    uint8_t r = clamp_u16_to_u8(accum_r[led_idx]);
    uint8_t g = clamp_u16_to_u8(accum_g[led_idx]);
    uint8_t b = clamp_u16_to_u8(accum_b[led_idx]);

    if (white_core && core_peak[led_idx] > 96u) {
      uint8_t white_mix =
          (uint8_t)(((uint16_t)(core_peak[led_idx] - 96u) * 255u) / 159u);
      mix_rgb_towards_white(&r, &g, &b, white_mix);
    }

    led_matrix_set_pixel_idx(led_idx, r, g, b);
  }

  {
    uint16_t delta_ms = reactive_frame_delta_ms;
    if (delta_ms == 0u) {
      delta_ms = 1u;
    } else if (delta_ms > 120u) {
      delta_ms = 120u;
    }

    for (uint8_t key = 0; key < NUM_KEYS; key++) {
      if (key_wave_energy[key] == 0u) {
        continue;
      }

      {
        uint32_t next_age = (uint32_t)key_wave_age_ms[key] + delta_ms;
        if (next_age >= lifetime_ms) {
          key_wave_energy[key] = 0u;
          key_wave_age_ms[key] = 0u;
        } else {
          key_wave_age_ms[key] = (uint16_t)next_age;
        }
      }
    }
  }
}

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

  // Effect animation speed (controls how fast effect_offset increments)
  // speed 1 = 200ms interval, speed 255 = 5ms interval
  uint32_t effect_interval = 200 - (effect_speed * 195 / 255);
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

  reactive_frame_delta_ms =
      (uint16_t)((render_delta > 65535u) ? 65535u : render_delta);
  last_render_tick = tick;

  // Matrix static mode and third-party mode do not run internal animations.
  if (current_effect == LED_EFFECT_STATIC_MATRIX ||
      current_effect == LED_EFFECT_THIRD_PARTY) {
    return;
  }

  // Now render the effect using current effect_offset
  render_current_effect_frame();
}
