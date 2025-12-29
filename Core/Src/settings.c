/*
 * settings.c
 * Keyboard settings storage with EEPROM emulation
 */

#include "settings.h"
#include "flash_storage.h"
#include "led_matrix.h"
#include "usb_gamepad.h"
#include <string.h>

//--------------------------------------------------------------------+
// Firmware Version
//--------------------------------------------------------------------+
#define FIRMWARE_VERSION 0x0102 // v1.2.0 (LED matrix support)

//--------------------------------------------------------------------+
// Internal Variables
//--------------------------------------------------------------------+

// RAM cache of current settings
static settings_t current_settings;

// Flag indicating if settings have been modified
static bool settings_dirty = false;

//--------------------------------------------------------------------+
// CRC32 Implementation (Simple polynomial)
//--------------------------------------------------------------------+
static uint32_t crc32_compute(const void *data, uint32_t len) {
  const uint8_t *buf = (const uint8_t *)data;
  uint32_t crc = 0xFFFFFFFF;

  while (len--) {
    crc ^= *buf++;
    for (int i = 0; i < 8; i++) {
      crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
    }
  }

  return ~crc;
}

//--------------------------------------------------------------------+
// Internal Functions
//--------------------------------------------------------------------+

static void settings_set_defaults(void) {
  memset(&current_settings, 0, sizeof(settings_t));

  // Header
  current_settings.magic_start = SETTINGS_MAGIC_START;
  current_settings.version = SETTINGS_VERSION;

  // Default options
  current_settings.options.keyboard_enabled = 0;
  current_settings.options.gamepad_enabled = 1;
  current_settings.options.raw_hid_echo = 0;
  current_settings.options.led_enabled = 1;

  // Default per-key settings with keycodes and SOCD pairs
  const settings_key_t default_keys[6] = {
      SETTINGS_DEFAULT_KEY_0, SETTINGS_DEFAULT_KEY_1, SETTINGS_DEFAULT_KEY_2,
      SETTINGS_DEFAULT_KEY_3, SETTINGS_DEFAULT_KEY_4, SETTINGS_DEFAULT_KEY_5};
  memcpy(current_settings.keys, default_keys, sizeof(default_keys));

  // Default gamepad settings
  settings_gamepad_t default_gamepad = SETTINGS_DEFAULT_GAMEPAD;
  current_settings.gamepad = default_gamepad;

  // Default calibration settings
  settings_calibration_t default_calibration = SETTINGS_DEFAULT_CALIBRATION;
  current_settings.calibration = default_calibration;

  // Default LED settings (all LEDs off, medium brightness)
  memset(current_settings.led.pixels, 0, LED_MATRIX_DATA_BYTES);
  current_settings.led.brightness = 50; // Medium brightness

  // Default LED effect settings
  current_settings.led_effect_mode = 0; // None
  current_settings.led_effect_speed = 50;
  current_settings.led_effect_color_r = 255;
  current_settings.led_effect_color_g = 0;
  current_settings.led_effect_color_b = 0;

  // Footer
  current_settings.magic_end = SETTINGS_MAGIC_END;
  current_settings.crc32 = 0; // Will be computed on save
}

static bool settings_validate(const settings_t *s) {
  // Check magic numbers
  if (s->magic_start != SETTINGS_MAGIC_START) {
    return false;
  }
  if (s->magic_end != SETTINGS_MAGIC_END) {
    return false;
  }

  // Check CRC (compute CRC of everything except the CRC field itself)
  uint32_t computed_crc =
      crc32_compute(s, sizeof(settings_t) - sizeof(uint32_t));
  if (s->crc32 != computed_crc) {
    return false;
  }

  return true;
}

static bool settings_load_from_flash(void) {
  settings_t temp;

  // Read settings from flash
  if (!flash_storage_read(0, &temp, sizeof(settings_t))) {
    return false;
  }

  // Validate
  if (!settings_validate(&temp)) {
    return false;
  }

  // Copy to RAM
  memcpy(&current_settings, &temp, sizeof(settings_t));

  return true;
}

//--------------------------------------------------------------------+
// Public API Implementation
//--------------------------------------------------------------------+

void settings_init(void) {
  flash_storage_init();

#if SETTINGS_FORCE_DEFAULTS
  // Force defaults (useful for development or recovery)
  settings_set_defaults();
  settings_save();
#else
  // Try to load settings from flash
  if (!settings_load_from_flash()) {
    // Load failed, use defaults and save
    settings_set_defaults();
    settings_save();
  }
#endif

  // Apply loaded settings to modules
  usb_gamepad_set_enabled(current_settings.options.gamepad_enabled);

  // Apply LED settings (LED matrix must be initialized first in main.c)
  led_matrix_set_brightness(current_settings.led.brightness);
  led_matrix_set_raw_data(current_settings.led.pixels);
  led_matrix_set_enabled(current_settings.options.led_enabled);

  settings_dirty = false;
}

const settings_t *settings_get(void) { return &current_settings; }

bool settings_is_keyboard_enabled(void) {
  return current_settings.options.keyboard_enabled;
}

bool settings_is_gamepad_enabled(void) {
  return current_settings.options.gamepad_enabled;
}

bool settings_set_keyboard_enabled(bool enabled) {
  current_settings.options.keyboard_enabled = enabled ? 1 : 0;
  return settings_save();
}

bool settings_set_gamepad_enabled(bool enabled) {
  current_settings.options.gamepad_enabled = enabled ? 1 : 0;
  usb_gamepad_set_enabled(enabled);
  return settings_save();
}

bool settings_is_nkro_enabled(void) {
  return current_settings.options.nkro_enabled;
}

bool settings_set_nkro_enabled(bool enabled) {
  current_settings.options.nkro_enabled = enabled ? 1 : 0;
  // Note: changing NKRO mode requires USB re-enumeration to take effect
  return settings_save();
}

bool settings_set_options(settings_options_t options) {
  current_settings.options = options;
  usb_gamepad_set_enabled(options.gamepad_enabled);
  return settings_save();
}

settings_options_t settings_get_options(void) {
  return current_settings.options;
}

bool settings_reset(void) {
  settings_set_defaults();
  usb_gamepad_set_enabled(current_settings.options.gamepad_enabled);
  return settings_save();
}

bool settings_save(void) {
  // Compute CRC before saving
  current_settings.crc32 =
      crc32_compute(&current_settings, sizeof(settings_t) - sizeof(uint32_t));

  // Erase flash sector
  if (!flash_storage_erase()) {
    return false;
  }

  // Align size to 4 bytes
  uint32_t write_size = sizeof(settings_t);
  if (write_size % 4 != 0) {
    write_size += 4 - (write_size % 4);
  }

  // Write settings to flash
  if (!flash_storage_write(0, &current_settings, write_size)) {
    return false;
  }

  settings_dirty = false;
  return true;
}

uint16_t settings_get_firmware_version(void) { return FIRMWARE_VERSION; }

//--------------------------------------------------------------------+
// LED Matrix Settings API
//--------------------------------------------------------------------+

bool settings_is_led_enabled(void) {
  return current_settings.options.led_enabled;
}

bool settings_set_led_enabled(bool enabled) {
  current_settings.options.led_enabled = enabled ? 1 : 0;
  led_matrix_set_enabled(enabled);
  return true; // Don't auto-save, let user call save explicitly
}

uint8_t settings_get_led_brightness(void) {
  return current_settings.led.brightness;
}

bool settings_set_led_brightness(uint8_t brightness) {
  current_settings.led.brightness = brightness;
  led_matrix_set_brightness(brightness);
  return true; // Don't auto-save
}

const uint8_t *settings_get_led_pixels(void) {
  return current_settings.led.pixels;
}

bool settings_set_led_pixels(const uint8_t *pixels) {
  if (pixels == NULL)
    return false;

  memcpy(current_settings.led.pixels, pixels, LED_MATRIX_DATA_BYTES);
  led_matrix_set_raw_data(pixels);
  return true; // Don't auto-save
}

bool settings_set_led_pixel(uint8_t index, uint8_t r, uint8_t g, uint8_t b) {
  if (index >= LED_MATRIX_SIZE)
    return false;

  current_settings.led.pixels[index * 3 + 0] = r;
  current_settings.led.pixels[index * 3 + 1] = g;
  current_settings.led.pixels[index * 3 + 2] = b;

  led_matrix_set_pixel_idx(index, r, g, b);
  return true; // Don't auto-save
}

const settings_led_t *settings_get_led(void) { return &current_settings.led; }

//--------------------------------------------------------------------+
// Key Settings API
//--------------------------------------------------------------------+

const settings_key_t *settings_get_key(uint8_t key_index) {
  if (key_index >= 6)
    return NULL;
  return &current_settings.keys[key_index];
}

bool settings_set_key(uint8_t key_index, const settings_key_t *key) {
  if (key_index >= 6 || key == NULL)
    return false;

  memcpy(&current_settings.keys[key_index], key, sizeof(settings_key_t));
  return true; // Don't auto-save
}

//--------------------------------------------------------------------+
// Gamepad Settings API
//--------------------------------------------------------------------+

const settings_gamepad_t *settings_get_gamepad(void) {
  return &current_settings.gamepad;
}

bool settings_set_gamepad(const settings_gamepad_t *gamepad) {
  if (gamepad == NULL)
    return false;

  memcpy(&current_settings.gamepad, gamepad, sizeof(settings_gamepad_t));
  return true; // Don't auto-save
}

//--------------------------------------------------------------------+
// Calibration Settings API
//--------------------------------------------------------------------+

const settings_calibration_t *settings_get_calibration(void) {
  return &current_settings.calibration;
}

bool settings_set_calibration(const settings_calibration_t *calibration) {
  if (calibration == NULL)
    return false;

  memcpy(&current_settings.calibration, calibration,
         sizeof(settings_calibration_t));
  return true; // Don't auto-save
}

bool settings_set_key_calibration(uint8_t key_index, int16_t zero_value) {
  if (key_index >= 6)
    return false;

  current_settings.calibration.key_zero_values[key_index] = zero_value;
  return true; // Don't auto-save
}

//--------------------------------------------------------------------+
// Per-Key Curve Settings API
//--------------------------------------------------------------------+

const settings_curve_t *settings_get_key_curve(uint8_t key_index) {
  if (key_index >= 6)
    return NULL;
  return &current_settings.keys[key_index].curve;
}

bool settings_set_key_curve(uint8_t key_index, const settings_curve_t *curve) {
  if (key_index >= 6 || curve == NULL)
    return false;

  memcpy(&current_settings.keys[key_index].curve, curve,
         sizeof(settings_curve_t));
  return true;
}

bool settings_set_key_curve_enabled(uint8_t key_index, bool enabled) {
  if (key_index >= 6)
    return false;

  current_settings.keys[key_index].curve_enabled = enabled ? 1 : 0;
  return true;
}

bool settings_is_key_curve_enabled(uint8_t key_index) {
  if (key_index >= 6)
    return false;
  return current_settings.keys[key_index].curve_enabled;
}

/**
 * @brief Compute cubic bezier value
 * @param t Parameter (0-255 normalized to 0.0-1.0)
 * @param p0 Start point
 * @param p1 First control point
 * @param p2 Second control point
 * @param p3 End point
 * @return Bezier value
 */
static uint8_t bezier_cubic(uint8_t t_byte, uint8_t p0, uint8_t p1, uint8_t p2,
                            uint8_t p3) {
  // Convert to fixed point (16.16 format for better precision)
  uint32_t t = ((uint32_t)t_byte << 8) / 255; // t is now 0-256
  uint32_t t2 = (t * t) >> 8;                 // t^2
  uint32_t t3 = (t2 * t) >> 8;                // t^3
  uint32_t mt = 256 - t;                      // 1-t
  uint32_t mt2 = (mt * mt) >> 8;              // (1-t)^2
  uint32_t mt3 = (mt2 * mt) >> 8;             // (1-t)^3

  // B(t) = (1-t)^3*P0 + 3*(1-t)^2*t*P1 + 3*(1-t)*t^2*P2 + t^3*P3
  uint32_t b0 = (mt3 * p0) >> 8;
  uint32_t b1 = (3 * ((mt2 * t) >> 8) * p1) >> 8;
  uint32_t b2 = (3 * ((mt * t2) >> 8) * p2) >> 8;
  uint32_t b3 = (t3 * p3) >> 8;

  uint32_t result = b0 + b1 + b2 + b3;
  if (result > 255)
    result = 255;
  return (uint8_t)result;
}

uint8_t settings_apply_curve(uint8_t key_index, uint8_t input) {
  if (key_index >= 6)
    return input;

  // If curve is disabled, return linear
  if (!current_settings.keys[key_index].curve_enabled)
    return input;

  const settings_curve_t *curve = &current_settings.keys[key_index].curve;

  // For bezier curve, we need to find t such that bezier_x(t) = input
  // Then return bezier_y(t)
  // Since this is computationally expensive, we use a simpler approach:
  // Assume X is roughly linear and just compute Y at t=input/255

  // P0 = (0, 0), P1 = curve->p1, P2 = curve->p2, P3 = (255, 255)
  return bezier_cubic(input, 0, curve->p1.y, curve->p2.y, 255);
}

//--------------------------------------------------------------------+
// Per-Key Gamepad Mapping API
//--------------------------------------------------------------------+

const settings_gamepad_mapping_t *
settings_get_key_gamepad_mapping(uint8_t key_index) {
  if (key_index >= 6)
    return NULL;
  return &current_settings.keys[key_index].gamepad_map;
}

bool settings_set_key_gamepad_mapping(
    uint8_t key_index, const settings_gamepad_mapping_t *mapping) {
  if (key_index >= 6 || mapping == NULL)
    return false;

  memcpy(&current_settings.keys[key_index].gamepad_map, mapping,
         sizeof(settings_gamepad_mapping_t));
  return true;
}

bool settings_is_gamepad_with_keyboard(void) {
  return current_settings.options.gamepad_with_keyboard;
}

bool settings_set_gamepad_with_keyboard(bool enabled) {
  current_settings.options.gamepad_with_keyboard = enabled ? 1 : 0;
  return true;
}
