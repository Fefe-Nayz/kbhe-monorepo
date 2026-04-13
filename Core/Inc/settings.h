/*
 * settings.h
 * Keyboard settings storage with EEPROM emulation
 */

#ifndef SETTINGS_H_
#define SETTINGS_H_

#include "analog/filter.h"
#include "board_config.h"
#include "led_matrix.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

//--------------------------------------------------------------------+
// Settings Magic Numbers
//--------------------------------------------------------------------+
#define SETTINGS_MAGIC_START 0x4B424845 // "KBHE"
#define SETTINGS_MAGIC_END 0x454E4421   // "END!"
#define SETTINGS_VERSION 0x0012         // Device identity: persistent keyboard name

//--------------------------------------------------------------------+
// LED Matrix Constants
//--------------------------------------------------------------------+
#define LED_MATRIX_SIZE NUM_KEYS
#define LED_MATRIX_DATA_BYTES (LED_MATRIX_SIZE * 3)

//--------------------------------------------------------------------+
// Analog Curve Constants
//--------------------------------------------------------------------+
#define ANALOG_CURVE_POINTS 4 // 4-point bezier curve (P0, P1, P2, P3)
#define GAMEPAD_CURVE_POINT_COUNT 4u
#define GAMEPAD_CURVE_MAX_DISTANCE_01MM 400u // 4.00 mm
#define SETTINGS_LAYER_COUNT 4u
#define SETTINGS_DYNAMIC_ZONE_COUNT 4u
#define SETTINGS_ADVANCED_TICK_RATE_MIN 1u
#define SETTINGS_ADVANCED_TICK_RATE_MAX 100u
#define SETTINGS_DEFAULT_ADVANCED_TICK_RATE 1u
#define SETTINGS_KEYBOARD_NAME_LENGTH 32u

//--------------------------------------------------------------------+
// Gamepad Mapping Constants
//--------------------------------------------------------------------+
typedef enum {
  GAMEPAD_AXIS_NONE = 0,
  GAMEPAD_AXIS_LEFT_X = 1,
  GAMEPAD_AXIS_LEFT_Y = 2,
  GAMEPAD_AXIS_RIGHT_X = 3,
  GAMEPAD_AXIS_RIGHT_Y = 4,
  GAMEPAD_AXIS_TRIGGER_L = 5,
  GAMEPAD_AXIS_TRIGGER_R = 6
} gamepad_axis_t;

typedef enum {
  GAMEPAD_DIR_POSITIVE = 0, // Press increases axis value
  GAMEPAD_DIR_NEGATIVE = 1  // Press decreases axis value
} gamepad_direction_t;

typedef enum {
  GAMEPAD_KEYBOARD_ROUTING_DISABLED = 0,
  GAMEPAD_KEYBOARD_ROUTING_ALL_KEYS = 1,
  GAMEPAD_KEYBOARD_ROUTING_UNMAPPED_ONLY = 2,
  GAMEPAD_KEYBOARD_ROUTING_MAX
} gamepad_keyboard_routing_t;

typedef enum {
  GAMEPAD_API_HID = 0,
  GAMEPAD_API_XINPUT = 1,
  GAMEPAD_API_MAX
} gamepad_api_mode_t;

typedef enum {
  GAMEPAD_BUTTON_NONE = 0,
  GAMEPAD_BUTTON_A = 1,
  GAMEPAD_BUTTON_B = 2,
  GAMEPAD_BUTTON_X = 3,
  GAMEPAD_BUTTON_Y = 4,
  GAMEPAD_BUTTON_L1 = 5,
  GAMEPAD_BUTTON_R1 = 6,
  GAMEPAD_BUTTON_L2 = 7,
  GAMEPAD_BUTTON_R2 = 8,
  GAMEPAD_BUTTON_SELECT = 9,
  GAMEPAD_BUTTON_START = 10,
  GAMEPAD_BUTTON_L3 = 11,
  GAMEPAD_BUTTON_R3 = 12,
  GAMEPAD_BUTTON_DPAD_UP = 13,
  GAMEPAD_BUTTON_DPAD_DOWN = 14,
  GAMEPAD_BUTTON_DPAD_LEFT = 15,
  GAMEPAD_BUTTON_DPAD_RIGHT = 16,
  GAMEPAD_BUTTON_HOME = 17
} gamepad_button_t;

typedef enum {
  ROTARY_ACTION_VOLUME = 0,
  ROTARY_ACTION_LED_BRIGHTNESS = 1,
  ROTARY_ACTION_LED_EFFECT_SPEED = 2,
  ROTARY_ACTION_LED_EFFECT_CYCLE = 3,
  ROTARY_ACTION_RGB_CUSTOMIZER = 4,
  ROTARY_ACTION_MAX
} rotary_action_t;

typedef enum {
  ROTARY_BUTTON_ACTION_PLAY_PAUSE = 0,
  ROTARY_BUTTON_ACTION_MUTE = 1,
  ROTARY_BUTTON_ACTION_TOGGLE_LED = 2,
  ROTARY_BUTTON_ACTION_CYCLE_LED_EFFECT = 3,
  ROTARY_BUTTON_ACTION_CYCLE_ROTARY_ACTION = 4,
  ROTARY_BUTTON_ACTION_MAX
} rotary_button_action_t;

typedef enum {
  ROTARY_RGB_BEHAVIOR_HUE = 0,
  ROTARY_RGB_BEHAVIOR_BRIGHTNESS = 1,
  ROTARY_RGB_BEHAVIOR_EFFECT_SPEED = 2,
  ROTARY_RGB_BEHAVIOR_EFFECT_CYCLE = 3,
  ROTARY_RGB_BEHAVIOR_MAX
} rotary_rgb_behavior_t;

typedef enum {
  ROTARY_PROGRESS_STYLE_SOLID = 0,
  ROTARY_PROGRESS_STYLE_RAINBOW = 1,
  ROTARY_PROGRESS_STYLE_EFFECT_PALETTE = 2,
  ROTARY_PROGRESS_STYLE_MAX
} rotary_progress_style_t;

typedef struct __attribute__((packed)) {
  uint8_t rotation_action;
  uint8_t button_action;
  uint8_t sensitivity;
  uint8_t step_size;
  uint8_t invert_direction;
  uint8_t rgb_behavior;
  uint8_t rgb_effect_mode;
  uint8_t progress_style;
  uint8_t progress_effect_mode;
  uint8_t progress_color_r;
  uint8_t progress_color_g;
  uint8_t progress_color_b;
} settings_rotary_encoder_t;

//--------------------------------------------------------------------+
// Settings Structure
//--------------------------------------------------------------------+

/**
 * @brief Global keyboard options
 */
typedef struct __attribute__((packed)) {
  uint8_t keyboard_enabled : 1;      // Enable keyboard HID output
  uint8_t gamepad_enabled : 1;       // Enable gamepad HID output
  uint8_t raw_hid_echo : 1;          // Enable RAW HID echo mode
  uint8_t led_enabled : 1;           // Enable LED matrix
  uint8_t nkro_enabled : 1;          // Use NKRO instead of 6KRO keyboard
  uint8_t gamepad_with_keyboard : 1; // Send keyboard along with gamepad
  uint8_t reserved : 2;              // Reserved for future use
} settings_options_t;

/**
 * @brief Analog curve point (normalized 0-255 for both axes)
 */
typedef struct __attribute__((packed)) {
  uint8_t x; // X position (0-255, represents 0-4mm travel)
  uint8_t y; // Y value (0-255, represents analog output)
} curve_point_t;

/**
 * @brief Per-key analog curve (4-point bezier)
 * P0 is always (0,0), P3 is always (255,255)
 * Only P1 and P2 are configurable control points
 */
typedef struct __attribute__((packed)) {
  curve_point_t p1; // First control point
  curve_point_t p2; // Second control point
} settings_curve_t;

/**
 * @brief Per-key gamepad mapping
 */
typedef struct __attribute__((packed)) {
  uint8_t axis;      // gamepad_axis_t - which axis to map to (0 = none)
  uint8_t direction; // gamepad_direction_t - positive or negative
  uint8_t button;    // gamepad_button_t - button to press (0 = none)
  uint8_t reserved;  // Padding
} settings_gamepad_mapping_t;

typedef struct __attribute__((packed)) {
  uint16_t x_01mm; // Distance in 0.01 mm (0..400 for 0..4.00 mm)
  uint8_t y;       // Analog output value (0..255)
} settings_gamepad_curve_point_t;

typedef enum {
  KEY_BEHAVIOR_NORMAL = 0,
  KEY_BEHAVIOR_TAP_HOLD = 1,
  KEY_BEHAVIOR_TOGGLE = 2,
  KEY_BEHAVIOR_DYNAMIC = 3,
  KEY_BEHAVIOR_MAX
} key_behavior_mode_t;

typedef struct __attribute__((packed)) {
  uint8_t end_mm_tenths; // Inclusive upper bound of the zone in 0.1 mm
  uint16_t hid_keycode;  // Action sent while the key travel is in this zone
} settings_dynamic_zone_t;

typedef struct __attribute__((packed)) {
  uint8_t behavior_mode;            // key_behavior_mode_t
  uint8_t hold_threshold_10ms;      // Tap-hold / toggle hold threshold
  uint8_t dynamic_zone_count;       // 1..4 when dynamic mapping is enabled
  uint8_t reserved;
  uint16_t secondary_hid_keycode;   // Hold / alternate action
  settings_dynamic_zone_t dynamic_zones[SETTINGS_DYNAMIC_ZONE_COUNT];
} settings_key_advanced_t;

typedef enum {
  SETTINGS_SOCD_RESOLUTION_LAST_INPUT_WINS = 0,
  SETTINGS_SOCD_RESOLUTION_MOST_PRESSED_WINS = 1,
  SETTINGS_SOCD_RESOLUTION_MAX
} settings_socd_resolution_t;

#define SETTINGS_SOCD_PAIR_NONE 0xFFu
#define SETTINGS_KEY_SOCD_RESOLUTION_MASK 0x03u
#define SETTINGS_KEY_CONTINUOUS_RAPID_TRIGGER_MASK 0x04u

/**
 * @brief Key-specific settings
 */
typedef struct __attribute__((packed)) {
  uint16_t hid_keycode;       // HID keycode / custom keycode for this key
  uint8_t actuation_point_mm; // Actuation point in 0.1mm (e.g., 20 = 2.0mm)
  uint8_t release_point_mm;   // Release point in 0.1mm
  uint8_t rapid_trigger_activation; // Initial activation distance in 0.1mm
  uint8_t
      rapid_trigger_press; // Press sensitivity in 0.01mm (e.g., 30 = 0.30mm)
  uint8_t rapid_trigger_release;     // Release sensitivity in 0.01mm
  uint8_t socd_pair;                 // SOCD paired key index (255 = none)
  uint8_t rapid_trigger_enabled : 1; // Enable rapid trigger for this key
  uint8_t
      disable_kb_on_gamepad : 1; // Disable keyboard output when gamepad active
  uint8_t curve_enabled : 1;     // Enable custom analog curve
  uint8_t reserved_bits : 5;     // Reserved bits
  settings_curve_t curve;        // Per-key analog curve (4 bytes)
  settings_gamepad_mapping_t gamepad_map; // Per-key gamepad mapping (4 bytes)
  settings_key_advanced_t advanced;     // Advanced per-key behaviors
} settings_key_t;

static inline settings_socd_resolution_t
settings_key_get_socd_resolution(const settings_key_t *key) {
  uint8_t raw = 0u;
  if (key == 0) {
    return SETTINGS_SOCD_RESOLUTION_LAST_INPUT_WINS;
  }

  raw = key->reserved_bits & SETTINGS_KEY_SOCD_RESOLUTION_MASK;
  if (raw >= SETTINGS_SOCD_RESOLUTION_MAX) {
    return SETTINGS_SOCD_RESOLUTION_LAST_INPUT_WINS;
  }

  return (settings_socd_resolution_t)raw;
}

static inline void settings_key_set_socd_resolution(
    settings_key_t *key, settings_socd_resolution_t resolution) {
  uint8_t sanitized = (uint8_t)SETTINGS_SOCD_RESOLUTION_LAST_INPUT_WINS;
  if (key == 0) {
    return;
  }

  if ((uint8_t)resolution < (uint8_t)SETTINGS_SOCD_RESOLUTION_MAX) {
    sanitized = (uint8_t)resolution;
  }

  key->reserved_bits =
      (uint8_t)((key->reserved_bits & (uint8_t)(~SETTINGS_KEY_SOCD_RESOLUTION_MASK)) |
                (sanitized & SETTINGS_KEY_SOCD_RESOLUTION_MASK));
}

static inline bool
settings_key_is_continuous_rapid_trigger_enabled(const settings_key_t *key) {
  if (key == 0) {
    return false;
  }

  return (key->reserved_bits & SETTINGS_KEY_CONTINUOUS_RAPID_TRIGGER_MASK) != 0u;
}

static inline void settings_key_set_continuous_rapid_trigger(
    settings_key_t *key, bool enabled) {
  if (key == 0) {
    return;
  }

  if (enabled) {
    key->reserved_bits |= SETTINGS_KEY_CONTINUOUS_RAPID_TRIGGER_MASK;
  } else {
    key->reserved_bits =
        (uint8_t)(key->reserved_bits & (uint8_t)(~SETTINGS_KEY_CONTINUOUS_RAPID_TRIGGER_MASK));
  }
}

/**
 * @brief Calibration settings for ADC offset correction
 */
typedef struct __attribute__((packed)) {
  int16_t lut_zero_value;     // LUT reference zero value (default ~2118)
  int16_t key_zero_values[NUM_KEYS]; // Per-key zero values
  int16_t key_max_values[NUM_KEYS];  // Per-key fully pressed raw ADC values
} settings_calibration_t;

/**
 * @brief Gamepad settings
 */
typedef struct __attribute__((packed)) {
  uint8_t radial_deadzone;   // Deprecated compatibility field, curve[0].x_01mm is authoritative
  uint8_t keyboard_routing;  // gamepad_keyboard_routing_t
  uint8_t square_mode;       // Preserve full diagonal output
  uint8_t reactive_stick;    // Strongest opposing direction wins
  uint8_t api_mode;          // gamepad_api_mode_t
  settings_gamepad_curve_point_t curve[GAMEPAD_CURVE_POINT_COUNT];
} settings_gamepad_t;

/**
 * @brief LED matrix settings
 */
typedef struct __attribute__((packed)) {
  uint8_t pixels[LED_MATRIX_DATA_BYTES];
  uint8_t brightness;                    // Global brightness (0-255)
  uint8_t reserved[3];                   // Padding for alignment
} settings_led_t;

/**
 * @brief Complete settings structure stored in flash
 */
typedef struct __attribute__((packed)) {
  // Header
  uint32_t magic_start; // Magic number to identify valid settings
  uint16_t version;     // Settings version for migration
  uint16_t reserved;    // Padding

  // Global options
  settings_options_t options;
  uint8_t padding1[3]; // Alignment padding

  // Per-key settings
  settings_key_t keys[NUM_KEYS];
  uint16_t layer_keycodes[SETTINGS_LAYER_COUNT - 1u][NUM_KEYS];

  // Gamepad settings
  settings_gamepad_t gamepad;

  // Calibration settings
  settings_calibration_t calibration;

  // LED matrix data
  settings_led_t led; // LED matrix pixels and brightness

  // LED effect settings
  uint8_t led_effect_mode;  // Current effect mode
  uint8_t led_effect_speed; // Effect animation speed
  uint8_t led_effect_color_r;
  uint8_t led_effect_color_g;
  uint8_t led_effect_color_b;
  uint8_t led_fps_limit; // FPS limit for LED effects (0 = unlimited)
  uint8_t led_effect_params[LED_EFFECT_MAX][LED_EFFECT_PARAM_COUNT];
  settings_rotary_encoder_t rotary;

  // ADC EMA Filter settings
  uint8_t filter_enabled;    // Enable/disable ADC EMA filtering
  uint8_t filter_noise_band; // Noise band in ADC counts (default 30)
  uint8_t filter_alpha_min;  // Alpha min denominator (1/N, default 32)
  uint8_t filter_alpha_max;  // Alpha max denominator (1/N, default 4)
  uint8_t advanced_tick_rate; // Delay in scan ticks between advanced actions
  char keyboard_name[SETTINGS_KEYBOARD_NAME_LENGTH];

  // Footer
  uint32_t magic_end; // Magic number to validate end
  uint32_t crc32;     // CRC32 checksum of settings
} settings_t;

//--------------------------------------------------------------------+
// Default Settings
//--------------------------------------------------------------------+

// Set to 1 to force defaults on startup (ignores saved settings)
// Useful for development or recovering from bad settings
#ifndef SETTINGS_FORCE_DEFAULTS
#define SETTINGS_FORCE_DEFAULTS 0 // Set to 0 after initial deploy
#endif

#define SETTINGS_DEFAULT_OPTIONS                                               \
  {.keyboard_enabled = 0,                                                      \
   .gamepad_enabled = 1,                                                       \
   .raw_hid_echo = 0,                                                          \
   .led_enabled = 1,                                                           \
   .reserved = 0}

// Legacy default HID keycodes kept for compatibility with old bring-up code.
#define HID_KEY_Q_CODE 0x14
#define HID_KEY_W_CODE 0x1A
#define HID_KEY_E_CODE 0x08
#define HID_KEY_A_CODE 0x04
#define HID_KEY_S_CODE 0x16
#define HID_KEY_D_CODE 0x07

// Default curve: linear (control points at 1/3 and 2/3 of the line)
#define SETTINGS_DEFAULT_CURVE {.p1 = {85, 85}, .p2 = {170, 170}}

// Default gamepad mapping: no mapping
#define SETTINGS_DEFAULT_GAMEPAD_MAP                                           \
  {.axis = 0, .direction = 0, .button = 0, .reserved = 0}

// Default key settings template used for all keys. The runtime default keycode is
// derived from the physical keyboard layout in layout.c.
#define SETTINGS_DEFAULT_KEY_0                                                 \
  {.hid_keycode = HID_KEY_Q_CODE,                                              \
   .actuation_point_mm = 20,                                                   \
   .release_point_mm = 18,                                                     \
   .rapid_trigger_activation = 5,                                              \
   .rapid_trigger_press = 30,                                                  \
   .rapid_trigger_release = 30,                                                \
   .socd_pair = 255,                                                           \
   .rapid_trigger_enabled = 0,                                                 \
   .curve_enabled = 0,                                                         \
   .curve = SETTINGS_DEFAULT_CURVE,                                            \
   .gamepad_map = SETTINGS_DEFAULT_GAMEPAD_MAP}
#define SETTINGS_DEFAULT_KEY_1                                                 \
  {.hid_keycode = HID_KEY_W_CODE,                                              \
   .actuation_point_mm = 20,                                                   \
   .release_point_mm = 18,                                                     \
   .rapid_trigger_activation = 5,                                              \
   .rapid_trigger_press = 30,                                                  \
   .rapid_trigger_release = 30,                                                \
   .socd_pair = 255,                                                           \
   .rapid_trigger_enabled = 0,                                                 \
   .curve_enabled = 0,                                                         \
   .curve = SETTINGS_DEFAULT_CURVE,                                            \
   .gamepad_map = SETTINGS_DEFAULT_GAMEPAD_MAP}
#define SETTINGS_DEFAULT_KEY_2                                                 \
  {.hid_keycode = HID_KEY_E_CODE,                                              \
   .actuation_point_mm = 20,                                                   \
   .release_point_mm = 18,                                                     \
   .rapid_trigger_activation = 5,                                              \
   .rapid_trigger_press = 30,                                                  \
   .rapid_trigger_release = 30,                                                \
   .socd_pair = 255,                                                           \
   .rapid_trigger_enabled = 0,                                                 \
   .curve_enabled = 0,                                                         \
   .curve = SETTINGS_DEFAULT_CURVE,                                            \
   .gamepad_map = SETTINGS_DEFAULT_GAMEPAD_MAP}
#define SETTINGS_DEFAULT_KEY_3                                                 \
  {.hid_keycode = HID_KEY_A_CODE,                                              \
   .actuation_point_mm = 20,                                                   \
   .release_point_mm = 18,                                                     \
   .rapid_trigger_activation = 5,                                              \
   .rapid_trigger_press = 30,                                                  \
   .rapid_trigger_release = 30,                                                \
   .socd_pair = 5,                                                             \
   .rapid_trigger_enabled = 0,                                                 \
   .curve_enabled = 0,                                                         \
   .curve = SETTINGS_DEFAULT_CURVE,                                            \
   .gamepad_map = SETTINGS_DEFAULT_GAMEPAD_MAP}
#define SETTINGS_DEFAULT_KEY_4                                                 \
  {.hid_keycode = HID_KEY_S_CODE,                                              \
   .actuation_point_mm = 20,                                                   \
   .release_point_mm = 18,                                                     \
   .rapid_trigger_activation = 5,                                              \
   .rapid_trigger_press = 30,                                                  \
   .rapid_trigger_release = 30,                                                \
   .socd_pair = 255,                                                           \
   .rapid_trigger_enabled = 0,                                                 \
   .curve_enabled = 0,                                                         \
   .curve = SETTINGS_DEFAULT_CURVE,                                            \
   .gamepad_map = SETTINGS_DEFAULT_GAMEPAD_MAP}
#define SETTINGS_DEFAULT_KEY_5                                                 \
  {.hid_keycode = HID_KEY_D_CODE,                                              \
   .actuation_point_mm = 20,                                                   \
   .release_point_mm = 18,                                                     \
   .rapid_trigger_activation = 5,                                              \
   .rapid_trigger_press = 30,                                                  \
   .rapid_trigger_release = 30,                                                \
   .socd_pair = 3,                                                             \
   .rapid_trigger_enabled = 0,                                                 \
   .curve_enabled = 0,                                                         \
   .curve = SETTINGS_DEFAULT_CURVE,                                            \
   .gamepad_map = SETTINGS_DEFAULT_GAMEPAD_MAP}

#define SETTINGS_DEFAULT_GAMEPAD                                               \
  {.radial_deadzone = 0,                                                       \
   .keyboard_routing = GAMEPAD_KEYBOARD_ROUTING_ALL_KEYS,                      \
   .square_mode = 0,                                                           \
   .reactive_stick = 0,                                                        \
  .api_mode = GAMEPAD_API_HID,                                                \
   .curve = {{0u, 0u}, {133u, 85u}, {266u, 170u}, {GAMEPAD_CURVE_MAX_DISTANCE_01MM, 255u}}}

// Default calibration values (from offset.c)
#define SETTINGS_DEFAULT_CALIBRATION                                           \
  {.lut_zero_value = 2118}

//--------------------------------------------------------------------+
// Settings API
//--------------------------------------------------------------------+

/**
 * @brief Initialize settings module, load from flash or reset to defaults
 */
void settings_init(void);

/**
 * @brief Get current settings (read-only)
 * @return Pointer to current settings
 */
const settings_t *settings_get(void);

/**
 * @brief Check if keyboard HID output is enabled
 */
bool settings_is_keyboard_enabled(void);

/**
 * @brief Check if gamepad HID output is enabled
 */
bool settings_is_gamepad_enabled(void);

/**
 * @brief Enable/disable keyboard HID output
 * @param enabled true to enable, false to disable
 * @return true if save was successful
 */
bool settings_set_keyboard_enabled(bool enabled);

/**
 * @brief Enable/disable gamepad HID output
 * @param enabled true to enable, false to disable
 * @return true if save was successful
 */
bool settings_set_gamepad_enabled(bool enabled);

/**
 * @brief Enable/disable gamepad HID output without saving to flash.
 * Updates the in-RAM settings state so subsequent reads stay coherent.
 * @param enabled true to enable, false to disable
 * @return true if successful
 */
bool settings_set_gamepad_enabled_live(bool enabled);

/**
 * @brief Check if NKRO mode is enabled
 */
bool settings_is_nkro_enabled(void);

/**
 * @brief Enable/disable NKRO mode
 * @param enabled true for NKRO, false for 6KRO
 * @return true if save was successful
 */
bool settings_set_nkro_enabled(bool enabled);

/**
 * @brief Set all options at once
 * @param options New options
 * @return true if save was successful
 */
bool settings_set_options(settings_options_t options);

/**
 * @brief Get options
 * @return Current options
 */
settings_options_t settings_get_options(void);

/**
 * @brief Reset all settings to defaults and save
 * @return true if reset was successful
 */
bool settings_reset(void);

/**
 * @brief Save current settings to flash
 * @return true if save was successful
 */
bool settings_save(void);

/**
 * @brief Service deferred autosave work.
 * Call periodically from the main loop.
 * @param now_ms Current HAL tick in milliseconds
 */
void settings_task(uint32_t now_ms);

/**
 * @brief Check if there are unsaved settings changes pending autosave.
 * @return true when a deferred save is pending
 */
bool settings_has_unsaved_changes(void);

/**
 * @brief Get firmware version
 * @return Firmware version number
 */
uint16_t settings_get_firmware_version(void);

/**
 * @brief Get the persistent keyboard name (NUL-terminated RAM cache).
 * @return Pointer to a read-only C string.
 */
const char *settings_get_keyboard_name(void);

/**
 * @brief Set the persistent keyboard name.
 *
 * Input bytes are sanitized to printable ASCII and persisted through settings.
 * The value is also cached in RAM for fast reads.
 *
 * @param name Input bytes (not required to be NUL-terminated)
 * @param length Number of bytes available in @p name
 * @return true if accepted
 */
bool settings_set_keyboard_name(const char *name, uint8_t length);

/**
 * @brief Get advanced action tick rate.
 * @return Tick rate in scan ticks
 */
uint8_t settings_get_advanced_tick_rate(void);

/**
 * @brief Set advanced action tick rate.
 * @param tick_rate Tick rate in scan ticks
 * @return true if successful
 */
bool settings_set_advanced_tick_rate(uint8_t tick_rate);

//--------------------------------------------------------------------+
// LED Matrix Settings API
//--------------------------------------------------------------------+

/**
 * @brief Check if LED matrix is enabled
 */
bool settings_is_led_enabled(void);

/**
 * @brief Enable/disable LED matrix
 * @param enabled true to enable, false to disable
 * @return true if successful
 */
bool settings_set_led_enabled(bool enabled);

/**
 * @brief Get LED brightness
 * @return Current brightness value (0-255)
 */
uint8_t settings_get_led_brightness(void);

/**
 * @brief Set LED brightness
 * @param brightness New brightness value (0-255)
 * @return true if successful
 */
bool settings_set_led_brightness(uint8_t brightness);

/**
 * @brief Get LED pixel data
 * @return Pointer to pixel data (192 bytes)
 */
const uint8_t *settings_get_led_pixels(void);

/**
 * @brief Set LED pixel data
 * @param pixels Pixel data (192 bytes)
 * @return true if successful
 */
bool settings_set_led_pixels(const uint8_t *pixels);

/**
 * @brief Set a single LED pixel
 * @param index LED index (0-63)
 * @param r Red value
 * @param g Green value
 * @param b Blue value
 * @return true if successful
 */
bool settings_set_led_pixel(uint8_t index, uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief Get complete LED settings
 * @return Pointer to LED settings
 */
const settings_led_t *settings_get_led(void);

/**
 * @brief Get LED effect mode
 * @return Current effect mode
 */
uint8_t settings_get_led_effect_mode(void);

/**
 * @brief Set LED effect mode
 * @param mode Effect mode
 * @return true if successful
 */
bool settings_set_led_effect_mode(uint8_t mode);

/**
 * @brief Get LED effect speed
 * @return Current effect speed
 */
uint8_t settings_get_led_effect_speed(void);

/**
 * @brief Set LED effect speed
 * @param speed Effect speed
 * @return true if successful
 */
bool settings_set_led_effect_speed(uint8_t speed);

/**
 * @brief Get LED FPS limit
 * @return Current FPS limit (0 = unlimited)
 */
uint8_t settings_get_led_fps_limit(void);

/**
 * @brief Set LED FPS limit
 * @param fps_limit FPS limit (0 = unlimited)
 * @return true if successful
 */
bool settings_set_led_fps_limit(uint8_t fps_limit);

/**
 * @brief Get LED effect color
 * @param r Pointer to store red value
 * @param g Pointer to store green value
 * @param b Pointer to store blue value
 */
void settings_get_led_effect_color(uint8_t *r, uint8_t *g, uint8_t *b);

/**
 * @brief Set LED effect color
 * @param r Red value
 * @param g Green value
 * @param b Blue value
 * @return true if successful
 */
bool settings_set_led_effect_color(uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief Get persisted parameters for one LED effect.
 * @param effect_mode Effect mode id
 * @param params Output array of LED_EFFECT_PARAM_COUNT bytes
 */
void settings_get_led_effect_params(uint8_t effect_mode, uint8_t *params);

/**
 * @brief Set persisted parameters for one LED effect.
 * Applies them immediately if the effect is currently active.
 * @param effect_mode Effect mode id
 * @param params Input array of LED_EFFECT_PARAM_COUNT bytes
 * @return true if successful
 */
bool settings_set_led_effect_params(uint8_t effect_mode, const uint8_t *params);

/**
 * @brief Restore the non-third-party LED effect that was active before a live
 * third-party override took over.
 * @return true if a previous effect was available and restored
 */
bool settings_restore_led_effect_before_third_party(void);

//--------------------------------------------------------------------+
// ADC Filter Settings API
//--------------------------------------------------------------------+

/**
 * @brief Check if ADC EMA filtering is enabled
 */
bool settings_is_filter_enabled(void);

/**
 * @brief Enable/disable ADC EMA filtering live
 * @param enabled true to enable filtering, false for raw passthrough
 * @return true if successful
 */
bool settings_set_filter_enabled(bool enabled);

/**
 * @brief Get current ADC EMA parameters
 */
void settings_get_filter_params(uint8_t *noise_band, uint8_t *alpha_min_denom,
                                uint8_t *alpha_max_denom);

/**
 * @brief Set current ADC EMA parameters live
 */
bool settings_set_filter_params(uint8_t noise_band, uint8_t alpha_min_denom,
                                uint8_t alpha_max_denom);

//--------------------------------------------------------------------+
// Key Settings API
//--------------------------------------------------------------------+

/**
 * @brief Get key settings for a specific key
 * @param key_index Key index (0-5)
 * @return Pointer to key settings or NULL if invalid index
 */
const settings_key_t *settings_get_key(uint8_t key_index);

/**
 * @brief Set key settings for a specific key
 * @param key_index Key index (0-5)
 * @param key Key settings to apply
 * @return true if successful
 */
bool settings_set_key(uint8_t key_index, const settings_key_t *key);

/**
 * @brief Get the keycode assigned to one logical layer/key slot.
 * Layer 0 is the base layer stored in settings_key_t.hid_keycode.
 * Layers 1..N are additional overlay layers.
 * @param layer_index Logical layer index
 * @param key_index Key index
 * @return Assigned keycode or KC_NO on invalid input
 */
uint16_t settings_get_layer_keycode(uint8_t layer_index, uint8_t key_index);

/**
 * @brief Set the keycode assigned to one logical layer/key slot.
 * Layer 0 updates the base settings_key_t.hid_keycode.
 * Layers 1..N update the persisted overlay keymaps.
 * @param layer_index Logical layer index
 * @param key_index Key index
 * @param keycode HID/custom keycode to assign
 * @return true if successful
 */
bool settings_set_layer_keycode(uint8_t layer_index, uint8_t key_index,
                                uint16_t keycode);

//--------------------------------------------------------------------+
// Gamepad Settings API
//--------------------------------------------------------------------+

/**
 * @brief Get gamepad settings
 * @return Pointer to gamepad settings
 */
const settings_gamepad_t *settings_get_gamepad(void);

/**
 * @brief Set gamepad settings
 * @param gamepad Gamepad settings to apply
 * @return true if successful
 */
bool settings_set_gamepad(const settings_gamepad_t *gamepad);

/**
 * @brief Get selected gamepad USB API mode.
 * @return Current gamepad API mode
 */
gamepad_api_mode_t settings_get_gamepad_api_mode(void);

/**
 * @brief Set gamepad USB API mode.
 * @param mode gamepad_api_mode_t
 * @return true if successful
 */
bool settings_set_gamepad_api_mode(gamepad_api_mode_t mode);

/**
 * @brief Apply the global gamepad analog curve to a distance value.
 * @param distance_01mm Distance in 0.01 mm
 * @return Curved output in the 0..255 range
 */
uint8_t settings_gamepad_apply_curve(uint16_t distance_01mm);

/**
 * @brief Check whether a key has any active gamepad mapping.
 * @param key_index Key index
 * @return true if the key contributes to a gamepad axis or button
 */
bool settings_is_key_mapped_to_gamepad(uint8_t key_index);

//--------------------------------------------------------------------+
// Rotary Encoder Settings API
//--------------------------------------------------------------------+

/**
 * @brief Read the current rotary encoder configuration.
 * @param rotary Output structure
 */
void settings_get_rotary_encoder(settings_rotary_encoder_t *rotary);

/**
 * @brief Update the rotary encoder configuration live.
 * @param rotary Input structure
 * @return true if successful
 */
bool settings_set_rotary_encoder(const settings_rotary_encoder_t *rotary);

//--------------------------------------------------------------------+
// Calibration Settings API
//--------------------------------------------------------------------+

/**
 * @brief Get calibration settings
 * @return Pointer to calibration settings
 */
const settings_calibration_t *settings_get_calibration(void);

/**
 * @brief Set calibration settings
 * @param calibration Calibration settings to apply
 * @return true if successful
 */
bool settings_set_calibration(const settings_calibration_t *calibration);

/**
 * @brief Set calibration for a single key
 * @param key_index Key index (0-5)
 * @param zero_value ADC zero value for this key
 * @return true if successful
 */
bool settings_set_key_calibration(uint8_t key_index, int16_t zero_value);

/**
 * @brief Set max calibration for a single key
 * @param key_index Key index
 * @param max_value Raw ADC value captured at maximum press
 * @return true if successful
 */
bool settings_set_key_calibration_max(uint8_t key_index, int16_t max_value);

//--------------------------------------------------------------------+
// Per-Key Curve Settings API
//--------------------------------------------------------------------+

/**
 * @brief Get curve settings for a specific key
 * @param key_index Key index (0-5)
 * @return Pointer to curve settings or NULL if invalid index
 */
const settings_curve_t *settings_get_key_curve(uint8_t key_index);

/**
 * @brief Set curve settings for a specific key
 * @param key_index Key index (0-5)
 * @param curve Curve settings to apply
 * @return true if successful
 */
bool settings_set_key_curve(uint8_t key_index, const settings_curve_t *curve);

/**
 * @brief Enable/disable curve for a specific key
 * @param key_index Key index (0-5)
 * @param enabled true to enable curve, false to use linear
 * @return true if successful
 */
bool settings_set_key_curve_enabled(uint8_t key_index, bool enabled);

/**
 * @brief Check if curve is enabled for a specific key
 * @param key_index Key index (0-5)
 * @return true if curve is enabled
 */
bool settings_is_key_curve_enabled(uint8_t key_index);

/**
 * @brief Apply bezier curve to a normalized input (0-255)
 * @param key_index Key index (0-5)
 * @param input Input value (0-255)
 * @return Curved output value (0-255)
 */
uint8_t settings_apply_curve(uint8_t key_index, uint8_t input);

//--------------------------------------------------------------------+
// Per-Key Gamepad Mapping API
//--------------------------------------------------------------------+

/**
 * @brief Get gamepad mapping for a specific key
 * @param key_index Key index (0-5)
 * @return Pointer to gamepad mapping or NULL if invalid index
 */
const settings_gamepad_mapping_t *
settings_get_key_gamepad_mapping(uint8_t key_index);

/**
 * @brief Set gamepad mapping for a specific key
 * @param key_index Key index (0-5)
 * @param mapping Gamepad mapping to apply
 * @return true if successful
 */
bool settings_set_key_gamepad_mapping(
    uint8_t key_index, const settings_gamepad_mapping_t *mapping);

/**
 * @brief Check if gamepad+keyboard mode is enabled
 * @return true if keyboard should be sent along with gamepad
 */
bool settings_is_gamepad_with_keyboard(void);

/**
 * @brief Enable/disable gamepad+keyboard mode
 * @param enabled true to send keyboard along with gamepad
 * @return true if successful
 */
bool settings_set_gamepad_with_keyboard(bool enabled);

#ifdef __cplusplus
}
#endif

#endif /* SETTINGS_H_ */
