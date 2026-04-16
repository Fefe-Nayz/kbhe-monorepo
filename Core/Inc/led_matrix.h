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
#define LED_EFFECT_PARAM_COUNT 16

// Shared parameter slots used by multiple effects for per-effect colors.
#define LED_EFFECT_PARAM_COLOR_R 8
#define LED_EFFECT_PARAM_COLOR_G 9
#define LED_EFFECT_PARAM_COLOR_B 10
#define LED_EFFECT_PARAM_COLOR2_R 11
#define LED_EFFECT_PARAM_COLOR2_G 12
#define LED_EFFECT_PARAM_COLOR2_B 13
// Shared parameter slot for per-effect animation speed.
#define LED_EFFECT_PARAM_SPEED 14

#define LED_AUDIO_SPECTRUM_BAND_COUNT 16

//--------------------------------------------------------------------+
// Per-effect parameter schema (Phase-2A)
// Each effect exposes up to LED_EFFECT_PARAM_COUNT descriptors that
// describe how the host should interpret and edit each byte slot.
// The schema is read-only ROM data; values are stored separately in
// settings as raw LED_EFFECT_PARAM_COUNT-byte blocks.
//--------------------------------------------------------------------+

// Parameter value types (1 byte, fits in a HID packet field).
typedef enum {
  LED_PARAM_TYPE_NONE  = 0, // Slot not used by this effect
  LED_PARAM_TYPE_U8    = 1, // Generic 0-255 integer (uses min/max/step)
  LED_PARAM_TYPE_BOOL  = 2, // Boolean flag (min=0, max=1, step=1)
  LED_PARAM_TYPE_HUE   = 3, // Hue wheel 0-255 (full rotation)
  LED_PARAM_TYPE_COLOR = 4, // First byte of an RGB triplet; host groups
                             // COLOR slots at ids n, n+1, n+2 as one
                             // color picker.  min/max/step unused.
} led_param_type_t;

// Descriptor for a single parameter slot.
typedef struct __attribute__((packed)) {
  uint8_t id;          // Slot index within the 16-byte param block (0-15)
  uint8_t type;        // led_param_type_t
  uint8_t min;         // Minimum value (inclusive)
  uint8_t max;         // Maximum value (inclusive)
  uint8_t default_val; // Factory default for this slot
  uint8_t step;        // Suggested UI step size (0 = continuous)
} led_param_desc_t;

// Maximum descriptors returned per schema chunk over HID.
// 6 bytes/desc × 9 = 54 bytes; leaves 8 bytes for chunk header in 64-byte packet.
#define LED_SCHEMA_PARAMS_PER_CHUNK 9u

/**
 * @brief Return the schema (param descriptors) for one effect.
 *
 * Writes up to LED_EFFECT_PARAM_COUNT descriptors into @p out.
 * Returns the number of active (non-NONE) descriptors written.
 *
 * @param effect_mode  Effect ID (0 .. LED_EFFECT_MAX-1)
 * @param out          Output array; caller must provide at least
 *                     LED_EFFECT_PARAM_COUNT entries.
 * @return             Number of active descriptors (type != NONE).
 */
uint8_t led_matrix_get_effect_schema(uint8_t effect_mode,
                                     led_param_desc_t *out);

//--------------------------------------------------------------------+
// LED Effect Modes
// Compact numbering after Phase-1B fusion (breaking change, v0x0017+).
// Old pre-fusion IDs are documented in docs/LED_EFFECTS_CANONICAL_PHASE0.md.
//--------------------------------------------------------------------+
typedef enum {
  LED_EFFECT_NONE = 0,                    // Static matrix (software pattern)
  LED_EFFECT_PLASMA = 1,                  // Plasma (psychedelic waves)
  LED_EFFECT_FIRE = 2,                    // Fire effect
  LED_EFFECT_OCEAN = 3,                   // Ocean waves (horizontal)
  LED_EFFECT_SPARKLE = 4,                 // Sparkle/Twinkle
  LED_EFFECT_BREATHING_RAINBOW = 5,       // Breathing rainbow
  LED_EFFECT_COLOR_CYCLE = 6,             // Solid color cycle
  LED_EFFECT_THIRD_PARTY = 7,             // External/third-party live control
  LED_EFFECT_DISTANCE_SENSOR = 8,         // Per-key color from sensor distance
  LED_EFFECT_IMPACT_RAINBOW = 9,          // Rainbow with key/audio impacts
  LED_EFFECT_REACTIVE_GHOST = 10,         // Keypress ghost trails
  LED_EFFECT_AUDIO_SPECTRUM = 11,         // Host audio spectrum bars
  LED_EFFECT_KEY_STATE_DEMO = 12,         // Digital key-state demo (red/green)
  LED_EFFECT_CYCLE_PINWHEEL = 13,         // Rotating pinwheel
  LED_EFFECT_CYCLE_SPIRAL = 14,           // Spiral cycle (fused: old SPIRAL)
  LED_EFFECT_CYCLE_OUT_IN_DUAL = 15,      // Dual-center out-in (fused: DUAL_SPHERE)
  LED_EFFECT_RAINBOW_BEACON = 16,         // Rotating rainbow stripe (fused: STRIP_SPIN_ZOOM)
  LED_EFFECT_RAINBOW_PINWHEELS = 17,      // Rainbow pinwheels
  LED_EFFECT_RAINBOW_MOVING_CHEVRON = 18, // Moving chevron
  LED_EFFECT_HUE_BREATHING = 19,          // Hue breathing
  LED_EFFECT_HUE_PENDULUM = 20,           // Hue pendulum
  LED_EFFECT_HUE_WAVE = 21,               // Hue wave
  LED_EFFECT_RIVERFLOW = 22,              // Riverflow
  LED_EFFECT_SOLID_COLOR = 23,            // Solid color (fused: old SOLID)
  LED_EFFECT_ALPHA_MODS = 24,             // Alpha mods highlight
  LED_EFFECT_GRADIENT_UP_DOWN = 25,       // Vertical gradient
  LED_EFFECT_GRADIENT_LEFT_RIGHT = 26,    // Horizontal gradient (fused: STATIC_RAINBOW)
  LED_EFFECT_BREATHING = 27,              // Breathing (fused: old BREATHING)
  LED_EFFECT_COLORBAND_SAT = 28,          // Colorband saturation
  LED_EFFECT_COLORBAND_VAL = 29,          // Colorband value
  LED_EFFECT_COLORBAND_PINWHEEL_SAT = 30, // Colorband pinwheel saturation
  LED_EFFECT_COLORBAND_PINWHEEL_VAL = 31, // Colorband pinwheel value
  LED_EFFECT_COLORBAND_SPIRAL_SAT = 32,   // Colorband spiral saturation
  LED_EFFECT_COLORBAND_SPIRAL_VAL = 33,   // Colorband spiral value
  LED_EFFECT_CYCLE_ALL = 34,              // Cycle all
  LED_EFFECT_CYCLE_LEFT_RIGHT = 35,       // Cycle left-right (fused: old RAINBOW)
  LED_EFFECT_CYCLE_UP_DOWN = 36,          // Cycle up-down
  LED_EFFECT_CYCLE_OUT_IN = 37,           // Cycle out-in (fused: SPHERE)
  LED_EFFECT_DUAL_BEACON = 38,            // Dual beacon
  LED_EFFECT_FLOWER_BLOOMING = 39,        // Flower blooming
  LED_EFFECT_RAINDROPS = 40,              // Raindrops
  LED_EFFECT_JELLYBEAN_RAINDROPS = 41,    // Jellybean raindrops
  LED_EFFECT_PIXEL_RAIN = 42,             // Pixel rain
  LED_EFFECT_PIXEL_FLOW = 43,             // Pixel flow
  LED_EFFECT_PIXEL_FRACTAL = 44,          // Pixel fractal
  LED_EFFECT_TYPING_HEATMAP = 45,         // Typing heatmap (fused: REACTIVE_HEATMAP)
  LED_EFFECT_DIGITAL_RAIN = 46,           // Digital rain (fused: old MATRIX)
  LED_EFFECT_SOLID_REACTIVE_SIMPLE = 47,  // Solid reactive simple
  LED_EFFECT_SOLID_REACTIVE = 48,         // Solid reactive
  LED_EFFECT_SOLID_REACTIVE_WIDE = 49,    // Solid reactive wide
  LED_EFFECT_SOLID_REACTIVE_CROSS = 50,   // Solid reactive cross
  LED_EFFECT_SOLID_REACTIVE_NEXUS = 51,   // Solid reactive nexus
  LED_EFFECT_SPLASH = 52,                 // Splash (fused: old REACTIVE)
  LED_EFFECT_SOLID_SPLASH = 53,           // Solid splash
  LED_EFFECT_STARLIGHT_SMOOTH = 54,       // Starlight smooth
  LED_EFFECT_STARLIGHT = 55,              // Starlight
  LED_EFFECT_STARLIGHT_DUAL_SAT = 56,     // Starlight dual saturation
  LED_EFFECT_STARLIGHT_DUAL_HUE = 57,     // Starlight dual hue
  LED_EFFECT_SOLID_REACTIVE_MULTI_WIDE = 58,  // Solid reactive multi wide
  LED_EFFECT_SOLID_REACTIVE_MULTI_CROSS = 59, // Solid reactive multi cross
  LED_EFFECT_SOLID_REACTIVE_MULTI_NEXUS = 60, // Solid reactive multi nexus
  LED_EFFECT_MULTI_SPLASH = 61,           // Multi splash
  LED_EFFECT_SOLID_MULTI_SPLASH = 62,     // Solid multi splash
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
 * @brief Compatibility setter for the active effect color.
 *
 * The RGB value maps to the active effect parameter block
 * (`LED_EFFECT_PARAM_COLOR_*`).
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
 * @brief Predictively nudge the cached host volume level for immediate rotary
 *        overlay feedback until the next real host refresh arrives.
 * @param direction Positive for volume up, negative for volume down.
 * @param steps Number of consumer volume steps emitted for this rotary detent.
 */
void led_matrix_nudge_host_volume_overlay(int8_t direction, uint8_t steps);

/**
 * @brief Replace the live runtime frame in one shot.
 * Useful for third-party/live rendering paths that must avoid
 * intermediate per-pixel WS2812 updates.
 * @param data RGB triplets in logical key order.
 */
void led_matrix_set_live_frame(const uint8_t *data);

/**
 * @brief Force a temporary full-frame output override on top of any effect or
 *        third-party live stream.
 * @param data RGB triplets in logical key order.
 */
void led_matrix_set_output_override_frame(const uint8_t *data);

/**
 * @brief Clear the temporary output override frame.
 */
void led_matrix_clear_output_override_frame(void);

/**
 * @brief Push host audio spectrum data for audio-reactive effects.
 * @param bands Spectrum levels in 0-255.
 * @param band_count Number of entries in @p bands.
 * @param impact_level Optional transient impact level (0-255).
 */
void led_matrix_set_audio_spectrum(const uint8_t *bands, uint8_t band_count,
                                   uint8_t impact_level);

/**
 * @brief Clear host audio spectrum state immediately.
 */
void led_matrix_clear_audio_spectrum(void);

#ifdef __cplusplus
}
#endif

#endif /* LED_MATRIX_H_ */
