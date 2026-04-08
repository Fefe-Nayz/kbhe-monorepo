/*
 * led_matrix.c
 * RGB LED strip/matrix driver using WS2812
 */

#include "led_matrix.h"
#include "analog/analog.h"
#include "main.h"
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
static uint32_t last_effect_tick = 0;
static uint32_t last_render_tick = 0; // Separate tick for FPS limiting
static uint16_t effect_offset = 0;

// Effect renderer context flag: when true, pixel writes target runtime frame only.
static bool effect_render_context = false;

// Diagnostic mode: 0=normal, 1=DMA stress (no CPU computation), 2=CPU stress
// (compute but no DMA)
static uint8_t diagnostic_mode = 0;

// Reactive effect state
static uint8_t key_wave_energy[NUM_KEYS] = {0};
static uint8_t key_wave_radius[NUM_KEYS] = {0};

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

// Trigger travel normalization range used by triggerGetDistance01mm():
// 4.00 mm => 400 units (0.01 mm).
#define KEY_TRAVEL_MAX_01MM 400u

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

static inline uint8_t scale_coord_to_u8(uint16_t value, uint16_t max_value) {
  if (max_value == 0u) {
    return 0u;
  }

  return (uint8_t)(((uint32_t)value * 255u) / max_value);
}

static inline uint16_t led_layout_distance(uint8_t a, uint8_t b) {
  uint16_t dx = abs_u16_diff(led_pos_x[a], led_pos_x[b]);
  uint16_t dy = abs_u16_diff(led_pos_y[a], led_pos_y[b]);
  uint16_t major = (dx > dy) ? dx : dy;
  uint16_t minor = (dx > dy) ? dy : dx;
  return major + (minor / 2u);
}

/**
 * @brief Push one runtime pixel to WS2812 buffer
 */
static inline void push_runtime_pixel_to_ws2812(uint8_t index) {
  uint8_t physical_index = LOGICAL_LED_INDEX_TO_PHYSICAL_LED_INDEX[index];
  uint8_t r = apply_brightness(pixels_runtime[index * 3 + 0], current_brightness);
  uint8_t g = apply_brightness(pixels_runtime[index * 3 + 1], current_brightness);
  uint8_t b = apply_brightness(pixels_runtime[index * 3 + 2], current_brightness);
  setLedValues(&led_ws2812_handle, physical_index, r, g, b);
}

/**
 * @brief Update ws2812 driver with current pixel data
 */
static void update_ws2812(void) {
  if (!initialized || !display_enabled)
    return;

  // Apply brightness and copy to WS2812 buffer
  for (uint8_t i = 0; i < LED_MATRIX_NUM_LEDS; i++) {
    push_runtime_pixel_to_ws2812(i);
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
    ws2812_show(&led_ws2812_handle);
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
    if (initialized && display_enabled) {
      zeroLedValues(&led_ws2812_handle);
      ws2812_show(&led_ws2812_handle);
    }
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

void led_matrix_set_effect_speed(uint8_t speed) {
  effect_speed = speed > 0 ? speed : 1;
}

uint8_t led_matrix_get_effect_speed(void) { return effect_speed; }

void led_matrix_set_fps_limit(uint8_t fps) { fps_limit = fps; }

uint8_t led_matrix_get_fps_limit(void) { return fps_limit; }

void led_matrix_set_diagnostic_mode(uint8_t mode) {
  uint8_t old_mode = diagnostic_mode;
  diagnostic_mode = mode;

  // Handle timer output enable/disable for Mode 3
  if (mode == 3 && old_mode != 3) {
    // Entering Mode 3: Disable timer output (GPIO to low)
    // This keeps DMA running but no signal reaches LEDs
    TIM_HandleTypeDef *timer = led_ws2812_handle.timer;
    if (timer) {
      // Disable the timer channel output
      HAL_TIM_PWM_Stop(timer, led_ws2812_handle.channel);
      // Restart DMA without PWM output - DMA still writes to CCR but pin stays
      // low
      HAL_TIM_PWM_Start_DMA(timer, led_ws2812_handle.channel,
                            (uint32_t *)led_ws2812_handle.dma_buffer,
                            BUFFER_SIZE * 2);
      // Disable the output compare channel
      __HAL_TIM_DISABLE_OCxPRELOAD(timer, led_ws2812_handle.channel);
      // Force pin low by setting to GPIO output
      GPIO_InitTypeDef GPIO_InitStruct = {0};
      GPIO_InitStruct.Pin = LED_DATA_Pin;
      GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
      GPIO_InitStruct.Pull = GPIO_NOPULL;
      GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
      HAL_GPIO_Init(LED_DATA_GPIO_Port, &GPIO_InitStruct);
      HAL_GPIO_WritePin(LED_DATA_GPIO_Port, LED_DATA_Pin, GPIO_PIN_RESET);
    }
  } else if (old_mode == 3 && mode != 3) {
    // Exiting Mode 3: Re-enable timer output
    TIM_HandleTypeDef *timer = led_ws2812_handle.timer;
    if (timer) {
      // Restore GPIO to alternate function for timer
      GPIO_InitTypeDef GPIO_InitStruct = {0};
      GPIO_InitStruct.Pin = LED_DATA_Pin;
      GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
      GPIO_InitStruct.Pull = GPIO_NOPULL;
      GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
      GPIO_InitStruct.Alternate = GPIO_AF1_TIM2;
      HAL_GPIO_Init(LED_DATA_GPIO_Port, &GPIO_InitStruct);
    }
  }
}

uint8_t led_matrix_get_diagnostic_mode(void) { return diagnostic_mode; }

void led_matrix_key_event(uint8_t key_index, bool pressed) {
  if (key_index >= NUM_KEYS)
    return;
  if (pressed) {
    key_wave_energy[key_index] = 255;
    key_wave_radius[key_index] = 0;
  }
}

/**
 * @brief Rainbow Wave effect - diagonal rainbow cycling
 */
static void effect_rainbow(void) {
  for (uint8_t i = 0; i < LED_MATRIX_NUM_LEDS; i++) {
    uint8_t hue = scale_coord_to_u8(led_pos_x[i], LED_LAYOUT_MAX_X) +
                  (scale_coord_to_u8(led_pos_y[i], LED_LAYOUT_MAX_Y) / 3u) +
                  (uint8_t)(effect_offset * 2u);
    uint8_t r, g, b;
    led_matrix_hsv_to_rgb(hue, 255, 255, &r, &g, &b);
    led_matrix_set_pixel_idx(i, r, g, b);
  }
}

/**
 * @brief Breathing effect - fade in/out with selected color
 */
static void effect_breathing(void) {
  uint8_t breath = (effect_offset % 128);
  if (breath > 64)
    breath = 128 - breath;
  uint8_t brightness = (255 * breath) / 64;

  uint8_t r = (uint8_t)((uint16_t)effect_color_r * brightness / 255);
  uint8_t g = (uint8_t)((uint16_t)effect_color_g * brightness / 255);
  uint8_t b = (uint8_t)((uint16_t)effect_color_b * brightness / 255);

  led_matrix_fill(r, g, b);
}

/**
 * @brief Static Rainbow - static rainbow pattern
 */
static void effect_static_rainbow(void) {
  for (uint8_t i = 0; i < LED_MATRIX_NUM_LEDS; i++) {
    uint8_t hue = scale_coord_to_u8(led_pos_x[i], LED_LAYOUT_MAX_X) +
                  scale_coord_to_u8(led_pos_y[i], LED_LAYOUT_MAX_Y);
    uint8_t r, g, b;
    led_matrix_hsv_to_rgb(hue, 255, 255, &r, &g, &b);
    led_matrix_set_pixel_idx(i, r, g, b);
  }
}

/**
 * @brief Solid color effect
 */
static void effect_solid(void) {
  led_matrix_fill(effect_color_r, effect_color_g, effect_color_b);
}

/**
 * @brief Plasma effect - psychedelic waves
 */
static void effect_plasma(void) {
  for (uint8_t i = 0; i < LED_MATRIX_NUM_LEDS; i++) {
    uint16_t x = led_pos_x[i] / 12u;
    uint16_t y = led_pos_y[i] / 12u;
    int16_t cx = (int16_t)(led_pos_x[i] / 10u) - (int16_t)(LED_LAYOUT_CENTER_X / 10u);
    int16_t cy = (int16_t)(led_pos_y[i] / 10u) - (int16_t)(LED_LAYOUT_CENTER_Y / 10u);
    uint8_t v1 = (uint8_t)(x + effect_offset);
    uint8_t v2 = (uint8_t)(y + effect_offset);
    uint8_t v3 = (uint8_t)(((x + y) * 3u) / 2u + effect_offset * 2u);
    uint16_t radial = (uint16_t)((cx * cx + cy * cy) / 24);
    uint8_t hue = (uint8_t)(v1 + v2 + v3 + radial);
    uint8_t r, g, b;
    led_matrix_hsv_to_rgb(hue, 255, 255, &r, &g, &b);
    led_matrix_set_pixel_idx(i, r, g, b);
  }
}

/**
 * @brief Fire effect - flames rising from bottom
 */
static void effect_fire(void) {
  for (uint8_t i = 0; i < LED_MATRIX_NUM_LEDS; i++) {
    uint8_t x_term = scale_coord_to_u8(led_pos_x[i], LED_LAYOUT_MAX_X);
    uint8_t y_term = 255u - scale_coord_to_u8(led_pos_y[i], LED_LAYOUT_MAX_Y);
    uint8_t heat = (uint8_t)((y_term * 3u) / 4u + ((effect_offset + x_term) % 64u));
    uint8_t r, g, b;

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
    } else if (heat > 45u) {
      r = 140u;
      g = 0u;
      b = 0u;
    } else {
      r = g = b = 0u;
    }

    led_matrix_set_pixel_idx(i, r, g, b);
  }
}

/**
 * @brief Ocean waves effect - horizontal waves
 */
static void effect_ocean(void) {
  for (uint8_t i = 0; i < LED_MATRIX_NUM_LEDS; i++) {
    uint8_t x_term = scale_coord_to_u8(led_pos_x[i], LED_LAYOUT_MAX_X);
    uint8_t y_term = scale_coord_to_u8(led_pos_y[i], LED_LAYOUT_MAX_Y);
    uint8_t wave = (uint8_t)(x_term + (effect_offset * 3u));
    uint8_t hue = 140u + (wave / 12u) + (y_term / 16u);
    uint8_t brightness = (uint8_t)(255u - (y_term / 2u));
    uint8_t r, g, b;
    led_matrix_hsv_to_rgb(hue, 255, brightness, &r, &g, &b);
    led_matrix_set_pixel_idx(i, r, g, b);
  }
}

/**
 * @brief Matrix rain effect - digital rain
 */
static void effect_matrix(void) {
  for (uint8_t i = 0; i < LED_MATRIX_NUM_LEDS; i++) {
    uint8_t col_seed = (uint8_t)(scale_coord_to_u8(led_pos_x[i], LED_LAYOUT_MAX_X) / 12u);
    uint8_t y_term = scale_coord_to_u8(led_pos_y[i], LED_LAYOUT_MAX_Y);
    uint8_t drop_pos = (uint8_t)((effect_offset * 6u + col_seed * 17u) & 0xFFu);
    uint8_t dist = abs_u8_diff(y_term, drop_pos);
    uint8_t r, g, b;

    if (dist < 8u) {
      r = 255u;
      g = 255u;
      b = 255u;
    } else if (dist < 64u) {
      r = 0u;
      g = (uint8_t)(255u - dist * 3u);
      b = 0u;
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
  for (uint8_t i = 0; i < LED_MATRIX_NUM_LEDS; i++) {
    uint8_t seed = (uint8_t)((led_pos_x[i] / 19u) + (led_pos_y[i] / 23u) + effect_offset) & 0x1Fu;
    uint8_t r, g, b;

    if (seed == 0u) {
      r = g = b = 255u;
    } else if (seed < 3u) {
      uint8_t hue = scale_coord_to_u8(led_pos_x[i], LED_LAYOUT_MAX_X) + effect_offset;
      led_matrix_hsv_to_rgb(hue, 255, 128, &r, &g, &b);
    } else {
      r = g = b = 0u;
    }

    led_matrix_set_pixel_idx(i, r, g, b);
  }
}

/**
 * @brief Breathing rainbow - color cycles while breathing
 */
static void effect_breathing_rainbow(void) {
  uint8_t breath = (effect_offset % 128);
  if (breath > 64)
    breath = 128 - breath;
  uint8_t brightness = (255 * breath) / 64;
  uint8_t hue = effect_offset / 2; // Slowly cycle through colors
  uint8_t r, g, b;
  led_matrix_hsv_to_rgb(hue, 255, brightness, &r, &g, &b);
  led_matrix_fill(r, g, b);
}

/**
 * @brief Spiral effect
 */
static void effect_spiral(void) {
  for (uint8_t i = 0; i < LED_MATRIX_NUM_LEDS; i++) {
    int16_t cx = (int16_t)led_pos_x[i] - (int16_t)LED_LAYOUT_CENTER_X;
    int16_t cy = (int16_t)led_pos_y[i] - (int16_t)LED_LAYOUT_CENTER_Y;
    uint16_t dist = (uint16_t)((cx * cx + cy * cy) / 96);
    uint8_t angle = (uint8_t)((cx / 12) + (cy / 12));
    uint8_t hue = (uint8_t)(dist + angle + effect_offset * 3u);
    uint8_t r, g, b;
    led_matrix_hsv_to_rgb(hue, 255, 255, &r, &g, &b);
    led_matrix_set_pixel_idx(i, r, g, b);
  }
}

/**
 * @brief Solid color cycle - all LEDs same color, slowly changing
 */
static void effect_color_cycle(void) {
  uint8_t hue = effect_offset / 4; // Slow color change
  uint8_t r, g, b;
  led_matrix_hsv_to_rgb(hue, 255, 255, &r, &g, &b);
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

  for (uint8_t key = 0; key < NUM_KEYS; key++) {
    int16_t distance_value = analog_read_distance_value(key);
    uint16_t distance_01mm = 0u;

    if (distance_value > 0) {
      distance_01mm = (uint16_t)distance_value;
    }

    if (distance_01mm > KEY_TRAVEL_MAX_01MM) {
      distance_01mm = KEY_TRAVEL_MAX_01MM;
    }

    uint8_t level = (uint8_t)((distance_01mm * 255u) / KEY_TRAVEL_MAX_01MM);

    // Hue ramps from cyan/blue to red as distance increases.
    uint8_t hue = (uint8_t)(170u - ((uint16_t)170u * level) / 255u);
    // Also increase brightness with travel for better readability.
    uint8_t value = (uint8_t)(32u + ((uint16_t)223u * level) / 255u);

    uint8_t r, g, b;
    led_matrix_hsv_to_rgb(hue, 255, value, &r, &g, &b);
    led_matrix_set_pixel_idx(key, r, g, b);
  }
}

/**
 * @brief Reactive effect - lights up on key presses
 */
static void effect_reactive(void) {
  led_matrix_clear();

  for (uint8_t led_idx = 0; led_idx < NUM_KEYS; led_idx++) {
    uint16_t intensity = 0;

    for (uint8_t key = 0; key < NUM_KEYS; key++) {
      uint8_t energy = key_wave_energy[key];
      if (energy == 0) {
        continue;
      }

      uint16_t distance = led_layout_distance(led_idx, key) / 24u;
      uint16_t core =
          (energy > (uint16_t)(distance * 18u))
              ? (energy - (uint16_t)(distance * 18u))
              : 0u;

      int16_t ring_delta =
          (int16_t)key_wave_radius[key] - (int16_t)(distance * 8u);
      if (ring_delta < 0) {
        ring_delta = -ring_delta;
      }

      uint16_t ring = 0u;
      if (ring_delta < 12) {
        ring = ((uint16_t)(12 - ring_delta) * energy) / 12u;
      }

      uint16_t contribution = (core > ring) ? core : ring;
      if (contribution > intensity) {
        intensity = contribution;
      }
    }

    uint8_t r = (uint8_t)((uint16_t)effect_color_r * intensity / 255u);
    uint8_t g = (uint8_t)((uint16_t)effect_color_g * intensity / 255u);
    uint8_t b = (uint8_t)((uint16_t)effect_color_b * intensity / 255u);
    led_matrix_set_pixel_idx(led_idx, r, g, b);
  }

  // Advance and decay all active key waves.
  for (uint8_t key = 0; key < NUM_KEYS; key++) {
    if (key_wave_energy[key] == 0) {
      continue;
    }

    if (key_wave_energy[key] > 18u) {
      key_wave_energy[key] -= 18u;
    } else {
      key_wave_energy[key] = 0;
    }

    if (key_wave_radius[key] < (255u - 12u)) {
      key_wave_radius[key] += 12u;
    } else {
      key_wave_radius[key] = 255u;
    }

    if (key_wave_energy[key] == 0) {
      key_wave_radius[key] = 0u;
    }
  }
}

void led_matrix_effect_tick(uint32_t tick) {
  if (!initialized || !display_enabled) {
    return;
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

  last_render_tick = tick;

  // Handle diagnostic modes
  if (diagnostic_mode == 1) {
    // Mode 1: DMA Stress - trigger DMA transfer without CPU computation
    // Just mark buffer dirty to cause DMA activity at current FPS rate
    led_ws2812_handle.is_dirty = 1;
    return;
  }

  if (diagnostic_mode == 2) {
    // Mode 2: CPU Stress - do heavy computation but don't trigger DMA
    // Run a rainbow-like computation to stress CPU
    for (uint8_t i = 0; i < LED_MATRIX_NUM_LEDS; i++) {
      uint8_t hue = scale_coord_to_u8(led_pos_x[i], LED_LAYOUT_MAX_X) +
                    scale_coord_to_u8(led_pos_y[i], LED_LAYOUT_MAX_Y) +
                    (uint8_t)(effect_offset * 2u);
      uint8_t r, g, b;
      led_matrix_hsv_to_rgb(hue, 255, 255, &r, &g, &b);
      pixels_runtime[i * 3 + 0] = r;
      pixels_runtime[i * 3 + 1] = g;
      pixels_runtime[i * 3 + 2] = b;
    }
    // Explicitly clear dirty flag to prevent DMA
    led_ws2812_handle.is_dirty = 0;
    return;
  }

  if (diagnostic_mode == 3) {
    // Mode 3: CPU + DMA Stress, no PWM output
    // Compute effect AND fill DMA buffer, but timer output is disabled
    // This tests if DMA memory access causes issues even without pin toggling
    for (uint8_t i = 0; i < LED_MATRIX_NUM_LEDS; i++) {
      uint8_t hue = scale_coord_to_u8(led_pos_x[i], LED_LAYOUT_MAX_X) +
                    scale_coord_to_u8(led_pos_y[i], LED_LAYOUT_MAX_Y) +
                    (uint8_t)(effect_offset * 2u);
      uint8_t r, g, b;
      uint8_t physical_index = LOGICAL_LED_INDEX_TO_PHYSICAL_LED_INDEX[i];
      led_matrix_hsv_to_rgb(hue, 255, 255, &r, &g, &b);
      setLedValues(&led_ws2812_handle, physical_index, r, g, b);
    }
    // Note: DMA still runs and updates the CCR register, but if timer output
    // was disabled before entering this mode, no signal goes to LEDs
    return;
  }

  // Matrix static mode and third-party mode do not run internal animations.
  if (current_effect == LED_EFFECT_STATIC_MATRIX ||
      current_effect == LED_EFFECT_THIRD_PARTY) {
    return;
  }

  // Now render the effect using current effect_offset
  render_current_effect_frame();
}
