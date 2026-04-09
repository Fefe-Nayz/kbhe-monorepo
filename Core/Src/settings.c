/*
 * settings.c
 * Keyboard settings storage with EEPROM emulation
 */

#include "settings.h"
#include "analog/calibration.h"
#include "analog/filter.h"
#include "analog/lut.h"
#include "flash_storage.h"
#include "layout/layout.h"
#include "led_matrix.h"
#include "trigger/socd.h"
#include "trigger/trigger.h"
#include "hid/gamepad_hid.h"
#include <string.h>

//--------------------------------------------------------------------+
// Firmware Version
//--------------------------------------------------------------------+
#define FIRMWARE_VERSION 0x0105 // v1.5.0
#define SETTINGS_VERSION_PREVIOUS 0x000B

//--------------------------------------------------------------------+
// Internal Variables
//--------------------------------------------------------------------+

// RAM cache of current settings
static settings_t current_settings;

// Flag indicating if settings have been modified
static bool settings_dirty = false;

typedef struct __attribute__((packed)) {
  uint8_t rotation_action;
  uint8_t button_action;
  uint8_t sensitivity;
  uint8_t invert_direction;
  uint8_t rgb_behavior;
  uint8_t rgb_effect_mode;
  uint8_t rgb_step;
} settings_legacy_rotary_encoder_t;

typedef struct __attribute__((packed)) {
  // Header
  uint32_t magic_start;
  uint16_t version;
  uint16_t reserved;

  // Global options
  settings_options_t options;
  uint8_t padding1[3];

  // Per-key settings
  settings_key_t keys[NUM_KEYS];

  // Gamepad settings
  settings_gamepad_t gamepad;

  // Calibration settings
  settings_calibration_t calibration;

  // LED matrix data
  settings_led_t led;

  // LED effect settings
  uint8_t led_effect_mode;
  uint8_t led_effect_speed;
  uint8_t led_effect_color_r;
  uint8_t led_effect_color_g;
  uint8_t led_effect_color_b;
  uint8_t led_fps_limit;
  uint8_t led_effect_params[LED_EFFECT_MAX][LED_EFFECT_PARAM_COUNT];

  // ADC EMA Filter settings
  uint8_t filter_enabled;
  uint8_t filter_noise_band;
  uint8_t filter_alpha_min;
  uint8_t filter_alpha_max;

  // Footer
  uint32_t magic_end;
  uint32_t crc32;
} settings_v11_t;

static void settings_default_rotary_encoder(settings_rotary_encoder_t *rotary) {
  if (rotary == NULL) {
    return;
  }

  rotary->rotation_action = ROTARY_ACTION_VOLUME;
  rotary->button_action = ROTARY_BUTTON_ACTION_PLAY_PAUSE;
  rotary->sensitivity = 4u;
  rotary->step_size = 1u;
  rotary->invert_direction = 0u;
  rotary->rgb_behavior = ROTARY_RGB_BEHAVIOR_HUE;
  rotary->rgb_effect_mode = LED_EFFECT_SOLID;
  rotary->progress_style = ROTARY_PROGRESS_STYLE_SOLID;
  rotary->progress_effect_mode = LED_EFFECT_RAINBOW;
  rotary->progress_color_r = 40u;
  rotary->progress_color_g = 210u;
  rotary->progress_color_b = 64u;
}

static void settings_rotary_encoder_from_legacy(
    settings_rotary_encoder_t *rotary,
    const settings_legacy_rotary_encoder_t *legacy) {
  bool all_zero = true;

  if (rotary == NULL || legacy == NULL) {
    return;
  }

  settings_default_rotary_encoder(rotary);

  for (uint8_t i = 0; i < sizeof(settings_legacy_rotary_encoder_t); i++) {
    if (((const uint8_t *)legacy)[i] != 0u) {
      all_zero = false;
      break;
    }
  }

  if (all_zero) {
    return;
  }

  rotary->rotation_action = legacy->rotation_action;
  rotary->button_action = legacy->button_action;
  rotary->sensitivity = legacy->sensitivity;
  rotary->step_size = legacy->rgb_step;
  rotary->invert_direction = legacy->invert_direction;
  rotary->rgb_behavior = legacy->rgb_behavior;
  rotary->rgb_effect_mode = legacy->rgb_effect_mode;
  rotary->progress_effect_mode = legacy->rgb_effect_mode;
}

static void settings_rotary_encoder_sanitize(settings_rotary_encoder_t *rotary) {
  settings_rotary_encoder_t defaults = {0};
  if (rotary == NULL) {
    return;
  }

  settings_default_rotary_encoder(&defaults);

  if (rotary->rotation_action >= ROTARY_ACTION_MAX) {
    rotary->rotation_action = defaults.rotation_action;
  }
  if (rotary->button_action >= ROTARY_BUTTON_ACTION_MAX) {
    rotary->button_action = defaults.button_action;
  }
  if (rotary->sensitivity == 0u) {
    rotary->sensitivity = defaults.sensitivity;
  } else if (rotary->sensitivity > 16u) {
    rotary->sensitivity = 16u;
  }
  if (rotary->step_size == 0u) {
    rotary->step_size = defaults.step_size;
  } else if (rotary->step_size > 64u) {
    rotary->step_size = 64u;
  }
  rotary->invert_direction = rotary->invert_direction ? 1u : 0u;
  if (rotary->rgb_behavior >= ROTARY_RGB_BEHAVIOR_MAX) {
    rotary->rgb_behavior = defaults.rgb_behavior;
  }
  if (rotary->rgb_effect_mode >= LED_EFFECT_MAX ||
      rotary->rgb_effect_mode == LED_EFFECT_THIRD_PARTY) {
    rotary->rgb_effect_mode = defaults.rgb_effect_mode;
  }
  if (rotary->progress_style >= ROTARY_PROGRESS_STYLE_MAX) {
    rotary->progress_style = defaults.progress_style;
  }
  if (rotary->progress_effect_mode >= LED_EFFECT_MAX ||
      rotary->progress_effect_mode == LED_EFFECT_THIRD_PARTY) {
    rotary->progress_effect_mode = defaults.progress_effect_mode;
  }
}

static void settings_rotary_encoder_load(settings_rotary_encoder_t *rotary) {
  if (rotary == NULL) {
    return;
  }

  memcpy(rotary, &current_settings.rotary, sizeof(*rotary));
  settings_rotary_encoder_sanitize(rotary);
}

static void settings_default_effect_params(uint8_t effect_mode,
                                           uint8_t *params) {
  if (params == NULL) {
    return;
  }

  memset(params, 0, LED_EFFECT_PARAM_COUNT);

  switch ((led_effect_mode_t)effect_mode) {
  case LED_EFFECT_RAINBOW:
    params[0] = 160u; // Horizontal scale
    params[1] = 96u;  // Vertical scale
    params[2] = 160u; // Drift
    params[3] = 255u; // Saturation
    break;
  case LED_EFFECT_BREATHING:
    params[0] = 24u;  // Brightness floor
    params[1] = 255u; // Brightness ceiling
    params[2] = 48u;  // Plateau
    break;
  case LED_EFFECT_STATIC_RAINBOW:
    params[0] = 160u; // Horizontal scale
    params[1] = 120u; // Vertical scale
    params[2] = 144u; // Saturation
    params[3] = 255u; // Value
    break;
  case LED_EFFECT_SOLID:
    params[0] = 255u; // Brightness trim
    break;
  case LED_EFFECT_PLASMA:
    params[0] = 96u;  // Motion depth
    params[1] = 192u; // Saturation
    params[2] = 128u; // Radial warp
    params[3] = 255u; // Value
    break;
  case LED_EFFECT_FIRE:
    params[0] = 160u; // Heat boost
    params[1] = 96u;  // Ember floor
    params[2] = 96u;  // Cooling
    params[3] = 0u;   // Palette
    break;
  case LED_EFFECT_OCEAN:
    params[0] = 160u; // Hue bias
    params[1] = 64u;  // Depth dimming
    params[2] = 1u;   // Foam highlight
    params[3] = 160u; // Crest speed
    break;
  case LED_EFFECT_MATRIX:
    params[0] = 64u;  // Trail length
    params[1] = 160u; // Head size
    params[2] = 96u;  // Density
    params[3] = 1u;   // White heads
    params[4] = 0u;   // Hue bias
    break;
  case LED_EFFECT_SPARKLE:
    params[0] = 48u;  // Density
    params[1] = 224u; // Sparkle brightness
    params[2] = 160u; // Rainbow mix
    params[3] = 0u;   // Ambient glow
    break;
  case LED_EFFECT_BREATHING_RAINBOW:
    params[0] = 24u;  // Brightness floor
    params[1] = 192u; // Hue drift
    params[2] = 255u; // Saturation
    break;
  case LED_EFFECT_SPIRAL:
    params[0] = 160u; // Twist
    params[1] = 96u;  // Radial scale
    params[2] = 128u; // Orbit speed
    params[3] = 255u; // Saturation
    break;
  case LED_EFFECT_COLOR_CYCLE:
    params[0] = 64u;  // Hue step
    params[1] = 255u; // Saturation
    params[2] = 255u; // Value
    params[3] = 0u;   // Effect-color mix
    break;
  case LED_EFFECT_REACTIVE:
    params[0] = 72u;  // Decay
    params[1] = 128u; // Spread
    params[2] = 0u;   // Base glow
    params[3] = 1u;   // White core
    params[4] = 224u; // Gain
    break;
  case LED_EFFECT_DISTANCE_SENSOR:
    params[0] = 32u;  // Brightness floor
    params[1] = 170u; // Hue span
    params[2] = 255u; // Saturation
    params[3] = 0u;   // Reverse gradient
    break;
  default:
    break;
  }
}

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

_Static_assert(sizeof(settings_t) <= FLASH_STORAGE_SIZE,
               "settings_t must fit in the flash storage sector");
_Static_assert(sizeof(settings_v11_t) <= FLASH_STORAGE_SIZE,
               "settings_v11_t must fit in the flash storage sector");

static settings_key_t settings_default_key(uint8_t key_index) {
  settings_key_t key = {
      .hid_keycode = layout_get_default_keycode(key_index),
      .actuation_point_mm = 20,
      .release_point_mm = 18,
      .rapid_trigger_activation = 5,
      .rapid_trigger_press = 30,
      .rapid_trigger_release = 30,
      .socd_pair = 255,
      .rapid_trigger_enabled = 0,
      .disable_kb_on_gamepad = 0,
      .curve_enabled = 0,
      .reserved_bits = 0,
      .curve = SETTINGS_DEFAULT_CURVE,
      .gamepad_map = SETTINGS_DEFAULT_GAMEPAD_MAP,
  };
  return key;
}

static void settings_sanitize_key_config(uint8_t key_index, settings_key_t *key) {
  settings_socd_resolution_t resolution = SETTINGS_SOCD_RESOLUTION_LAST_INPUT_WINS;

  if (key == NULL) {
    return;
  }

  if (key->socd_pair >= NUM_KEYS || key->socd_pair == key_index) {
    key->socd_pair = SETTINGS_SOCD_PAIR_NONE;
  }

  resolution = settings_key_get_socd_resolution(key);
  settings_key_set_socd_resolution(key, resolution);
}

static void settings_set_defaults(void) {
  memset(&current_settings, 0, sizeof(settings_t));
  settings_rotary_encoder_t default_rotary = {0};

  // Header
  current_settings.magic_start = SETTINGS_MAGIC_START;
  current_settings.version = SETTINGS_VERSION;

  // Default options
  current_settings.options.keyboard_enabled = 0;
  current_settings.options.gamepad_enabled = 1;
  current_settings.options.raw_hid_echo = 0;
  current_settings.options.led_enabled = 1;

  // Default per-key settings follow the physical keyboard layout.
  for (uint8_t i = 0; i < NUM_KEYS; i++) {
    current_settings.keys[i] = settings_default_key(i);
  }

  // Default gamepad settings
  settings_gamepad_t default_gamepad = SETTINGS_DEFAULT_GAMEPAD;
  current_settings.gamepad = default_gamepad;

  // Default calibration settings
  current_settings.calibration.lut_zero_value = LUT_ZERO_VALUE;
  for (uint8_t i = 0; i < NUM_KEYS; i++) {
    current_settings.calibration.key_zero_values[i] = LUT_ZERO_VALUE;
    current_settings.calibration.key_max_values[i] =
        (int16_t)(LUT_BASE_VOLTAGE + LUT_SIZE - 1);
  }

  // Default LED settings
  memset(current_settings.led.pixels, 0, LED_MATRIX_DATA_BYTES);
  current_settings.led.brightness = 50; // Medium brightness

  // Default LED effect settings
  current_settings.led_effect_mode = 0; // None / static matrix
  current_settings.led_effect_speed = 50;
  current_settings.led_effect_color_r = 255;
  current_settings.led_effect_color_g = 0;
  current_settings.led_effect_color_b = 0;
  current_settings.led_fps_limit = 0; // Unlimited by default
  for (uint8_t effect = 0; effect < LED_EFFECT_MAX; effect++) {
    settings_default_effect_params(effect,
                                   current_settings.led_effect_params[effect]);
  }

  current_settings.filter_enabled = FILTER_DEFAULT_ENABLED;
  current_settings.filter_noise_band = FILTER_DEFAULT_NOISE_BAND;
  current_settings.filter_alpha_min = FILTER_DEFAULT_ALPHA_MIN_DENOM;
  current_settings.filter_alpha_max = FILTER_DEFAULT_ALPHA_MAX_DENOM;

  settings_default_rotary_encoder(&default_rotary);
  current_settings.rotary = default_rotary;

  // Footer
  current_settings.magic_end = SETTINGS_MAGIC_END;
  current_settings.crc32 = 0; // Will be computed on save
}

static bool settings_validate_current(const settings_t *s) {
  // Check magic numbers
  if (s->magic_start != SETTINGS_MAGIC_START) {
    return false;
  }
  if (s->magic_end != SETTINGS_MAGIC_END) {
    return false;
  }
  if (s->version != SETTINGS_VERSION) {
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

static bool settings_validate_v11(const settings_v11_t *s) {
  if (s->magic_start != SETTINGS_MAGIC_START) {
    return false;
  }
  if (s->magic_end != SETTINGS_MAGIC_END) {
    return false;
  }
  if (s->version != SETTINGS_VERSION_PREVIOUS) {
    return false;
  }

  if (s->crc32 !=
      crc32_compute(s, sizeof(settings_v11_t) - sizeof(uint32_t))) {
    return false;
  }

  return true;
}

static void settings_migrate_v11(const settings_v11_t *legacy) {
  settings_legacy_rotary_encoder_t legacy_rotary = {0};

  memset(&current_settings, 0, sizeof(current_settings));
  current_settings.magic_start = SETTINGS_MAGIC_START;
  current_settings.version = SETTINGS_VERSION;
  current_settings.reserved = legacy->reserved;
  current_settings.options = legacy->options;
  memcpy(current_settings.padding1, legacy->padding1,
         sizeof(current_settings.padding1));
  memcpy(current_settings.keys, legacy->keys, sizeof(current_settings.keys));
  current_settings.gamepad = legacy->gamepad;
  current_settings.calibration = legacy->calibration;
  current_settings.led = legacy->led;
  current_settings.led_effect_mode = legacy->led_effect_mode;
  current_settings.led_effect_speed = legacy->led_effect_speed;
  current_settings.led_effect_color_r = legacy->led_effect_color_r;
  current_settings.led_effect_color_g = legacy->led_effect_color_g;
  current_settings.led_effect_color_b = legacy->led_effect_color_b;
  current_settings.led_fps_limit = legacy->led_fps_limit;
  memcpy(current_settings.led_effect_params, legacy->led_effect_params,
         sizeof(current_settings.led_effect_params));
  current_settings.filter_enabled = legacy->filter_enabled;
  current_settings.filter_noise_band = legacy->filter_noise_band;
  current_settings.filter_alpha_min = legacy->filter_alpha_min;
  current_settings.filter_alpha_max = legacy->filter_alpha_max;

  memcpy(&legacy_rotary, legacy->gamepad.reserved, 4u);
  memcpy(((uint8_t *)&legacy_rotary) + 4u, legacy->led.reserved, 3u);
  settings_rotary_encoder_from_legacy(&current_settings.rotary, &legacy_rotary);
  settings_rotary_encoder_sanitize(&current_settings.rotary);

  memset(current_settings.gamepad.reserved, 0,
         sizeof(current_settings.gamepad.reserved));
  memset(current_settings.led.reserved, 0, sizeof(current_settings.led.reserved));
  current_settings.magic_end = SETTINGS_MAGIC_END;
  current_settings.crc32 = 0u;
}

static bool settings_load_from_flash(bool *migrated) {
  settings_t temp;
  settings_v11_t legacy;

  if (migrated != NULL) {
    *migrated = false;
  }

  // Read settings from flash
  if (!flash_storage_read(0, &temp, sizeof(settings_t))) {
    return false;
  }

  // Validate
  if (settings_validate_current(&temp)) {
    memcpy(&current_settings, &temp, sizeof(settings_t));
    return true;
  }

  if (!flash_storage_read(0, &legacy, sizeof(settings_v11_t))) {
    return false;
  }

  if (!settings_validate_v11(&legacy)) {
    return false;
  }

  settings_migrate_v11(&legacy);
  if (migrated != NULL) {
    *migrated = true;
  }

  return true;
}

//--------------------------------------------------------------------+
// Public API Implementation
//--------------------------------------------------------------------+

void settings_init(void) {
  flash_storage_init();
  bool migrated = false;

#if SETTINGS_FORCE_DEFAULTS
  // Force defaults (useful for development or recovery)
  settings_set_defaults();
  settings_save();
#else
  // Try to load settings from flash
  if (!settings_load_from_flash(&migrated)) {
    // Load failed, use defaults and save
    settings_set_defaults();
    settings_save();
  } else if (migrated) {
    settings_save();
  }
#endif

  // Apply loaded settings to modules
  gamepad_hid_set_enabled(current_settings.options.gamepad_enabled);

  // Apply LED settings (LED matrix must be initialized first in main.c)
  led_matrix_set_brightness(current_settings.led.brightness);
  led_matrix_set_raw_data(current_settings.led.pixels);
  led_matrix_set_enabled(current_settings.options.led_enabled);

  // Apply LED effect settings
  led_matrix_set_effect((led_effect_mode_t)current_settings.led_effect_mode);
  led_matrix_set_effect_speed(current_settings.led_effect_speed);
  led_matrix_set_effect_color(current_settings.led_effect_color_r,
                              current_settings.led_effect_color_g,
                              current_settings.led_effect_color_b);
  led_matrix_set_effect_params(
      current_settings.led_effect_params[current_settings.led_effect_mode]);
  led_matrix_set_fps_limit(current_settings.led_fps_limit);

  filter_set_params(current_settings.filter_noise_band,
                    current_settings.filter_alpha_min,
                    current_settings.filter_alpha_max);
  filter_set_enabled(current_settings.filter_enabled != 0);
  calibration_load_settings();

  // Apply per-key runtime settings after the settings blob is loaded.
  trigger_reload_settings();

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
  gamepad_hid_set_enabled(enabled);
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
  gamepad_hid_set_enabled(options.gamepad_enabled);
  return settings_save();
}

settings_options_t settings_get_options(void) {
  return current_settings.options;
}

bool settings_reset(void) {
  settings_set_defaults();
  gamepad_hid_set_enabled(current_settings.options.gamepad_enabled);
  led_matrix_set_brightness(current_settings.led.brightness);
  led_matrix_set_raw_data(current_settings.led.pixels);
  led_matrix_set_enabled(current_settings.options.led_enabled);
  led_matrix_set_effect((led_effect_mode_t)current_settings.led_effect_mode);
  led_matrix_set_effect_speed(current_settings.led_effect_speed);
  led_matrix_set_effect_color(current_settings.led_effect_color_r,
                              current_settings.led_effect_color_g,
                              current_settings.led_effect_color_b);
  led_matrix_set_effect_params(
      current_settings.led_effect_params[current_settings.led_effect_mode]);
  led_matrix_set_fps_limit(current_settings.led_fps_limit);
  filter_set_params(current_settings.filter_noise_band,
                    current_settings.filter_alpha_min,
                    current_settings.filter_alpha_max);
  filter_set_enabled(current_settings.filter_enabled != 0);
  calibration_load_settings();
  trigger_reload_settings();
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
// LED Effect Settings API
//--------------------------------------------------------------------+

uint8_t settings_get_led_effect_mode(void) {
  return current_settings.led_effect_mode;
}

bool settings_set_led_effect_mode(uint8_t mode) {
  if (mode >= LED_EFFECT_MAX) {
    return false;
  }
  current_settings.led_effect_mode = mode;
  led_matrix_set_effect((led_effect_mode_t)mode);
  led_matrix_set_effect_params(current_settings.led_effect_params[mode]);
  return true; // Don't auto-save
}

uint8_t settings_get_led_effect_speed(void) {
  return current_settings.led_effect_speed;
}

bool settings_set_led_effect_speed(uint8_t speed) {
  current_settings.led_effect_speed = speed;
  led_matrix_set_effect_speed(speed);
  return true; // Don't auto-save
}

uint8_t settings_get_led_fps_limit(void) {
  return current_settings.led_fps_limit;
}

void settings_get_led_effect_color(uint8_t *r, uint8_t *g, uint8_t *b) {
  if (r)
    *r = current_settings.led_effect_color_r;
  if (g)
    *g = current_settings.led_effect_color_g;
  if (b)
    *b = current_settings.led_effect_color_b;
}

bool settings_set_led_effect_color(uint8_t r, uint8_t g, uint8_t b) {
  current_settings.led_effect_color_r = r;
  current_settings.led_effect_color_g = g;
  current_settings.led_effect_color_b = b;
  led_matrix_set_effect_color(r, g, b);
  return true; // Don't auto-save
}

void settings_get_led_effect_params(uint8_t effect_mode, uint8_t *params) {
  if (params == NULL) {
    return;
  }

  if (effect_mode >= LED_EFFECT_MAX) {
    memset(params, 0, LED_EFFECT_PARAM_COUNT);
    return;
  }

  memcpy(params, current_settings.led_effect_params[effect_mode],
         LED_EFFECT_PARAM_COUNT);
}

bool settings_set_led_effect_params(uint8_t effect_mode, const uint8_t *params) {
  if (effect_mode >= LED_EFFECT_MAX || params == NULL) {
    return false;
  }

  memcpy(current_settings.led_effect_params[effect_mode], params,
         LED_EFFECT_PARAM_COUNT);

  if (current_settings.led_effect_mode == effect_mode) {
    led_matrix_set_effect_params(current_settings.led_effect_params[effect_mode]);
  }

  return true;
}

//--------------------------------------------------------------------+
// ADC Filter Settings API
//--------------------------------------------------------------------+

bool settings_is_filter_enabled(void) {
  return current_settings.filter_enabled != 0;
}

bool settings_set_filter_enabled(bool enabled) {
  current_settings.filter_enabled = enabled ? 1 : 0;
  filter_set_enabled(enabled);
  return true;
}

void settings_get_filter_params(uint8_t *noise_band, uint8_t *alpha_min_denom,
                                uint8_t *alpha_max_denom) {
  if (noise_band != NULL) {
    *noise_band = current_settings.filter_noise_band;
  }
  if (alpha_min_denom != NULL) {
    *alpha_min_denom = current_settings.filter_alpha_min;
  }
  if (alpha_max_denom != NULL) {
    *alpha_max_denom = current_settings.filter_alpha_max;
  }
}

bool settings_set_filter_params(uint8_t noise_band, uint8_t alpha_min_denom,
                                uint8_t alpha_max_denom) {
  filter_set_params(noise_band, alpha_min_denom, alpha_max_denom);
  filter_get_params(&current_settings.filter_noise_band,
                    &current_settings.filter_alpha_min,
                    &current_settings.filter_alpha_max);
  return true;
}

//--------------------------------------------------------------------+
// Key Settings API
//--------------------------------------------------------------------+

const settings_key_t *settings_get_key(uint8_t key_index) {
  if (key_index >= NUM_KEYS)
    return NULL;
  return &current_settings.keys[key_index];
}

bool settings_set_key(uint8_t key_index, const settings_key_t *key) {
  if (key_index >= NUM_KEYS || key == NULL)
    return false;

  memcpy(&current_settings.keys[key_index], key, sizeof(settings_key_t));
  settings_sanitize_key_config(key_index, &current_settings.keys[key_index]);
  trigger_apply_key_settings(key_index, &current_settings.keys[key_index]);
  socd_load_settings();
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

void settings_get_rotary_encoder(settings_rotary_encoder_t *rotary) {
  settings_rotary_encoder_load(rotary);
}

bool settings_set_rotary_encoder(const settings_rotary_encoder_t *rotary) {
  settings_rotary_encoder_t sanitized = {0};
  if (rotary == NULL) {
    return false;
  }

  memcpy(&sanitized, rotary, sizeof(sanitized));
  settings_rotary_encoder_sanitize(&sanitized);
  current_settings.rotary = sanitized;
  return true;
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
  calibration_load_settings();
  return true; // Don't auto-save
}

bool settings_set_key_calibration(uint8_t key_index, int16_t zero_value) {
  if (key_index >= NUM_KEYS)
    return false;

  current_settings.calibration.key_zero_values[key_index] = zero_value;
  calibration_load_settings();
  return true; // Don't auto-save
}

bool settings_set_key_calibration_max(uint8_t key_index, int16_t max_value) {
  if (key_index >= NUM_KEYS) {
    return false;
  }

  current_settings.calibration.key_max_values[key_index] = max_value;
  calibration_load_settings();
  return true; // Don't auto-save
}

//--------------------------------------------------------------------+
// Per-Key Curve Settings API
//--------------------------------------------------------------------+

const settings_curve_t *settings_get_key_curve(uint8_t key_index) {
  if (key_index >= NUM_KEYS)
    return NULL;
  return &current_settings.keys[key_index].curve;
}

bool settings_set_key_curve(uint8_t key_index, const settings_curve_t *curve) {
  if (key_index >= NUM_KEYS || curve == NULL)
    return false;

  memcpy(&current_settings.keys[key_index].curve, curve,
         sizeof(settings_curve_t));
  return true;
}

bool settings_set_key_curve_enabled(uint8_t key_index, bool enabled) {
  if (key_index >= NUM_KEYS)
    return false;

  current_settings.keys[key_index].curve_enabled = enabled ? 1 : 0;
  return true;
}

bool settings_is_key_curve_enabled(uint8_t key_index) {
  if (key_index >= NUM_KEYS)
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
  if (key_index >= NUM_KEYS)
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
  if (key_index >= NUM_KEYS)
    return NULL;
  return &current_settings.keys[key_index].gamepad_map;
}

bool settings_set_key_gamepad_mapping(
    uint8_t key_index, const settings_gamepad_mapping_t *mapping) {
  if (key_index >= NUM_KEYS || mapping == NULL)
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
