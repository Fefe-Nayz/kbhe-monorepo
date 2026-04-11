/*
 * settings.c
 * Keyboard settings storage with EEPROM emulation
 */

#include "settings.h"
#include "analog/calibration.h"
#include "analog/filter.h"
#include "analog/lut.h"
#include "flash_storage.h"
#include "layout/keycodes.h"
#include "layout/layout.h"
#include "led_matrix.h"
#include "trigger/socd.h"
#include "trigger/trigger.h"
#include "hid/gamepad_hid.h"
#include <string.h>

//--------------------------------------------------------------------+
// Firmware Version
//--------------------------------------------------------------------+
#define FIRMWARE_VERSION 0x0107 // v1.7.0
#define KBHE_FW_VERSION_RECORD_MAGIC 0x4B465756u
#define SETTINGS_VERSION_PREVIOUS 0x0010
#define SETTINGS_VERSION_V13 0x000E
#define SETTINGS_VERSION_V12 0x000C
#define SETTINGS_VERSION_V11 0x000B

typedef struct __attribute__((packed)) {
  uint32_t magic;
  uint16_t version;
  uint16_t version_xor;
} kbhe_fw_version_record_t;

__attribute__((used, section(".kbhe_fw_version")))
static const kbhe_fw_version_record_t g_kbhe_fw_version_record = {
    .magic = KBHE_FW_VERSION_RECORD_MAGIC,
    .version = FIRMWARE_VERSION,
    .version_xor = (uint16_t)(FIRMWARE_VERSION ^ 0xFFFFu),
};

//--------------------------------------------------------------------+
// Internal Variables
//--------------------------------------------------------------------+

// RAM cache of current settings
static settings_t current_settings;

// Flag indicating if settings have been modified
static bool settings_dirty = false;
static uint32_t settings_change_counter = 0u;
static uint32_t settings_last_seen_change_counter = 0u;
static uint32_t settings_last_change_ms = 0u;

#define SETTINGS_AUTOSAVE_DELAY_MS 750u

static uint8_t led_effect_restore_mode = LED_EFFECT_STATIC_MATRIX;
static bool led_effect_restore_valid = false;

static settings_key_t settings_default_key(uint8_t key_index);

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
  uint16_t hid_keycode;
  uint8_t actuation_point_mm;
  uint8_t release_point_mm;
  uint8_t rapid_trigger_activation;
  uint8_t rapid_trigger_press;
  uint8_t rapid_trigger_release;
  uint8_t socd_pair;
  uint8_t rapid_trigger_enabled : 1;
  uint8_t disable_kb_on_gamepad : 1;
  uint8_t curve_enabled : 1;
  uint8_t reserved_bits : 5;
  settings_curve_t curve;
  settings_gamepad_mapping_t gamepad_map;
} settings_key_v13_t;

typedef struct __attribute__((packed)) {
  uint8_t radial_deadzone;
  uint8_t keyboard_routing;
  uint8_t square_mode;
  uint8_t reactive_stick;
  settings_gamepad_curve_point_t curve[GAMEPAD_CURVE_POINT_COUNT];
} settings_gamepad_v13_t;

typedef struct __attribute__((packed)) {
  uint32_t magic_start;
  uint16_t version;
  uint16_t reserved;

  settings_options_t options;
  uint8_t padding1[3];

  settings_key_v13_t keys[NUM_KEYS];

  settings_gamepad_v13_t gamepad;
  settings_calibration_t calibration;
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
  uint32_t magic_end;
  uint32_t crc32;
} settings_v13_t;

typedef struct __attribute__((packed)) {
  uint32_t magic_start;
  uint16_t version;
  uint16_t reserved;

  settings_options_t options;
  uint8_t padding1[3];

  settings_key_t keys[NUM_KEYS];

  settings_gamepad_t gamepad;
  settings_calibration_t calibration;
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
  uint32_t magic_end;
  uint32_t crc32;
} settings_v14_t;

typedef struct __attribute__((packed)) {
  uint8_t deadzone;
  uint8_t curve_type;
  uint8_t square_mode;
  uint8_t snappy_mode;
  uint8_t reserved[4];
} settings_gamepad_v12_t;

typedef struct __attribute__((packed)) {
  // Header
  uint32_t magic_start;
  uint16_t version;
  uint16_t reserved;

  // Global options
  settings_options_t options;
  uint8_t padding1[3];

  // Per-key settings
  settings_key_v13_t keys[NUM_KEYS];

  // Gamepad settings
  settings_gamepad_v12_t gamepad;

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
  settings_rotary_encoder_t rotary;

  // ADC EMA Filter settings
  uint8_t filter_enabled;
  uint8_t filter_noise_band;
  uint8_t filter_alpha_min;
  uint8_t filter_alpha_max;

  // Footer
  uint32_t magic_end;
  uint32_t crc32;
} settings_v12_t;

typedef struct __attribute__((packed)) {
  // Header
  uint32_t magic_start;
  uint16_t version;
  uint16_t reserved;

  // Global options
  settings_options_t options;
  uint8_t padding1[3];

  // Per-key settings
  settings_key_v13_t keys[NUM_KEYS];

  // Gamepad settings
  settings_gamepad_v12_t gamepad;

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

static void settings_default_gamepad(settings_gamepad_t *gamepad) {
  settings_gamepad_t defaults = SETTINGS_DEFAULT_GAMEPAD;

  if (gamepad == NULL) {
    return;
  }

  memcpy(gamepad, &defaults, sizeof(defaults));
}

static void settings_default_layer_keycodes(void) {
  for (uint8_t layer = 1u; layer < SETTINGS_LAYER_COUNT; layer++) {
    for (uint8_t key = 0u; key < NUM_KEYS; key++) {
      current_settings.layer_keycodes[layer - 1u][key] =
          layout_get_default_layer_keycode(layer, key);
    }
  }
}

static void settings_sanitize_layer_keycodes(void) {
  for (uint8_t layer = 1u; layer < SETTINGS_LAYER_COUNT; layer++) {
    for (uint8_t key = 0u; key < NUM_KEYS; key++) {
      if (current_settings.layer_keycodes[layer - 1u][key] == 0xFFFFu) {
        current_settings.layer_keycodes[layer - 1u][key] = KC_TRANSPARENT;
      }
    }
  }
}

static uint16_t settings_gamepad_deadzone_to_curve_x_01mm(uint8_t deadzone) {
  uint32_t scaled =
      ((uint32_t)deadzone * GAMEPAD_CURVE_MAX_DISTANCE_01MM + 127u) / 255u;
  if (scaled > GAMEPAD_CURVE_MAX_DISTANCE_01MM) {
    scaled = GAMEPAD_CURVE_MAX_DISTANCE_01MM;
  }
  return (uint16_t)scaled;
}

static void settings_gamepad_sanitize_mapping(settings_gamepad_mapping_t *mapping) {
  if (mapping == NULL) {
    return;
  }

  if (mapping->axis > GAMEPAD_AXIS_TRIGGER_R) {
    mapping->axis = GAMEPAD_AXIS_NONE;
  }

  if (mapping->direction > GAMEPAD_DIR_NEGATIVE) {
    mapping->direction = GAMEPAD_DIR_POSITIVE;
  }

  if (mapping->button > GAMEPAD_BUTTON_HOME) {
    mapping->button = GAMEPAD_BUTTON_NONE;
  }

  mapping->reserved = 0u;
}

static void settings_default_key_advanced(settings_key_advanced_t *advanced,
                                          uint16_t primary_hid_keycode) {
  if (advanced == NULL) {
    return;
  }

  memset(advanced, 0, sizeof(*advanced));
  advanced->behavior_mode = (uint8_t)KEY_BEHAVIOR_NORMAL;
  advanced->hold_threshold_10ms = 20u;
  advanced->dynamic_zone_count = 1u;
  advanced->secondary_hid_keycode = KC_NO;
  advanced->dynamic_zones[0].end_mm_tenths = 40u;
  advanced->dynamic_zones[0].hid_keycode = primary_hid_keycode;
  for (uint8_t i = 1u; i < SETTINGS_DYNAMIC_ZONE_COUNT; i++) {
    advanced->dynamic_zones[i].end_mm_tenths = 40u;
    advanced->dynamic_zones[i].hid_keycode = KC_NO;
  }
}

static void settings_copy_key_from_v13(settings_key_t *dst,
                                       const settings_key_v13_t *src,
                                       uint8_t key_index) {
  settings_key_t defaults = settings_default_key(key_index);

  if (dst == NULL) {
    return;
  }

  *dst = defaults;
  if (src == NULL) {
    return;
  }

  dst->hid_keycode = src->hid_keycode;
  dst->actuation_point_mm = src->actuation_point_mm;
  dst->release_point_mm = src->release_point_mm;
  dst->rapid_trigger_activation = src->rapid_trigger_activation;
  dst->rapid_trigger_press = src->rapid_trigger_press;
  dst->rapid_trigger_release = src->rapid_trigger_release;
  dst->socd_pair = src->socd_pair;
  dst->rapid_trigger_enabled = src->rapid_trigger_enabled;
  dst->disable_kb_on_gamepad = src->disable_kb_on_gamepad;
  dst->curve_enabled = src->curve_enabled;
  dst->reserved_bits = src->reserved_bits;
  dst->curve = src->curve;
  dst->gamepad_map = src->gamepad_map;
  settings_default_key_advanced(&dst->advanced, dst->hid_keycode);
}

static void settings_sanitize_key_advanced(uint16_t primary_hid_keycode,
                                           settings_key_t *key) {
  settings_key_advanced_t defaults = {0};
  uint8_t previous_end = 0u;
  bool has_any_dynamic_action = false;

  if (key == NULL) {
    return;
  }

  settings_default_key_advanced(&defaults, primary_hid_keycode);

  if (key->advanced.behavior_mode >= (uint8_t)KEY_BEHAVIOR_MAX) {
    key->advanced.behavior_mode = defaults.behavior_mode;
  }

  if (key->advanced.hold_threshold_10ms == 0u) {
    key->advanced.hold_threshold_10ms = defaults.hold_threshold_10ms;
  }

  if (key->advanced.dynamic_zone_count == 0u ||
      key->advanced.dynamic_zone_count > SETTINGS_DYNAMIC_ZONE_COUNT) {
    key->advanced.dynamic_zone_count = defaults.dynamic_zone_count;
  }

  key->advanced.reserved = 0u;

  for (uint8_t i = 0u; i < SETTINGS_DYNAMIC_ZONE_COUNT; i++) {
    if (key->advanced.dynamic_zones[i].end_mm_tenths == 0u) {
      key->advanced.dynamic_zones[i].end_mm_tenths =
          defaults.dynamic_zones[i].end_mm_tenths;
    } else if (key->advanced.dynamic_zones[i].end_mm_tenths > 40u) {
      key->advanced.dynamic_zones[i].end_mm_tenths = 40u;
    }

    if (key->advanced.dynamic_zones[i].end_mm_tenths < previous_end) {
      key->advanced.dynamic_zones[i].end_mm_tenths = previous_end;
    }

    previous_end = key->advanced.dynamic_zones[i].end_mm_tenths;
    if (key->advanced.dynamic_zones[i].hid_keycode != KC_NO) {
      has_any_dynamic_action = true;
    }
  }

  if (!has_any_dynamic_action) {
    key->advanced.dynamic_zones[0].hid_keycode = primary_hid_keycode;
  } else if (key->advanced.dynamic_zones[0].hid_keycode == KC_NO) {
    key->advanced.dynamic_zones[0].hid_keycode = primary_hid_keycode;
  }
}

static void settings_gamepad_sanitize(settings_gamepad_t *gamepad) {
  settings_gamepad_t defaults;
  uint16_t previous_x = 0u;
  bool has_any_span = false;

  if (gamepad == NULL) {
    return;
  }

  settings_default_gamepad(&defaults);

  if (gamepad->keyboard_routing >= (uint8_t)GAMEPAD_KEYBOARD_ROUTING_MAX) {
    gamepad->keyboard_routing = defaults.keyboard_routing;
  }

  if (gamepad->api_mode >= (uint8_t)GAMEPAD_API_MAX) {
    gamepad->api_mode = defaults.api_mode;
  }

  // The first curve point position is now the only authoritative start deadzone.
  gamepad->radial_deadzone = 0u;
  gamepad->square_mode = gamepad->square_mode ? 1u : 0u;
  gamepad->reactive_stick = gamepad->reactive_stick ? 1u : 0u;

  for (uint8_t i = 0; i < GAMEPAD_CURVE_POINT_COUNT; i++) {
    if (gamepad->curve[i].x_01mm > GAMEPAD_CURVE_MAX_DISTANCE_01MM) {
      gamepad->curve[i].x_01mm = GAMEPAD_CURVE_MAX_DISTANCE_01MM;
    }

    if (i > 0u && gamepad->curve[i].x_01mm < previous_x) {
      gamepad->curve[i].x_01mm = previous_x;
    }

    if (gamepad->curve[i].x_01mm != previous_x || i == 0u) {
      has_any_span = true;
    }

    previous_x = gamepad->curve[i].x_01mm;
  }

  if (!has_any_span) {
    memcpy(gamepad->curve, defaults.curve, sizeof(defaults.curve));
  }
}

static void settings_gamepad_apply_keyboard_routing_option(void) {
  current_settings.options.gamepad_with_keyboard =
      current_settings.gamepad.keyboard_routing !=
              (uint8_t)GAMEPAD_KEYBOARD_ROUTING_DISABLED
          ? 1u
          : 0u;
}

static uint8_t settings_sanitize_advanced_tick_rate(uint8_t tick_rate) {
  if (tick_rate < SETTINGS_ADVANCED_TICK_RATE_MIN) {
    return SETTINGS_ADVANCED_TICK_RATE_MIN;
  }

  if (tick_rate > SETTINGS_ADVANCED_TICK_RATE_MAX) {
    return SETTINGS_ADVANCED_TICK_RATE_MAX;
  }

  return tick_rate;
}

static void settings_gamepad_from_v13(settings_gamepad_t *gamepad,
                                      const settings_gamepad_v13_t *legacy) {
  if (gamepad == NULL) {
    return;
  }

  settings_default_gamepad(gamepad);
  if (legacy == NULL) {
    return;
  }

  gamepad->radial_deadzone = 0u;
  gamepad->keyboard_routing = legacy->keyboard_routing;
  gamepad->square_mode = legacy->square_mode ? 1u : 0u;
  gamepad->reactive_stick = legacy->reactive_stick ? 1u : 0u;
  gamepad->api_mode = (uint8_t)GAMEPAD_API_HID;
  memcpy(gamepad->curve, legacy->curve, sizeof(gamepad->curve));
  if (legacy->radial_deadzone > 0u && gamepad->curve[0].x_01mm == 0u) {
    gamepad->curve[0].x_01mm =
        settings_gamepad_deadzone_to_curve_x_01mm(legacy->radial_deadzone);
  }
  settings_gamepad_sanitize(gamepad);
}

static void settings_gamepad_from_v12(settings_gamepad_t *gamepad,
                                      const settings_gamepad_v12_t *legacy,
                                      bool legacy_gamepad_with_keyboard) {
  if (gamepad == NULL) {
    return;
  }

  settings_default_gamepad(gamepad);
  if (legacy == NULL) {
    return;
  }

  gamepad->radial_deadzone = 0u;
  gamepad->keyboard_routing =
      legacy_gamepad_with_keyboard
          ? (uint8_t)GAMEPAD_KEYBOARD_ROUTING_ALL_KEYS
          : (uint8_t)GAMEPAD_KEYBOARD_ROUTING_DISABLED;
  gamepad->square_mode = legacy->square_mode ? 1u : 0u;
  gamepad->reactive_stick = legacy->snappy_mode ? 1u : 0u;
  gamepad->api_mode = (uint8_t)GAMEPAD_API_HID;
  if (legacy->deadzone > 0u) {
    gamepad->curve[0].x_01mm =
        settings_gamepad_deadzone_to_curve_x_01mm(legacy->deadzone);
  }
  settings_gamepad_sanitize(gamepad);
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

static void settings_reset_led_effect_restore_state(void) {
  led_effect_restore_mode = current_settings.led_effect_mode;
  led_effect_restore_valid = false;
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

static void settings_mark_dirty(void) {
  settings_dirty = true;
  settings_change_counter++;
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
_Static_assert(sizeof(settings_v14_t) <= FLASH_STORAGE_SIZE,
               "settings_v14_t must fit in the flash storage sector");
_Static_assert(sizeof(settings_v13_t) <= FLASH_STORAGE_SIZE,
               "settings_v13_t must fit in the flash storage sector");
_Static_assert(sizeof(settings_v12_t) <= FLASH_STORAGE_SIZE,
               "settings_v12_t must fit in the flash storage sector");
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
  settings_default_key_advanced(&key.advanced, key.hid_keycode);
  settings_gamepad_sanitize_mapping(&key.gamepad_map);
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
  settings_gamepad_sanitize_mapping(&key->gamepad_map);
  settings_sanitize_key_advanced(key->hid_keycode, key);
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
  settings_default_layer_keycodes();

  // Default gamepad settings
  settings_default_gamepad(&current_settings.gamepad);
  settings_gamepad_apply_keyboard_routing_option();

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
  current_settings.advanced_tick_rate = SETTINGS_DEFAULT_ADVANCED_TICK_RATE;

  settings_default_rotary_encoder(&default_rotary);
  current_settings.rotary = default_rotary;
  settings_reset_led_effect_restore_state();

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

static bool settings_validate_v13(const settings_v13_t *s) {
  if (s->magic_start != SETTINGS_MAGIC_START) {
    return false;
  }
  if (s->magic_end != SETTINGS_MAGIC_END) {
    return false;
  }
  if (s->version != SETTINGS_VERSION_V13) {
    return false;
  }

  if (s->crc32 !=
      crc32_compute(s, sizeof(settings_v13_t) - sizeof(uint32_t))) {
    return false;
  }

  return true;
}

static bool settings_validate_v14(const settings_v14_t *s) {
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
      crc32_compute(s, sizeof(settings_v14_t) - sizeof(uint32_t))) {
    return false;
  }

  return true;
}

static bool settings_validate_v12(const settings_v12_t *s) {
  if (s->magic_start != SETTINGS_MAGIC_START) {
    return false;
  }
  if (s->magic_end != SETTINGS_MAGIC_END) {
    return false;
  }
  if (s->version != SETTINGS_VERSION_V12) {
    return false;
  }

  if (s->crc32 !=
      crc32_compute(s, sizeof(settings_v12_t) - sizeof(uint32_t))) {
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
  if (s->version != SETTINGS_VERSION_V11) {
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
  for (uint8_t i = 0; i < NUM_KEYS; i++) {
    settings_copy_key_from_v13(&current_settings.keys[i], &legacy->keys[i], i);
  }
  settings_default_layer_keycodes();
  settings_gamepad_from_v12(&current_settings.gamepad, &legacy->gamepad,
                            legacy->options.gamepad_with_keyboard != 0);
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
  current_settings.advanced_tick_rate = SETTINGS_DEFAULT_ADVANCED_TICK_RATE;

  memcpy(&legacy_rotary, legacy->gamepad.reserved, 4u);
  memcpy(((uint8_t *)&legacy_rotary) + 4u, legacy->led.reserved, 3u);
  settings_rotary_encoder_from_legacy(&current_settings.rotary, &legacy_rotary);
  settings_rotary_encoder_sanitize(&current_settings.rotary);
  settings_gamepad_apply_keyboard_routing_option();
  settings_reset_led_effect_restore_state();
  memset(current_settings.led.reserved, 0, sizeof(current_settings.led.reserved));
  current_settings.magic_end = SETTINGS_MAGIC_END;
  current_settings.crc32 = 0u;
}

static void settings_migrate_v13(const settings_v13_t *legacy) {
  memset(&current_settings, 0, sizeof(current_settings));
  current_settings.magic_start = SETTINGS_MAGIC_START;
  current_settings.version = SETTINGS_VERSION;
  current_settings.reserved = legacy->reserved;
  current_settings.options = legacy->options;
  memcpy(current_settings.padding1, legacy->padding1,
         sizeof(current_settings.padding1));
  for (uint8_t i = 0; i < NUM_KEYS; i++) {
    settings_copy_key_from_v13(&current_settings.keys[i], &legacy->keys[i], i);
  }
  settings_default_layer_keycodes();
  settings_gamepad_from_v13(&current_settings.gamepad, &legacy->gamepad);
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
  current_settings.rotary = legacy->rotary;
  current_settings.filter_enabled = legacy->filter_enabled;
  current_settings.filter_noise_band = legacy->filter_noise_band;
  current_settings.filter_alpha_min = legacy->filter_alpha_min;
  current_settings.filter_alpha_max = legacy->filter_alpha_max;
  current_settings.advanced_tick_rate = SETTINGS_DEFAULT_ADVANCED_TICK_RATE;

  for (uint8_t i = 0; i < NUM_KEYS; i++) {
    settings_sanitize_key_config(i, &current_settings.keys[i]);
  }
  settings_gamepad_sanitize(&current_settings.gamepad);
  settings_gamepad_apply_keyboard_routing_option();
  settings_rotary_encoder_sanitize(&current_settings.rotary);
  settings_reset_led_effect_restore_state();
  current_settings.magic_end = SETTINGS_MAGIC_END;
  current_settings.crc32 = 0u;
}

static void settings_migrate_v14(const settings_v14_t *legacy) {
  memset(&current_settings, 0, sizeof(current_settings));
  current_settings.magic_start = SETTINGS_MAGIC_START;
  current_settings.version = SETTINGS_VERSION;
  current_settings.reserved = legacy->reserved;
  current_settings.options = legacy->options;
  memcpy(current_settings.padding1, legacy->padding1,
         sizeof(current_settings.padding1));
  for (uint8_t i = 0; i < NUM_KEYS; i++) {
    current_settings.keys[i] = legacy->keys[i];
  }
  settings_default_layer_keycodes();
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
  current_settings.rotary = legacy->rotary;
  current_settings.filter_enabled = legacy->filter_enabled;
  current_settings.filter_noise_band = legacy->filter_noise_band;
  current_settings.filter_alpha_min = legacy->filter_alpha_min;
  current_settings.filter_alpha_max = legacy->filter_alpha_max;
  current_settings.advanced_tick_rate = SETTINGS_DEFAULT_ADVANCED_TICK_RATE;

  for (uint8_t i = 0; i < NUM_KEYS; i++) {
    settings_sanitize_key_config(i, &current_settings.keys[i]);
  }
  settings_gamepad_sanitize(&current_settings.gamepad);
  settings_gamepad_apply_keyboard_routing_option();
  settings_rotary_encoder_sanitize(&current_settings.rotary);
  current_settings.advanced_tick_rate = settings_sanitize_advanced_tick_rate(
      current_settings.advanced_tick_rate);
  settings_reset_led_effect_restore_state();
  current_settings.magic_end = SETTINGS_MAGIC_END;
  current_settings.crc32 = 0u;
}

static void settings_migrate_v12(const settings_v12_t *legacy) {
  memset(&current_settings, 0, sizeof(current_settings));
  current_settings.magic_start = SETTINGS_MAGIC_START;
  current_settings.version = SETTINGS_VERSION;
  current_settings.reserved = legacy->reserved;
  current_settings.options = legacy->options;
  memcpy(current_settings.padding1, legacy->padding1,
         sizeof(current_settings.padding1));
  for (uint8_t i = 0; i < NUM_KEYS; i++) {
    settings_copy_key_from_v13(&current_settings.keys[i], &legacy->keys[i], i);
  }
  settings_default_layer_keycodes();
  settings_gamepad_from_v12(&current_settings.gamepad, &legacy->gamepad,
                            legacy->options.gamepad_with_keyboard != 0);
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
  current_settings.rotary = legacy->rotary;
  current_settings.filter_enabled = legacy->filter_enabled;
  current_settings.filter_noise_band = legacy->filter_noise_band;
  current_settings.filter_alpha_min = legacy->filter_alpha_min;
  current_settings.filter_alpha_max = legacy->filter_alpha_max;
  current_settings.advanced_tick_rate = SETTINGS_DEFAULT_ADVANCED_TICK_RATE;

  for (uint8_t i = 0; i < NUM_KEYS; i++) {
    settings_sanitize_key_config(i, &current_settings.keys[i]);
  }
  settings_gamepad_apply_keyboard_routing_option();
  settings_rotary_encoder_sanitize(&current_settings.rotary);
  settings_reset_led_effect_restore_state();
  current_settings.magic_end = SETTINGS_MAGIC_END;
  current_settings.crc32 = 0u;
}

static bool settings_load_from_flash(bool *migrated) {
  settings_t temp;
  settings_v14_t legacy_v14;
  settings_v13_t legacy_v13;
  settings_v12_t legacy_v12;
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
    for (uint8_t i = 0; i < NUM_KEYS; i++) {
      settings_sanitize_key_config(i, &current_settings.keys[i]);
    }
    settings_sanitize_layer_keycodes();
    settings_gamepad_sanitize(&current_settings.gamepad);
    settings_gamepad_apply_keyboard_routing_option();
    settings_rotary_encoder_sanitize(&current_settings.rotary);
    current_settings.advanced_tick_rate = settings_sanitize_advanced_tick_rate(
        current_settings.advanced_tick_rate);
    settings_reset_led_effect_restore_state();
    return true;
  }

  if (!flash_storage_read(0, &legacy_v14, sizeof(settings_v14_t))) {
    return false;
  }

  if (settings_validate_v14(&legacy_v14)) {
    settings_migrate_v14(&legacy_v14);
    if (migrated != NULL) {
      *migrated = true;
    }
    return true;
  }

  if (!flash_storage_read(0, &legacy_v13, sizeof(settings_v13_t))) {
    return false;
  }

  if (settings_validate_v13(&legacy_v13)) {
    settings_migrate_v13(&legacy_v13);
    if (migrated != NULL) {
      *migrated = true;
    }
    return true;
  }

  if (!flash_storage_read(0, &legacy_v12, sizeof(settings_v12_t))) {
    return false;
  }

  if (settings_validate_v12(&legacy_v12)) {
    settings_migrate_v12(&legacy_v12);
    if (migrated != NULL) {
      *migrated = true;
    }
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
  gamepad_hid_reload_settings();

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
  settings_reset_led_effect_restore_state();

  settings_dirty = false;
  settings_change_counter = 0u;
  settings_last_seen_change_counter = 0u;
  settings_last_change_ms = 0u;
}

const settings_t *settings_get(void) { return &current_settings; }

bool settings_is_keyboard_enabled(void) {
  return current_settings.options.keyboard_enabled;
}

bool settings_is_gamepad_enabled(void) {
  return current_settings.options.gamepad_enabled != 0u;
}

bool settings_set_gamepad_enabled_live(bool enabled) {
  current_settings.options.gamepad_enabled = enabled ? 1u : 0u;
  gamepad_hid_set_enabled(enabled);
  gamepad_hid_reload_settings();
  settings_mark_dirty();
  return true;
}

bool settings_set_keyboard_enabled(bool enabled) {
  current_settings.options.keyboard_enabled = enabled ? 1 : 0;
  settings_mark_dirty();
  return true;
}

bool settings_set_gamepad_enabled(bool enabled) {
  return settings_set_gamepad_enabled_live(enabled);
}

bool settings_is_nkro_enabled(void) {
  return current_settings.options.nkro_enabled;
}

bool settings_set_nkro_enabled(bool enabled) {
  current_settings.options.nkro_enabled = enabled ? 1 : 0;
  // Note: changing NKRO mode requires USB re-enumeration to take effect
  settings_mark_dirty();
  return true;
}

bool settings_set_options(settings_options_t options) {
  current_settings.options = options;
  settings_gamepad_apply_keyboard_routing_option();
  gamepad_hid_set_enabled(options.gamepad_enabled);
  gamepad_hid_reload_settings();
  settings_mark_dirty();
  return true;
}

settings_options_t settings_get_options(void) {
  return current_settings.options;
}

bool settings_reset(void) {
  settings_set_defaults();
  gamepad_hid_set_enabled(current_settings.options.gamepad_enabled);
  gamepad_hid_reload_settings();
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

  // Append a new settings snapshot. The storage backend consolidates only
  // when the reserved flash sector is full, which keeps normal saves cheap.
  if (!flash_storage_write(0, &current_settings, sizeof(settings_t))) {
    return false;
  }

  settings_dirty = false;
  settings_last_seen_change_counter = settings_change_counter;
  return true;
}

void settings_task(uint32_t now_ms) {
  if (settings_change_counter != settings_last_seen_change_counter) {
    settings_last_seen_change_counter = settings_change_counter;
    settings_last_change_ms = now_ms;
    settings_dirty = true;
  }

  if (!settings_dirty) {
    return;
  }

  if (calibration_guided_is_active()) {
    return;
  }

  if ((uint32_t)(now_ms - settings_last_change_ms) < SETTINGS_AUTOSAVE_DELAY_MS) {
    return;
  }

  (void)settings_save();
}

bool settings_has_unsaved_changes(void) { return settings_dirty; }

uint16_t settings_get_firmware_version(void) { return FIRMWARE_VERSION; }

uint8_t settings_get_advanced_tick_rate(void) {
  return settings_sanitize_advanced_tick_rate(current_settings.advanced_tick_rate);
}

bool settings_set_advanced_tick_rate(uint8_t tick_rate) {
  current_settings.advanced_tick_rate =
      settings_sanitize_advanced_tick_rate(tick_rate);
  trigger_reload_settings();
  settings_mark_dirty();
  return true;
}

//--------------------------------------------------------------------+
// LED Matrix Settings API
//--------------------------------------------------------------------+

bool settings_is_led_enabled(void) {
  return current_settings.options.led_enabled;
}

bool settings_set_led_enabled(bool enabled) {
  current_settings.options.led_enabled = enabled ? 1 : 0;
  led_matrix_set_enabled(enabled);
  settings_mark_dirty();
  return true;
}

uint8_t settings_get_led_brightness(void) {
  return current_settings.led.brightness;
}

bool settings_set_led_brightness(uint8_t brightness) {
  current_settings.led.brightness = brightness;
  led_matrix_set_brightness(brightness);
  settings_mark_dirty();
  return true;
}

const uint8_t *settings_get_led_pixels(void) {
  return current_settings.led.pixels;
}

bool settings_set_led_pixels(const uint8_t *pixels) {
  if (pixels == NULL)
    return false;

  memcpy(current_settings.led.pixels, pixels, LED_MATRIX_DATA_BYTES);
  led_matrix_set_raw_data(pixels);
  settings_mark_dirty();
  return true;
}

bool settings_set_led_pixel(uint8_t index, uint8_t r, uint8_t g, uint8_t b) {
  if (index >= LED_MATRIX_SIZE)
    return false;

  current_settings.led.pixels[index * 3 + 0] = r;
  current_settings.led.pixels[index * 3 + 1] = g;
  current_settings.led.pixels[index * 3 + 2] = b;

  led_matrix_set_pixel_idx(index, r, g, b);
  settings_mark_dirty();
  return true;
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

  if (mode == LED_EFFECT_THIRD_PARTY) {
    if (current_settings.led_effect_mode != LED_EFFECT_THIRD_PARTY) {
      led_effect_restore_mode = current_settings.led_effect_mode;
    }
    led_effect_restore_valid = true;
  } else {
    led_effect_restore_mode = mode;
    led_effect_restore_valid = false;
  }

  current_settings.led_effect_mode = mode;
  led_matrix_set_effect((led_effect_mode_t)mode);
  led_matrix_set_effect_params(current_settings.led_effect_params[mode]);
  settings_mark_dirty();
  return true;
}

uint8_t settings_get_led_effect_speed(void) {
  return current_settings.led_effect_speed;
}

bool settings_set_led_effect_speed(uint8_t speed) {
  current_settings.led_effect_speed = speed;
  led_matrix_set_effect_speed(speed);
  settings_mark_dirty();
  return true;
}

uint8_t settings_get_led_fps_limit(void) {
  return current_settings.led_fps_limit;
}

bool settings_set_led_fps_limit(uint8_t fps_limit) {
  current_settings.led_fps_limit = fps_limit;
  led_matrix_set_fps_limit(fps_limit);
  settings_mark_dirty();
  return true;
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
  settings_mark_dirty();
  return true;
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

  settings_mark_dirty();
  return true;
}

bool settings_restore_led_effect_before_third_party(void) {
  if (!led_effect_restore_valid) {
    return false;
  }

  return settings_set_led_effect_mode(led_effect_restore_mode);
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
  settings_mark_dirty();
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
  settings_mark_dirty();
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
  gamepad_hid_reload_settings();
  settings_mark_dirty();
  return true;
}

uint16_t settings_get_layer_keycode(uint8_t layer_index, uint8_t key_index) {
  if (key_index >= NUM_KEYS || layer_index >= SETTINGS_LAYER_COUNT) {
    return KC_NO;
  }

  if (layer_index == 0u) {
    return current_settings.keys[key_index].hid_keycode;
  }

  return current_settings.layer_keycodes[layer_index - 1u][key_index];
}

bool settings_set_layer_keycode(uint8_t layer_index, uint8_t key_index,
                                uint16_t keycode) {
  settings_key_t key = {0};

  if (key_index >= NUM_KEYS || layer_index >= SETTINGS_LAYER_COUNT) {
    return false;
  }

  if (layer_index == 0u) {
    const settings_key_t *current_key = settings_get_key(key_index);
    if (current_key == NULL) {
      return false;
    }

    key = *current_key;
    key.hid_keycode = keycode;
    return settings_set_key(key_index, &key);
  }

  current_settings.layer_keycodes[layer_index - 1u][key_index] = keycode;
  settings_mark_dirty();
  return true;
}

//--------------------------------------------------------------------+
// Gamepad Settings API
//--------------------------------------------------------------------+

const settings_gamepad_t *settings_get_gamepad(void) {
  return &current_settings.gamepad;
}

bool settings_set_gamepad(const settings_gamepad_t *gamepad) {
  if (gamepad == NULL) {
    return false;
  }

  memcpy(&current_settings.gamepad, gamepad, sizeof(settings_gamepad_t));
  settings_gamepad_sanitize(&current_settings.gamepad);
  settings_gamepad_apply_keyboard_routing_option();
  gamepad_hid_reload_settings();
  settings_mark_dirty();
  return true;
}

gamepad_api_mode_t settings_get_gamepad_api_mode(void) {
  if (current_settings.gamepad.api_mode >= (uint8_t)GAMEPAD_API_MAX) {
    return GAMEPAD_API_HID;
  }

  return (gamepad_api_mode_t)current_settings.gamepad.api_mode;
}

bool settings_set_gamepad_api_mode(gamepad_api_mode_t mode) {
  if ((uint8_t)mode >= (uint8_t)GAMEPAD_API_MAX) {
    return false;
  }

  current_settings.gamepad.api_mode = (uint8_t)mode;
  gamepad_hid_reload_settings();
  settings_mark_dirty();
  return true;
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
  settings_mark_dirty();
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
  settings_mark_dirty();
  return true;
}

bool settings_set_key_calibration(uint8_t key_index, int16_t zero_value) {
  if (key_index >= NUM_KEYS)
    return false;

  current_settings.calibration.key_zero_values[key_index] = zero_value;
  calibration_load_settings();
  settings_mark_dirty();
  return true;
}

bool settings_set_key_calibration_max(uint8_t key_index, int16_t max_value) {
  if (key_index >= NUM_KEYS) {
    return false;
  }

  current_settings.calibration.key_max_values[key_index] = max_value;
  calibration_load_settings();
  settings_mark_dirty();
  return true;
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
  settings_mark_dirty();
  return true;
}

bool settings_set_key_curve_enabled(uint8_t key_index, bool enabled) {
  if (key_index >= NUM_KEYS)
    return false;

  current_settings.keys[key_index].curve_enabled = enabled ? 1 : 0;
  settings_mark_dirty();
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

uint8_t settings_gamepad_apply_curve(uint16_t distance_01mm) {
  const settings_gamepad_t *gamepad = &current_settings.gamepad;
  const settings_gamepad_curve_point_t *curve = gamepad->curve;

  if (distance_01mm <= curve[0].x_01mm) {
    return curve[0].y;
  }

  for (uint8_t i = 1; i < GAMEPAD_CURVE_POINT_COUNT; i++) {
    uint16_t x0 = curve[i - 1u].x_01mm;
    uint16_t x1 = curve[i].x_01mm;
    uint8_t y0 = curve[i - 1u].y;
    uint8_t y1 = curve[i].y;

    if (distance_01mm <= x1) {
      if (x1 <= x0) {
        return y1;
      }

      uint32_t delta_x = (uint32_t)(x1 - x0);
      uint32_t delta_input = (uint32_t)(distance_01mm - x0);
      int32_t delta_y = (int32_t)y1 - (int32_t)y0;
      int32_t interpolated =
          (int32_t)y0 +
          (int32_t)(((int64_t)delta_y * (int64_t)delta_input +
                     (int64_t)(delta_x / 2u)) /
                    (int64_t)delta_x);

      if (interpolated < 0) {
        return 0u;
      }
      if (interpolated > 255) {
        return 255u;
      }
      return (uint8_t)interpolated;
    }
  }

  return curve[GAMEPAD_CURVE_POINT_COUNT - 1u].y;
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
  settings_gamepad_sanitize_mapping(&current_settings.keys[key_index].gamepad_map);
  gamepad_hid_reload_settings();
  settings_mark_dirty();
  return true;
}

bool settings_is_gamepad_with_keyboard(void) {
  return current_settings.gamepad.keyboard_routing !=
         (uint8_t)GAMEPAD_KEYBOARD_ROUTING_DISABLED;
}

bool settings_set_gamepad_with_keyboard(bool enabled) {
  current_settings.gamepad.keyboard_routing =
      enabled ? (uint8_t)GAMEPAD_KEYBOARD_ROUTING_ALL_KEYS
              : (uint8_t)GAMEPAD_KEYBOARD_ROUTING_DISABLED;
  settings_gamepad_apply_keyboard_routing_option();
  gamepad_hid_reload_settings();
  settings_mark_dirty();
  return true;
}

bool settings_is_key_mapped_to_gamepad(uint8_t key_index) {
  const settings_gamepad_mapping_t *mapping = NULL;

  if (key_index >= NUM_KEYS) {
    return false;
  }

  mapping = &current_settings.keys[key_index].gamepad_map;
  return mapping->axis != (uint8_t)GAMEPAD_AXIS_NONE ||
         mapping->button != (uint8_t)GAMEPAD_BUTTON_NONE;
}
