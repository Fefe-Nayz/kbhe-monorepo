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
#define SETTINGS_VERSION 0x001D         // Phase-4B: LED thermal protection option

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
#define SETTINGS_PROFILE_COUNT 4u
#define SETTINGS_PROFILE_NAME_LENGTH 16u
#define SETTINGS_DEFAULT_PROFILE_NONE 0xFFu // Sentinel: no fixed boot profile (use last active)
#define SETTINGS_DYNAMIC_ZONE_COUNT 4u
#define SETTINGS_ADVANCED_TICK_RATE_MIN 1u
#define SETTINGS_ADVANCED_TICK_RATE_MAX 100u
#define SETTINGS_DEFAULT_ADVANCED_TICK_RATE 1u
#define SETTINGS_DKS_BOTTOM_OUT_POINT_MIN_TENTHS 1u
#define SETTINGS_DKS_BOTTOM_OUT_POINT_MAX_TENTHS 40u
#define SETTINGS_DKS_BOTTOM_OUT_POINT_DEFAULT_TENTHS 40u
#define SETTINGS_SOCD_FULLY_PRESSED_POINT_MIN_TENTHS 1u
#define SETTINGS_SOCD_FULLY_PRESSED_POINT_MAX_TENTHS 40u
#define SETTINGS_SOCD_FULLY_PRESSED_POINT_DEFAULT_TENTHS 40u
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

typedef enum {
  ROTARY_BINDING_MODE_INTERNAL = 0,
  ROTARY_BINDING_MODE_KEYCODE = 1,
  ROTARY_BINDING_MODE_MAX
} rotary_binding_mode_t;

typedef enum {
  ROTARY_BINDING_LAYER_ACTIVE = 0,
  ROTARY_BINDING_LAYER_FIXED = 1,
  ROTARY_BINDING_LAYER_MAX
} rotary_binding_layer_mode_t;

typedef struct __attribute__((packed)) {
  uint8_t mode; // rotary_binding_mode_t
  uint16_t keycode;
  uint8_t modifier_mask_exact;
  uint16_t fallback_no_mod_keycode;
  uint8_t layer_mode;  // rotary_binding_layer_mode_t
  uint8_t layer_index; // Valid only when layer_mode=fixed
} settings_rotary_binding_t;

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
  settings_rotary_binding_t cw_binding;
  settings_rotary_binding_t ccw_binding;
  settings_rotary_binding_t click_binding;
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
  uint8_t nkro_enabled : 1;          // Auto mode: try NKRO, fallback to 6KRO
  uint8_t led_thermal_protection_enabled : 1; // Clamp brightness above temp threshold
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
  KEY_BEHAVIOR_DYNAMIC = 3, // Dynamic Keystroke (DKS)
  KEY_BEHAVIOR_MAX
} key_behavior_mode_t;

typedef struct __attribute__((packed)) {
  // DKS action bitmap:
  // bit 0-1: key press
  // bit 2-3: key fully pressed
  // bit 4-5: key release from fully pressed
  // bit 6-7: key release
  uint8_t end_mm_tenths;
  uint16_t hid_keycode; // Binding keycode for this DKS slot
} settings_dynamic_zone_t;

#define SETTINGS_KEY_ADV_TAP_HOLD_HOLD_ON_OTHER_MASK 0x01u
#define SETTINGS_KEY_ADV_TAP_HOLD_UPPERCASE_HOLD_MASK 0x02u

typedef struct __attribute__((packed)) {
  uint8_t behavior_mode;            // key_behavior_mode_t
  uint8_t hold_threshold_10ms;      // Tap-hold / toggle hold threshold
  uint8_t dks_bottom_out_point_tenths; // DKS bottom-out point in 0.1 mm
  uint8_t reserved;                 // Tap-hold option flags
  uint16_t secondary_hid_keycode;   // Hold / alternate action
  settings_dynamic_zone_t dynamic_zones[SETTINGS_DYNAMIC_ZONE_COUNT];
  uint8_t socd_fully_pressed_point_tenths;
} settings_key_advanced_t;

typedef enum {
  SETTINGS_SOCD_RESOLUTION_LAST_INPUT_WINS = 0,
  SETTINGS_SOCD_RESOLUTION_MOST_PRESSED_WINS = 1,
  SETTINGS_SOCD_RESOLUTION_ABSOLUTE_PRIORITY_KEY1 = 2,
  SETTINGS_SOCD_RESOLUTION_ABSOLUTE_PRIORITY_KEY2 = 3,
  SETTINGS_SOCD_RESOLUTION_NEUTRAL = 4,
  SETTINGS_SOCD_RESOLUTION_MAX
} settings_socd_resolution_t;

#define SETTINGS_SOCD_PAIR_NONE 0xFFu
#define SETTINGS_KEY_SOCD_RESOLUTION_MASK 0x07u
#define SETTINGS_KEY_CONTINUOUS_RAPID_TRIGGER_MASK 0x08u
#define SETTINGS_KEY_SOCD_FULLY_PRESSED_ENABLE_MASK 0x10u

/**
 * @brief Key-specific settings
 */
typedef struct __attribute__((packed)) {
  uint16_t hid_keycode;       // HID keycode / custom keycode for this key
  uint8_t actuation_point_mm; // Actuation point in 0.1mm (e.g., 20 = 2.0mm)
  uint8_t release_point_mm;   // Release point in 0.1mm
  uint8_t
      rapid_trigger_press; // Press sensitivity in 0.01mm (e.g., 30 = 0.30mm)
  uint8_t rapid_trigger_release;     // Release sensitivity in 0.01mm
  uint8_t socd_pair;                 // SOCD paired key index (255 = none)
  uint8_t rapid_trigger_enabled : 1; // Enable rapid trigger for this key
  uint8_t
      disable_kb_on_gamepad : 1; // Disable keyboard output when gamepad active
  uint8_t curve_enabled : 1;     // Enable custom analog curve
  uint8_t reserved_bits : 5;     // SOCD mode + continuous RT + SOCD fully pressed
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

static inline bool
settings_key_is_socd_fully_pressed_enabled(const settings_key_t *key) {
  if (key == 0) {
    return false;
  }

  return (key->reserved_bits & SETTINGS_KEY_SOCD_FULLY_PRESSED_ENABLE_MASK) != 0u;
}

static inline void settings_key_set_socd_fully_pressed_enabled(
    settings_key_t *key, bool enabled) {
  if (key == 0) {
    return;
  }

  if (enabled) {
    key->reserved_bits |= SETTINGS_KEY_SOCD_FULLY_PRESSED_ENABLE_MASK;
  } else {
    key->reserved_bits =
        (uint8_t)(key->reserved_bits &
                  (uint8_t)(~SETTINGS_KEY_SOCD_FULLY_PRESSED_ENABLE_MASK));
  }
}

static inline bool settings_key_is_tap_hold_hold_on_other_key_press(
    const settings_key_t *key) {
  if (key == 0) {
    return false;
  }

  return (key->advanced.reserved &
          SETTINGS_KEY_ADV_TAP_HOLD_HOLD_ON_OTHER_MASK) != 0u;
}

static inline void settings_key_set_tap_hold_hold_on_other_key_press(
    settings_key_t *key, bool enabled) {
  if (key == 0) {
    return;
  }

  if (enabled) {
    key->advanced.reserved |= SETTINGS_KEY_ADV_TAP_HOLD_HOLD_ON_OTHER_MASK;
  } else {
    key->advanced.reserved =
        (uint8_t)(key->advanced.reserved &
                  (uint8_t)(~SETTINGS_KEY_ADV_TAP_HOLD_HOLD_ON_OTHER_MASK));
  }
}

static inline bool
settings_key_is_tap_hold_uppercase_hold(const settings_key_t *key) {
  if (key == 0) {
    return false;
  }

  return (key->advanced.reserved &
          SETTINGS_KEY_ADV_TAP_HOLD_UPPERCASE_HOLD_MASK) != 0u;
}

static inline void settings_key_set_tap_hold_uppercase_hold(
    settings_key_t *key, bool enabled) {
  if (key == 0) {
    return;
  }

  if (enabled) {
    key->advanced.reserved |= SETTINGS_KEY_ADV_TAP_HOLD_UPPERCASE_HOLD_MASK;
  } else {
    key->advanced.reserved =
        (uint8_t)(key->advanced.reserved &
                  (uint8_t)(~SETTINGS_KEY_ADV_TAP_HOLD_UPPERCASE_HOLD_MASK));
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
 * @brief Full per-profile persistent configuration snapshot.
 */
typedef struct __attribute__((packed)) {
  settings_key_t keys[NUM_KEYS];
  uint16_t layer_keycodes[SETTINGS_LAYER_COUNT - 1u][NUM_KEYS];
  settings_key_advanced_t advanced_by_layer[SETTINGS_LAYER_COUNT][NUM_KEYS];
  settings_gamepad_t gamepad;
  settings_led_t led;
  uint8_t led_effect_mode;
  uint8_t led_effect_speed;
  uint8_t led_effect_color_r;
  uint8_t led_effect_color_g;
  uint8_t led_effect_color_b;
  uint8_t led_fps_limit;
  uint8_t led_effect_params[LED_EFFECT_MAX][LED_EFFECT_PARAM_COUNT];
  settings_rotary_encoder_t rotary;
  uint8_t filter_enabled;
  uint8_t filter_noise_band;
  uint8_t filter_alpha_min;
  uint8_t filter_alpha_max;
  uint8_t advanced_tick_rate;
} settings_profile_t;

/**
 * @brief Complete settings structure stored in flash
 */
typedef struct __attribute__((packed)) {
  // Header
  uint32_t magic_start;          // Magic number to identify valid settings
  uint16_t version;              // Settings version for migration
  uint8_t default_profile_index; // Profile applied on boot; SETTINGS_DEFAULT_PROFILE_NONE = last active
  uint8_t reserved_pad;          // Padding (was upper byte of uint16_t reserved)

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
  uint8_t led_effect_speed; // Deprecated mirror of active effect speed
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

  // Global non-profile metadata
  char keyboard_name[SETTINGS_KEYBOARD_NAME_LENGTH];
  uint8_t active_profile_index;
  uint8_t profile_used_mask;
  char profile_names[SETTINGS_PROFILE_COUNT][SETTINGS_PROFILE_NAME_LENGTH];

  // Persistent snapshots for all profile slots
  settings_profile_t profiles[SETTINGS_PROFILE_COUNT];

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
  {.keyboard_enabled = 1,                                                      \
   .gamepad_enabled = 0,                                                       \
   .raw_hid_echo = 0,                                                          \
   .led_enabled = 1,                                                           \
  .nkro_enabled = 1,                                                          \
  .led_thermal_protection_enabled = 1,                                        \
   .reserved = 0}

// Default curve: linear (control points at 1/3 and 2/3 of the line)
#define SETTINGS_DEFAULT_CURVE {.p1 = {85, 85}, .p2 = {170, 170}}

// Default gamepad mapping: no mapping
#define SETTINGS_DEFAULT_GAMEPAD_MAP                                           \
  {.axis = 0, .direction = 0, .button = 0, .reserved = 0}

#define SETTINGS_DEFAULT_GAMEPAD                                               \
  {.keyboard_routing = GAMEPAD_KEYBOARD_ROUTING_UNMAPPED_ONLY,                 \
   .square_mode = 0,                                                           \
   .reactive_stick = 0,                                                        \
   .api_mode = GAMEPAD_API_XINPUT,                                             \
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
 * @brief Check if LED thermal protection is enabled.
 */
bool settings_is_led_thermal_protection_enabled(void);

/**
 * @brief Enable/disable LED thermal protection.
 * @param enabled true to enforce thermal brightness cap
 * @return true if save was successful
 */
bool settings_set_led_thermal_protection_enabled(bool enabled);

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
 * @brief Request a deferred settings save from the main loop.
 *
 * This keeps RAW HID command handlers non-blocking by avoiding direct flash
 * writes in command context.
 *
 * @return true if the request was accepted
 */
bool settings_request_save(void);

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

/**
 * @brief Get currently active persistent profile slot.
 * @return Profile index (0..SETTINGS_PROFILE_COUNT-1)
 */
uint8_t settings_get_active_profile_index(void);

/**
 * @brief Get the default boot profile index.
 * @return Profile index, or SETTINGS_DEFAULT_PROFILE_NONE if none is set.
 */
uint8_t settings_get_default_profile_index(void);

/**
 * @brief Set the default boot profile index.
 * @param profile_index Profile index (0..SETTINGS_PROFILE_COUNT-1),
 *                      or SETTINGS_DEFAULT_PROFILE_NONE to clear.
 * @return true if the value was accepted and stored.
 */
bool settings_set_default_profile_index(uint8_t profile_index);

/**
 * @brief Whether the keyboard is currently in RAM-only mode.
 *
 * In RAM-only mode every settings write goes to RAM only; calls to
 * settings_save() are silently suppressed.  The mode is cleared on reboot
 * or by calling settings_exit_ram_only_mode().
 */
bool settings_is_ram_only_mode(void);

/**
 * @brief Enter RAM-only mode (suppresses all flash saves).
 */
void settings_enter_ram_only_mode(void);

/**
 * @brief Exit RAM-only mode and reload the last persisted settings from flash.
 *
 * After this call the in-RAM state reflects what was last saved to flash,
 * discarding any RAM-only changes.  Returns false if the flash reload fails
 * (in which case RAM-only mode is still cleared and defaults are applied).
 */
bool settings_exit_ram_only_mode(void);

/**
 * @brief Set currently active persistent profile slot.
 * @param profile_index Profile index (0..SETTINGS_PROFILE_COUNT-1)
 * @return true if successful
 */
bool settings_set_active_profile_index(uint8_t profile_index);

/**
 * @brief Return bitmask of profile slots currently used/persisted on MCU.
 * bit N corresponds to profile slot N.
 */
uint8_t settings_get_profile_used_mask(void);

/**
 * @brief Check whether a profile slot is currently used.
 */
bool settings_is_profile_slot_used(uint8_t profile_index);

/**
 * @brief Create a profile in the first free slot.
 *
 * @param name Optional profile name bytes (ASCII sanitized). Can be NULL.
 * @param length Input length in bytes.
 * @return Created slot index [0..SETTINGS_PROFILE_COUNT-1], or -1 on failure.
 */
int8_t settings_create_profile(const char *name, uint8_t length);

/**
 * @brief Delete a profile slot.
 *
 * Cannot delete the last remaining used slot.
 *
 * @param profile_index Slot index to delete.
 * @return true if deleted.
 */
bool settings_delete_profile(uint8_t profile_index);

/**
 * @brief Copy one profile slot into another slot.
 *
 * If target slot is unused, it becomes used and receives a default slot name.
 *
 * @param source_profile_index Source slot index.
 * @param target_profile_index Target slot index.
 * @return true if copied.
 */
bool settings_copy_profile_slot(uint8_t source_profile_index,
                                uint8_t target_profile_index);

/**
 * @brief Reset one used profile slot to factory defaults.
 *
 * The slot name and used-mask membership are preserved.
 *
 * @param profile_index Slot index to reset.
 * @return true if reset.
 */
bool settings_reset_profile_slot(uint8_t profile_index);

/**
 * @brief Get profile name for one slot.
 * @param profile_index Profile index (0..SETTINGS_PROFILE_COUNT-1)
 * @return Pointer to NUL-terminated profile name, or NULL on invalid index
 */
const char *settings_get_profile_name(uint8_t profile_index);

/**
 * @brief Set profile name for one slot.
 *
 * Input bytes are sanitized to printable ASCII.
 *
 * @param profile_index Profile index (0..SETTINGS_PROFILE_COUNT-1)
 * @param name Input bytes (not required to be NUL-terminated)
 * @param length Number of input bytes
 * @return true if accepted
 */
bool settings_set_profile_name(uint8_t profile_index, const char *name,
                               uint8_t length);

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
 * @brief Get speed for the currently selected LED effect
 * @return Current effect speed parameter
 */
uint8_t settings_get_led_effect_speed(void);

/**
 * @brief Set speed for the currently selected LED effect
 * @param speed Effect speed parameter
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
 * @brief Get active effect color (compatibility wrapper).
 *
 * Color values come from the active effect parameter block
 * (`LED_EFFECT_PARAM_COLOR_*`).
 * @param r Pointer to store red value
 * @param g Pointer to store green value
 * @param b Pointer to store blue value
 */
void settings_get_led_effect_color(uint8_t *r, uint8_t *g, uint8_t *b);

/**
 * @brief Set active effect color (compatibility wrapper).
 *
 * Color values are written into the active effect parameter block
 * (`LED_EFFECT_PARAM_COLOR_*`).
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
 * @brief Get effective key settings for the active profile and one layer/key.
 *
 * Layer-dependent advanced behavior is resolved from profile storage while
 * retaining per-key trigger parameters from layer 0.
 *
 * @param key_index Physical key index
 * @param layer_index Logical layer index
 * @param key Output settings structure
 * @return true if successful
 */
bool settings_get_key_for_layer(uint8_t key_index, uint8_t layer_index,
                                settings_key_t *key);

/**
 * @brief Set effective key settings for the active profile and one layer/key.
 *
 * For layer 0 this updates the full per-key trigger settings. For layers 1..N,
 * only layer keycode + layer-dependent advanced behavior are updated.
 *
 * @param key_index Physical key index
 * @param layer_index Logical layer index
 * @param key Input settings
 * @return true if successful
 */
bool settings_set_key_for_layer(uint8_t key_index, uint8_t layer_index,
                                const settings_key_t *key);

/**
 * @brief Get effective key settings for a specific profile and layer/key.
 *
 * @param profile_index Profile slot index
 * @param layer_index Logical layer index
 * @param key_index Physical key index
 * @param key Output settings structure
 * @return true if successful
 */
bool settings_get_profile_layer_key_settings(uint8_t profile_index,
                                             uint8_t layer_index,
                                             uint8_t key_index,
                                             settings_key_t *key);

/**
 * @brief Set effective key settings for a specific profile and layer/key.
 *
 * @param profile_index Profile slot index
 * @param layer_index Logical layer index
 * @param key_index Physical key index
 * @param key Input settings structure
 * @return true if successful
 */
bool settings_set_profile_layer_key_settings(uint8_t profile_index,
                                             uint8_t layer_index,
                                             uint8_t key_index,
                                             const settings_key_t *key);

/**
 * @brief Set key settings for a specific key
 * @param key_index Key index (0-5)
 * @param key Key settings to apply
 * @return true if successful
 */
bool settings_set_key(uint8_t key_index, const settings_key_t *key);

/**
 * @brief Reset per-key actuation and rapid-trigger settings to firmware defaults.
 * This keeps non-trigger fields (keycode, SOCD, mapping, advanced behavior, etc.) unchanged.
 * @param key_index Key index
 * @return true if successful
 */
bool settings_reset_key_trigger_settings(uint8_t key_index);

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

#ifdef __cplusplus
}
#endif

#endif /* SETTINGS_H_ */
