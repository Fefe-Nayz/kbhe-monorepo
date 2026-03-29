/*
 * led_matrix.c
 * 8x8 RGB LED Matrix driver using WS2812
 */

#include "led_matrix.h"
#include "main.h"
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
static uint8_t key_brightness[6] = {0, 0, 0, 0, 0, 0};

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

/**
 * @brief Push one runtime pixel to WS2812 buffer
 */
static inline void push_runtime_pixel_to_ws2812(uint8_t index) {
  uint8_t r = apply_brightness(pixels_runtime[index * 3 + 0], current_brightness);
  uint8_t g = apply_brightness(pixels_runtime[index * 3 + 1], current_brightness);
  uint8_t b = apply_brightness(pixels_runtime[index * 3 + 2], current_brightness);
  setLedValues(&led_ws2812_handle, index, r, g, b);
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
}

static void sync_runtime_from_static(void) {
  memcpy(pixels_runtime, pixels_static, LED_MATRIX_DATA_SIZE);
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
    }
    return;
  }

  if (current_effect == LED_EFFECT_STATIC_MATRIX) {
    memset(pixels_static, 0, sizeof(pixels_static));
    memset(pixels_runtime, 0, sizeof(pixels_runtime));
    if (initialized && display_enabled) {
      zeroLedValues(&led_ws2812_handle);
    }
    return;
  }

  if (current_effect == LED_EFFECT_THIRD_PARTY) {
    memset(pixels_runtime, 0, sizeof(pixels_runtime));
    if (initialized && display_enabled) {
      zeroLedValues(&led_ws2812_handle);
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
  for (uint8_t y = 0; y < LED_MATRIX_HEIGHT; y++) {
    for (uint8_t x = 0; x < LED_MATRIX_WIDTH; x++) {
      uint8_t hue = offset + x * 16 + y * 16;
      uint8_t r, g, b;
      led_matrix_hsv_to_rgb(hue, 255, 255, &r, &g, &b);
      led_matrix_set_pixel(x, y, r, g, b);
    }
  }
}

//--------------------------------------------------------------------+
// LED Effect Implementation
//--------------------------------------------------------------------+

void led_matrix_set_effect(led_effect_mode_t mode) {
  led_effect_mode_t previous_effect = current_effect;
  current_effect = mode;
  effect_offset = 0;

  // Restoring matrix mode must restore the exact saved pattern.
  if (mode == LED_EFFECT_STATIC_MATRIX && previous_effect != LED_EFFECT_STATIC_MATRIX) {
    sync_runtime_from_static();
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
      GPIO_InitStruct.Alternate = GPIO_AF2_TIM3;
      HAL_GPIO_Init(LED_DATA_GPIO_Port, &GPIO_InitStruct);
    }
  }
}

uint8_t led_matrix_get_diagnostic_mode(void) { return diagnostic_mode; }

void led_matrix_key_event(uint8_t key_index, bool pressed) {
  if (key_index >= 6)
    return;
  if (pressed) {
    key_brightness[key_index] = 255;
  }
}

/**
 * @brief Rainbow Wave effect - diagonal rainbow cycling
 */
static void effect_rainbow(void) {
  for (uint8_t y = 0; y < LED_MATRIX_HEIGHT; y++) {
    for (uint8_t x = 0; x < LED_MATRIX_WIDTH; x++) {
      uint8_t hue = (x * 20) + (y * 20) + (effect_offset * 2);
      uint8_t r, g, b;
      led_matrix_hsv_to_rgb(hue, 255, 255, &r, &g, &b);
      led_matrix_set_pixel(x, y, r, g, b);
    }
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
  for (uint8_t y = 0; y < LED_MATRIX_HEIGHT; y++) {
    for (uint8_t x = 0; x < LED_MATRIX_WIDTH; x++) {
      uint8_t hue = (x * 32) + (y * 32);
      uint8_t r, g, b;
      led_matrix_hsv_to_rgb(hue, 255, 255, &r, &g, &b);
      led_matrix_set_pixel(x, y, r, g, b);
    }
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
  for (uint8_t y = 0; y < LED_MATRIX_HEIGHT; y++) {
    for (uint8_t x = 0; x < LED_MATRIX_WIDTH; x++) {
      // Combine multiple waves using integer math
      uint8_t v1 = x * 16 + effect_offset;
      uint8_t v2 = y * 16 + effect_offset;
      uint8_t v3 = (x + y) * 12 + effect_offset * 2;
      uint8_t v4 = ((x - 4) * (x - 4) + (y - 4) * (y - 4)) + effect_offset;
      uint8_t hue = v1 + v2 + v3 + v4;
      uint8_t r, g, b;
      led_matrix_hsv_to_rgb(hue, 255, 255, &r, &g, &b);
      led_matrix_set_pixel(x, y, r, g, b);
    }
  }
}

/**
 * @brief Fire effect - flames rising from bottom
 */
static void effect_fire(void) {
  for (uint8_t y = 0; y < LED_MATRIX_HEIGHT; y++) {
    for (uint8_t x = 0; x < LED_MATRIX_WIDTH; x++) {
      uint8_t r, g, b;
      // Fire rises from bottom, intensity based on y position
      uint8_t heat = (7 - y) * 30 + ((effect_offset + x * 7) % 40);

      if (heat > 200) {
        r = 255;
        g = 255;
        b = 128; // White-yellow tip
      } else if (heat > 150) {
        r = 255;
        g = 170;
        b = 0; // Yellow
      } else if (heat > 80) {
        r = 255;
        g = 64;
        b = 0; // Orange
      } else if (heat > 40) {
        r = 128;
        g = 0;
        b = 0; // Red
      } else {
        r = g = b = 0; // Dark
      }
      led_matrix_set_pixel(x, y, r, g, b);
    }
  }
}

/**
 * @brief Ocean waves effect - horizontal waves
 */
static void effect_ocean(void) {
  for (uint8_t y = 0; y < LED_MATRIX_HEIGHT; y++) {
    for (uint8_t x = 0; x < LED_MATRIX_WIDTH; x++) {
      uint8_t wave = ((x * 32) + (effect_offset * 3)) % 256;
      uint8_t depth = (y * 20);
      uint8_t hue = 140 + (wave / 16) + depth / 8; // Blue-cyan range
      uint8_t brightness = 255 - (depth * 255 / 200);
      if (brightness > 255)
        brightness = 0; // Underflow protection
      uint8_t r, g, b;
      led_matrix_hsv_to_rgb(hue, 255, brightness, &r, &g, &b);
      led_matrix_set_pixel(x, y, r, g, b);
    }
  }
}

/**
 * @brief Matrix rain effect - digital rain
 */
static void effect_matrix(void) {
  for (uint8_t y = 0; y < LED_MATRIX_HEIGHT; y++) {
    for (uint8_t x = 0; x < LED_MATRIX_WIDTH; x++) {
      uint8_t r, g, b;
      // Each column has independent "drops"
      uint8_t col_offset = x * 37; // Prime for pseudo-random
      uint8_t drop_pos = ((effect_offset + col_offset) / 3) % 16;
      int8_t dist = (int8_t)y - (int8_t)(drop_pos % 8);
      if (dist < 0)
        dist = -dist;

      if (dist == 0) {
        r = 255;
        g = 255;
        b = 255; // Head (white)
      } else if (dist < 4) {
        r = 0;
        g = 255 / dist;
        b = 0; // Trail (green fade)
      } else {
        r = g = b = 0;
      }
      led_matrix_set_pixel(x, y, r, g, b);
    }
  }
}

/**
 * @brief Sparkle/Twinkle effect
 */
static void effect_sparkle(void) {
  for (uint8_t y = 0; y < LED_MATRIX_HEIGHT; y++) {
    for (uint8_t x = 0; x < LED_MATRIX_WIDTH; x++) {
      uint8_t r, g, b;
      // Pseudo-random sparkle based on position and frame
      uint8_t seed = (x * 13 + y * 7 + effect_offset) % 32;

      if (seed == 0) {
        r = g = b = 255; // White sparkle
      } else if (seed < 3) {
        uint8_t hue = effect_offset + x * 32;
        led_matrix_hsv_to_rgb(hue, 255, 128, &r, &g, &b);
      } else {
        r = g = b = 0;
      }
      led_matrix_set_pixel(x, y, r, g, b);
    }
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
  for (uint8_t y = 0; y < LED_MATRIX_HEIGHT; y++) {
    for (uint8_t x = 0; x < LED_MATRIX_WIDTH; x++) {
      // Distance from center + angle creates spiral
      int8_t cx = x - 4;
      int8_t cy = y - 4;
      uint8_t dist = (cx * cx + cy * cy);              // Squared distance
      uint8_t angle = ((cx + 4) * 32 + (cy + 4) * 32); // Approximation
      uint8_t hue = dist * 8 + angle + effect_offset * 3;
      uint8_t r, g, b;
      led_matrix_hsv_to_rgb(hue, 255, 255, &r, &g, &b);
      led_matrix_set_pixel(x, y, r, g, b);
    }
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
 * @brief Reactive effect - lights up on key presses
 */
static void effect_reactive(void) {
  led_matrix_clear();

  // Each key maps to a column or area of the matrix
  // Keys 0-5 map to columns (with 2 pixels per key)
  for (uint8_t key = 0; key < 6; key++) {
    if (key_brightness[key] > 0) {
      uint8_t col = key;
      uint8_t brightness = key_brightness[key];

      uint8_t r = (uint8_t)((uint16_t)effect_color_r * brightness / 255);
      uint8_t g = (uint8_t)((uint16_t)effect_color_g * brightness / 255);
      uint8_t b = (uint8_t)((uint16_t)effect_color_b * brightness / 255);

      // Light up the column for this key
      for (uint8_t y = 0; y < LED_MATRIX_HEIGHT; y++) {
        led_matrix_set_pixel(col, y, r, g, b);
        if (col + 1 < LED_MATRIX_WIDTH) {
          led_matrix_set_pixel(col + 1, y, r, g, b);
        }
      }

      // Fade out
      if (key_brightness[key] > 8) {
        key_brightness[key] -= 8;
      } else {
        key_brightness[key] = 0;
      }
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
    for (uint8_t y = 0; y < LED_MATRIX_HEIGHT; y++) {
      for (uint8_t x = 0; x < LED_MATRIX_WIDTH; x++) {
        uint8_t hue = (x * 20) + (y * 20) + (effect_offset * 2);
        uint8_t r, g, b;
        led_matrix_hsv_to_rgb(hue, 255, 255, &r, &g, &b);
        // Store in local pixels array but DON'T send to WS2812
        uint8_t idx = y * LED_MATRIX_WIDTH + x;
        pixels_runtime[idx * 3 + 0] = r;
        pixels_runtime[idx * 3 + 1] = g;
        pixels_runtime[idx * 3 + 2] = b;
      }
    }
    // Explicitly clear dirty flag to prevent DMA
    led_ws2812_handle.is_dirty = 0;
    return;
  }

  if (diagnostic_mode == 3) {
    // Mode 3: CPU + DMA Stress, no PWM output
    // Compute effect AND fill DMA buffer, but timer output is disabled
    // This tests if DMA memory access causes issues even without pin toggling
    for (uint8_t y = 0; y < LED_MATRIX_HEIGHT; y++) {
      for (uint8_t x = 0; x < LED_MATRIX_WIDTH; x++) {
        uint8_t hue = (x * 20) + (y * 20) + (effect_offset * 2);
        uint8_t r, g, b;
        led_matrix_hsv_to_rgb(hue, 255, 255, &r, &g, &b);
        // Update WS2812 buffer (this will trigger DMA buffer updates)
        uint8_t idx = y * LED_MATRIX_WIDTH + x;
        setLedValues(&led_ws2812_handle, idx, r, g, b);
      }
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
  default:
    break;
  }
  effect_render_context = false;
}
