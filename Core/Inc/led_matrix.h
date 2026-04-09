/*
 * led_matrix.h
 * RGB LED strip/matrix driver using WS2812
 */

#ifndef LED_MATRIX_H_
#define LED_MATRIX_H_

#include "board_config.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

//--------------------------------------------------------------------+
// LED Matrix Configuration
//--------------------------------------------------------------------+
#define LED_MATRIX_WIDTH 8
#define LED_MATRIX_HEIGHT 8
#define LED_MATRIX_NUM_LEDS NUM_KEYS

// Each LED has 3 bytes: R, G, B
#define LED_MATRIX_DATA_SIZE (LED_MATRIX_NUM_LEDS * 3)

// Brightness limits
#define LED_BRIGHTNESS_MIN 0
#define LED_BRIGHTNESS_MAX 255
#define LED_BRIGHTNESS_DEFAULT 50
// Maximum number of persisted tuning bytes available per effect.
#define LED_EFFECT_PARAM_COUNT 6

//--------------------------------------------------------------------+
// LED Effect Modes
//--------------------------------------------------------------------+
typedef enum {
  LED_EFFECT_NONE = 0,               // Matrix (software static pattern)
  LED_EFFECT_RAINBOW = 1,            // Rainbow wave (diagonal)
  LED_EFFECT_BREATHING = 2,          // Breathing (fade in/out)
  LED_EFFECT_STATIC_RAINBOW = 3,     // Static rainbow pattern
  LED_EFFECT_SOLID = 4,              // Solid color
  LED_EFFECT_PLASMA = 5,             // Plasma (psychedelic waves)
  LED_EFFECT_FIRE = 6,               // Fire effect
  LED_EFFECT_OCEAN = 7,              // Ocean waves (horizontal)
  LED_EFFECT_MATRIX = 8,             // Matrix rain (digital rain)
  LED_EFFECT_SPARKLE = 9,            // Sparkle/Twinkle
  LED_EFFECT_BREATHING_RAINBOW = 10, // Breathing rainbow
  LED_EFFECT_SPIRAL = 11,            // Spiral
  LED_EFFECT_COLOR_CYCLE = 12,       // Solid color cycle
  LED_EFFECT_REACTIVE = 13,          // React to key presses
  LED_EFFECT_THIRD_PARTY = 14,       // External/third-party live control
  LED_EFFECT_DISTANCE_SENSOR = 15,   // Per-key color from sensor distance
  LED_EFFECT_MAX
} led_effect_mode_t;

#define LED_EFFECT_STATIC_MATRIX LED_EFFECT_NONE

//--------------------------------------------------------------------+
// LED Matrix Data Structure (for storage)
//--------------------------------------------------------------------+
typedef struct __attribute__((packed)) {
  uint8_t pixels[LED_MATRIX_DATA_SIZE];
  uint8_t brightness;               // Global brightness (0-255)
  uint8_t enabled;                  // Enable/disable display
  uint8_t reserved[2];              // Padding for alignment
} led_matrix_data_t;

//--------------------------------------------------------------------+
// LED Matrix API
//--------------------------------------------------------------------+

/**
 * @brief Initialize LED matrix driver
 * @param htim Pointer to TIM handle for PWM/DMA
 * @param channel Timer channel (e.g., TIM_CHANNEL_2)
 * @return true if successful
 */
bool led_matrix_init(void *htim, uint32_t channel);

/**
 * @brief Set a single pixel color
 * @param x X position (0-7)
 * @param y Y position (0-7)
 * @param r Red value (0-255)
 * @param g Green value (0-255)
 * @param b Blue value (0-255)
 */
void led_matrix_set_pixel(uint8_t x, uint8_t y, uint8_t r, uint8_t g,
                          uint8_t b);

/**
 * @brief Get a single pixel color
 * @param x X position (0-7)
 * @param y Y position (0-7)
 * @param r Pointer to store red value
 * @param g Pointer to store green value
 * @param b Pointer to store blue value
 */
void led_matrix_get_pixel(uint8_t x, uint8_t y, uint8_t *r, uint8_t *g,
                          uint8_t *b);

/**
 * @brief Set pixel by linear index
 * @param index LED index (0-63)
 * @param r Red value
 * @param g Green value
 * @param b Blue value
 */
void led_matrix_set_pixel_idx(uint8_t index, uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief Clear all pixels (set to black)
 */
void led_matrix_clear(void);

/**
 * @brief Fill all pixels with a color
 * @param r Red value
 * @param g Green value
 * @param b Blue value
 */
void led_matrix_fill(uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief Set global brightness
 * @param brightness Brightness value (0-255)
 */
void led_matrix_set_brightness(uint8_t brightness);

/**
 * @brief Get global brightness
 * @return Current brightness value
 */
uint8_t led_matrix_get_brightness(void);

/**
 * @brief Enable/disable LED display
 * @param enabled true to enable, false to disable
 */
void led_matrix_set_enabled(bool enabled);

/**
 * @brief Check if LED display is enabled
 * @return true if enabled
 */
bool led_matrix_is_enabled(void);

/**
 * @brief Update the LED strip (apply changes)
 * This is called automatically but can be forced
 */
void led_matrix_update(void);

/**
 * @brief Get raw pixel data for storage/transfer
 * @return Pointer to pixel data (192 bytes)
 */
const uint8_t *led_matrix_get_raw_data(void);

/**
 * @brief Set raw pixel data from storage/transfer
 * @param data Pointer to pixel data (192 bytes)
 */
void led_matrix_set_raw_data(const uint8_t *data);

/**
 * @brief Get complete LED data structure for saving
 * @return Current LED data
 */
led_matrix_data_t led_matrix_get_data(void);

/**
 * @brief Load complete LED data structure from storage
 * @param data LED data to load
 */
void led_matrix_load_data(const led_matrix_data_t *data);

/**
 * @brief HSV to RGB conversion utility
 * @param h Hue (0-255)
 * @param s Saturation (0-255)
 * @param v Value/Brightness (0-255)
 * @param r Output red
 * @param g Output green
 * @param b Output blue
 */
void led_matrix_hsv_to_rgb(uint8_t h, uint8_t s, uint8_t v, uint8_t *r,
                           uint8_t *g, uint8_t *b);

/**
 * @brief Show a rainbow test pattern
 * @param offset Animation offset for cycling
 */
void led_matrix_test_rainbow(uint8_t offset);

//--------------------------------------------------------------------+
// LED Effect Functions
//--------------------------------------------------------------------+

/**
 * @brief Set the current effect mode
 * @param mode Effect mode from led_effect_mode_t
 */
void led_matrix_set_effect(led_effect_mode_t mode);

/**
 * @brief Get the current effect mode
 * @return Current effect mode
 */
led_effect_mode_t led_matrix_get_effect(void);

/**
 * @brief Set effect color (for single-color effects)
 * @param r Red value
 * @param g Green value
 * @param b Blue value
 */
void led_matrix_set_effect_color(uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief Load runtime parameters for the active effect.
 * @param params Array of LED_EFFECT_PARAM_COUNT bytes.
 */
void led_matrix_set_effect_params(const uint8_t *params);

/**
 * @brief Read runtime parameters for the active effect.
 * @param params Output array of LED_EFFECT_PARAM_COUNT bytes.
 */
void led_matrix_get_effect_params(uint8_t *params);

/**
 * @brief Set effect speed (animation rate)
 * @param speed Speed value (1-255, higher = faster)
 */
void led_matrix_set_effect_speed(uint8_t speed);

/**
 * @brief Get effect speed
 * @return Current effect speed
 */
uint8_t led_matrix_get_effect_speed(void);

/**
 * @brief Set FPS limit for LED effects
 * @param fps FPS value (1-255, 0 = unlimited)
 */
void led_matrix_set_fps_limit(uint8_t fps);

/**
 * @brief Get FPS limit
 * @return Current FPS limit (0 = unlimited)
 */
uint8_t led_matrix_get_fps_limit(void);

/**
 * @brief Set diagnostic mode for troubleshooting
 * @param mode 0=normal, 1=DMA stress (no CPU), 2=CPU stress (no DMA)
 */
void led_matrix_set_diagnostic_mode(uint8_t mode);

/**
 * @brief Get diagnostic mode
 * @return Current diagnostic mode
 */
uint8_t led_matrix_get_diagnostic_mode(void);

/**
 * @brief Update effect animation (call periodically from main loop)
 * @param tick Current system tick (ms)
 */
void led_matrix_effect_tick(uint32_t tick);

/**
 * @brief Trigger reactive effect on key press
 * @param key_index Key index (0-81)
 * @param pressed true if pressed, false if released
 */
void led_matrix_key_event(uint8_t key_index, bool pressed);

/**
 * @brief Show a temporary progress bar on the function row.
 * @param level Progress level in the 0-255 range.
 */
void led_matrix_set_progress_overlay(uint8_t level);

/**
 * @brief Clear the temporary progress overlay immediately.
 */
void led_matrix_clear_progress_overlay(void);

/**
 * @brief Backward-compatible wrapper for the host volume progress bar.
 * @param level Volume level in the 0-255 range.
 */
void led_matrix_set_volume_overlay(uint8_t level);

/**
 * @brief Backward-compatible wrapper to clear the host volume overlay.
 */
void led_matrix_clear_volume_overlay(void);

/**
 * @brief Cache the last host-reported PC volume level.
 * The cached value is only displayed when explicitly triggered,
 * typically by the rotary encoder.
 */
void led_matrix_set_host_volume_level(uint8_t level);

/**
 * @brief Clear the cached host volume level.
 */
void led_matrix_clear_host_volume_level(void);

/**
 * @brief Display the cached host volume level as a temporary overlay.
 * If the host has not refreshed a recent volume value, nothing is shown.
 */
void led_matrix_show_host_volume_overlay(void);

/**
 * @brief Replace the live runtime frame in one shot.
 * Useful for third-party/live rendering paths that must avoid
 * intermediate per-pixel WS2812 updates.
 * @param data RGB triplets in logical key order.
 */
void led_matrix_set_live_frame(const uint8_t *data);

#ifdef __cplusplus
}
#endif

#endif /* LED_MATRIX_H_ */
