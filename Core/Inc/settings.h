/*
 * settings.h
 * Keyboard settings storage with EEPROM emulation
 */

#ifndef SETTINGS_H_
#define SETTINGS_H_

#include "board_config.h"
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
#define SETTINGS_VERSION 0x0007         // Bumped for 82-LED settings layout

//--------------------------------------------------------------------+
// LED Matrix Constants
//--------------------------------------------------------------------+
#define LED_MATRIX_SIZE NUM_KEYS
#define LED_MATRIX_DATA_BYTES (LED_MATRIX_SIZE * 3)

//--------------------------------------------------------------------+
// Analog Curve Constants
//--------------------------------------------------------------------+
#define ANALOG_CURVE_POINTS 4 // 4-point bezier curve (P0, P1, P2, P3)

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

/**
 * @brief Key-specific settings
 */
typedef struct __attribute__((packed)) {
  uint8_t hid_keycode;        // HID keycode for this key
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
} settings_key_t;

/**
 * @brief Calibration settings for ADC offset correction
 */
typedef struct __attribute__((packed)) {
  int16_t lut_zero_value;     // LUT reference zero value (default ~2118)
  int16_t key_zero_values[6]; // Per-key zero values
} settings_calibration_t;

/**
 * @brief Gamepad settings
 */
typedef struct __attribute__((packed)) {
  uint8_t deadzone;    // Analog deadzone (0-255)
  uint8_t curve_type;  // Analog curve (0=linear, 1=smooth, 2=aggressive)
  uint8_t square_mode; // Use square joystick mapping
  uint8_t snappy_mode; // Faster return to neutral
  uint8_t reserved[4];
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

  // Per-key settings (6 keys)
  settings_key_t keys[6];

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
  uint8_t led_reserved[2];

  // ADC EMA Filter settings
  uint8_t filter_enabled;    // Enable/disable ADC EMA filtering
  uint8_t filter_noise_band; // Noise band in ADC counts (default 30)
  uint8_t filter_alpha_min;  // Alpha min denominator (1/N, default 32)
  uint8_t filter_alpha_max;  // Alpha max denominator (1/N, default 4)

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

// Default HID keycodes for keys 0-5 (Q, W, E, A, S, D)
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

// Default key settings with keycodes and SOCD pairs (A↔D = keys 3↔5)
// Rapid trigger disabled by default, with 2.0mm actuation point
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
  {.deadzone = 10, .curve_type = 0, .square_mode = 0, .snappy_mode = 0}

// Default calibration values (from offset.c)
#define SETTINGS_DEFAULT_CALIBRATION                                           \
  {.lut_zero_value = 2118,                                                     \
   .key_zero_values = {2100, 2110, 2118, 2130, 2114, 2094}}

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
 * @brief Get firmware version
 * @return Firmware version number
 */
uint16_t settings_get_firmware_version(void);

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
