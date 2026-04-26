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
#include "hid/keyboard_hid.h"
#include "hid/keyboard_nkro_hid.h"
#include <string.h>

//--------------------------------------------------------------------+
// Firmware Version (semver: major.minor.patch)
//--------------------------------------------------------------------+
#define FIRMWARE_VERSION_MAJOR 2u
#define FIRMWARE_VERSION_MINOR 0u
#define FIRMWARE_VERSION_PATCH 2u
#define FIRMWARE_VERSION_PACKED                                                \
  (((uint32_t)FIRMWARE_VERSION_MAJOR << 16) |                                  \
   ((uint32_t)FIRMWARE_VERSION_MINOR << 8) |                                   \
   ((uint32_t)FIRMWARE_VERSION_PATCH))

#define KBHE_FW_VERSION_RECORD_MAGIC 0x4B465756u

typedef struct __attribute__((packed)) {
  uint32_t magic;
  uint32_t version_packed; // (major << 16) | (minor << 8) | patch
  uint32_t version_xor;    // version_packed ^ 0xFFFFFFFFu
} kbhe_fw_version_record_t;

__attribute__((used, section(".kbhe_fw_version")))
static const kbhe_fw_version_record_t g_kbhe_fw_version_record = {
    .magic = KBHE_FW_VERSION_RECORD_MAGIC,
    .version_packed = FIRMWARE_VERSION_PACKED,
    .version_xor = (uint32_t)(FIRMWARE_VERSION_PACKED ^ 0xFFFFFFFFu),
};

//--------------------------------------------------------------------+
// Internal Variables
//--------------------------------------------------------------------+

// RAM cache of current settings
static settings_t current_settings;

// Flag indicating if settings have been modified
static bool settings_dirty = false;
static bool settings_save_requested = false;
static bool settings_save_in_progress = false;
static uint32_t settings_save_snapshot_change_counter = 0u;
static settings_t settings_save_snapshot;
// When true, explicit flash saves are rejected (inactive/RAM-only profile active)
static bool settings_ram_only_mode = false;
static uint32_t settings_change_counter = 0u;
static uint32_t settings_last_seen_change_counter = 0u;
static uint32_t settings_last_change_ms = 0u;
static char settings_keyboard_name_cache[SETTINGS_KEYBOARD_NAME_LENGTH + 1u];
static char settings_profile_name_cache[SETTINGS_PROFILE_COUNT]
                                      [SETTINGS_PROFILE_NAME_LENGTH + 1u];

#define SETTINGS_AUTOSAVE_DELAY_MS 750u
#define SETTINGS_REQUEST_SAVE_DELAY_MS 100u
#define SETTINGS_FLASH_WORDS_PER_STEP 16u
#define SETTINGS_PROFILE_MASK_ALL ((1u << SETTINGS_PROFILE_COUNT) - 1u)
#define SETTINGS_LED_META_TRIGGER_CHATTER_GUARD_DURATION_INDEX 0u
#define SETTINGS_LED_META_FLAGS_INDEX 1u
#define SETTINGS_LED_META_LED_IDLE_TIMEOUT_SECONDS_INDEX 2u
#define SETTINGS_LED_META_FLAG_TRIGGER_CHATTER_GUARD_ENABLED 0x01u
#define SETTINGS_LED_META_FLAG_USB_SUSPEND_RGB_OFF 0x02u
#define SETTINGS_LED_META_FLAGS_ALLOWED_MASK                                    \
  (SETTINGS_LED_META_FLAG_TRIGGER_CHATTER_GUARD_ENABLED |                     \
   SETTINGS_LED_META_FLAG_USB_SUSPEND_RGB_OFF)

static uint8_t led_effect_restore_mode = LED_EFFECT_STATIC_MATRIX;
static bool led_effect_restore_valid = false;

static settings_key_t settings_default_key(uint8_t key_index);
static void settings_default_effect_params(uint8_t effect_mode,
                                           uint8_t *params);
static uint8_t settings_sanitize_effect_speed(uint8_t speed);
static void settings_sanitize_options(settings_options_t *options);

static void settings_default_rotary_binding(settings_rotary_binding_t *binding) {
  if (binding == NULL) {
    return;
  }

  binding->mode = (uint8_t)ROTARY_BINDING_MODE_INTERNAL;
  binding->keycode = KC_NO;
  binding->modifier_mask_exact = 0u;
  binding->fallback_no_mod_keycode = KC_NO;
  binding->layer_mode = (uint8_t)ROTARY_BINDING_LAYER_ACTIVE;
  binding->layer_index = 0u;
}

static void settings_sanitize_rotary_binding(
    settings_rotary_binding_t *binding,
    const settings_rotary_binding_t *defaults) {
  if (binding == NULL || defaults == NULL) {
    return;
  }

  if (binding->mode >= (uint8_t)ROTARY_BINDING_MODE_MAX) {
    binding->mode = defaults->mode;
  }

  if (binding->keycode == 0xFFFFu) {
    binding->keycode = defaults->keycode;
  }

  if (binding->fallback_no_mod_keycode == 0xFFFFu) {
    binding->fallback_no_mod_keycode = defaults->fallback_no_mod_keycode;
  }

  if (binding->layer_mode >= (uint8_t)ROTARY_BINDING_LAYER_MAX) {
    binding->layer_mode = defaults->layer_mode;
  }

  if (binding->layer_index >= SETTINGS_LAYER_COUNT) {
    binding->layer_index = defaults->layer_index;
  }
}

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
  rotary->rgb_effect_mode = LED_EFFECT_SOLID_COLOR;
  rotary->progress_style = ROTARY_PROGRESS_STYLE_SOLID;
  rotary->progress_effect_mode = LED_EFFECT_CYCLE_LEFT_RIGHT;
  rotary->progress_color_r = 40u;
  rotary->progress_color_g = 210u;
  rotary->progress_color_b = 64u;
  settings_default_rotary_binding(&rotary->cw_binding);
  settings_default_rotary_binding(&rotary->ccw_binding);
  settings_default_rotary_binding(&rotary->click_binding);
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

  settings_gamepad_mapping_set_layer_mask(
      mapping, settings_gamepad_mapping_get_layer_mask(mapping));
}

static void settings_default_key_advanced(settings_key_advanced_t *advanced,
                                          uint16_t primary_hid_keycode) {
  if (advanced == NULL) {
    return;
  }

  memset(advanced, 0, sizeof(*advanced));
  advanced->behavior_mode = (uint8_t)KEY_BEHAVIOR_NORMAL;
  advanced->hold_threshold_10ms = 20u;
  advanced->dks_bottom_out_point_tenths = SETTINGS_DKS_BOTTOM_OUT_POINT_DEFAULT_TENTHS;
  advanced->secondary_hid_keycode = KC_NO;
  advanced->dynamic_zones[0].end_mm_tenths = 0x81u;
  advanced->dynamic_zones[0].hid_keycode = primary_hid_keycode;
  for (uint8_t i = 1u; i < SETTINGS_DYNAMIC_ZONE_COUNT; i++) {
    advanced->dynamic_zones[i].end_mm_tenths = 0u;
    advanced->dynamic_zones[i].hid_keycode = KC_NO;
  }
  advanced->socd_fully_pressed_point_tenths =
      SETTINGS_SOCD_FULLY_PRESSED_POINT_DEFAULT_TENTHS;
}

static void settings_sanitize_key_advanced(uint16_t primary_hid_keycode,
                                           settings_key_t *key) {
  settings_key_advanced_t defaults = {0};
  bool has_any_dks_keycode = false;
  bool has_any_dks_action = false;

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

  if (key->advanced.dks_bottom_out_point_tenths <
          SETTINGS_DKS_BOTTOM_OUT_POINT_MIN_TENTHS ||
      key->advanced.dks_bottom_out_point_tenths >
          SETTINGS_DKS_BOTTOM_OUT_POINT_MAX_TENTHS) {
    key->advanced.dks_bottom_out_point_tenths = defaults.dks_bottom_out_point_tenths;
  }

  key->advanced.reserved &=
      (SETTINGS_KEY_ADV_TAP_HOLD_HOLD_ON_OTHER_MASK |
       SETTINGS_KEY_ADV_TAP_HOLD_UPPERCASE_HOLD_MASK);

  if (key->advanced.socd_fully_pressed_point_tenths <
          SETTINGS_SOCD_FULLY_PRESSED_POINT_MIN_TENTHS ||
      key->advanced.socd_fully_pressed_point_tenths >
          SETTINGS_SOCD_FULLY_PRESSED_POINT_MAX_TENTHS) {
    key->advanced.socd_fully_pressed_point_tenths =
        defaults.socd_fully_pressed_point_tenths;
  }

  for (uint8_t i = 0u; i < SETTINGS_DYNAMIC_ZONE_COUNT; i++) {
    if (key->advanced.dynamic_zones[i].hid_keycode != KC_NO) {
      has_any_dks_keycode = true;
    }
    if (key->advanced.dynamic_zones[i].end_mm_tenths != 0u) {
      has_any_dks_action = true;
    }
  }

  if (!has_any_dks_keycode || !has_any_dks_action) {
    memcpy(key->advanced.dynamic_zones, defaults.dynamic_zones,
           sizeof(key->advanced.dynamic_zones));
  }

  if (key->advanced.dynamic_zones[0].hid_keycode == KC_NO) {
    key->advanced.dynamic_zones[0].hid_keycode = primary_hid_keycode;
  }

  if (key->advanced.dynamic_zones[0].end_mm_tenths == 0u) {
    key->advanced.dynamic_zones[0].end_mm_tenths =
        defaults.dynamic_zones[0].end_mm_tenths;
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

static uint8_t settings_sanitize_advanced_tick_rate(uint8_t tick_rate) {
  if (tick_rate < SETTINGS_ADVANCED_TICK_RATE_MIN) {
    return SETTINGS_ADVANCED_TICK_RATE_MIN;
  }

  if (tick_rate > SETTINGS_ADVANCED_TICK_RATE_MAX) {
    return SETTINGS_ADVANCED_TICK_RATE_MAX;
  }

  return tick_rate;
}

static uint8_t
settings_sanitize_trigger_chatter_guard_duration(uint8_t duration_ms) {
  if (duration_ms > SETTINGS_TRIGGER_CHATTER_GUARD_MAX_MS) {
    return SETTINGS_DEFAULT_TRIGGER_CHATTER_GUARD_MS;
  }

  return duration_ms;
}

static uint8_t
settings_sanitize_trigger_chatter_guard_enabled_raw(uint8_t enabled_raw) {
  if (enabled_raw > 1u) {
    return (uint8_t)(SETTINGS_DEFAULT_TRIGGER_CHATTER_GUARD_ENABLED ? 1u : 0u);
  }

  return enabled_raw;
}

static uint8_t settings_default_led_meta_flags(void) {
  uint8_t flags = 0u;

  if (SETTINGS_DEFAULT_TRIGGER_CHATTER_GUARD_ENABLED != 0u) {
    flags = (uint8_t)(flags | SETTINGS_LED_META_FLAG_TRIGGER_CHATTER_GUARD_ENABLED);
  }

  if (SETTINGS_DEFAULT_LED_USB_SUSPEND_RGB_OFF != 0u) {
    flags = (uint8_t)(flags | SETTINGS_LED_META_FLAG_USB_SUSPEND_RGB_OFF);
  }

  return flags;
}

static uint8_t settings_sanitize_led_meta_flags_raw(uint8_t flags_raw) {
  return (uint8_t)(flags_raw & SETTINGS_LED_META_FLAGS_ALLOWED_MASK);
}

static uint8_t settings_read_led_meta_flags(const settings_led_t *led) {
  if (led == NULL) {
    return settings_default_led_meta_flags();
  }

  return settings_sanitize_led_meta_flags_raw(
      led->reserved[SETTINGS_LED_META_FLAGS_INDEX]);
}

static void settings_write_led_meta_flags(settings_led_t *led, uint8_t flags) {
  if (led == NULL) {
    return;
  }

  led->reserved[SETTINGS_LED_META_FLAGS_INDEX] =
      settings_sanitize_led_meta_flags_raw(flags);
}

static uint8_t settings_sanitize_led_idle_timeout_seconds(
    uint8_t timeout_seconds) {
  if (timeout_seconds > SETTINGS_LED_IDLE_TIMEOUT_MAX_SECONDS) {
    return SETTINGS_DEFAULT_LED_IDLE_TIMEOUT_SECONDS;
  }

  return timeout_seconds;
}

static void settings_write_trigger_chatter_guard_to_led(settings_led_t *led,
                                                        bool enabled,
                                                        uint8_t duration_ms) {
  uint8_t flags = 0u;

  if (led == NULL) {
    return;
  }

  led->reserved[SETTINGS_LED_META_TRIGGER_CHATTER_GUARD_DURATION_INDEX] =
      settings_sanitize_trigger_chatter_guard_duration(duration_ms);

  flags = settings_read_led_meta_flags(led);
  if (enabled) {
    flags = (uint8_t)(flags | SETTINGS_LED_META_FLAG_TRIGGER_CHATTER_GUARD_ENABLED);
  } else {
    flags =
        (uint8_t)(flags & (uint8_t)~SETTINGS_LED_META_FLAG_TRIGGER_CHATTER_GUARD_ENABLED);
  }
  settings_write_led_meta_flags(led, flags);
}

static void settings_read_trigger_chatter_guard_from_led(
    const settings_led_t *led, bool *enabled, uint8_t *duration_ms) {
  uint8_t flags = settings_default_led_meta_flags();
  uint8_t enabled_raw =
      (uint8_t)(SETTINGS_DEFAULT_TRIGGER_CHATTER_GUARD_ENABLED ? 1u : 0u);
  uint8_t duration_raw = SETTINGS_DEFAULT_TRIGGER_CHATTER_GUARD_MS;

  if (led != NULL) {
    flags = settings_read_led_meta_flags(led);
    enabled_raw = (flags & SETTINGS_LED_META_FLAG_TRIGGER_CHATTER_GUARD_ENABLED)
                      ? 1u
                      : 0u;
    duration_raw =
        led->reserved[SETTINGS_LED_META_TRIGGER_CHATTER_GUARD_DURATION_INDEX];
  }

  enabled_raw = settings_sanitize_trigger_chatter_guard_enabled_raw(enabled_raw);
  duration_raw = settings_sanitize_trigger_chatter_guard_duration(duration_raw);

  if (enabled != NULL) {
    *enabled = enabled_raw != 0u;
  }

  if (duration_ms != NULL) {
    *duration_ms = duration_raw;
  }
}

static void settings_sanitize_trigger_chatter_guard_storage(settings_led_t *led) {
  bool enabled = false;
  uint8_t duration_ms = SETTINGS_DEFAULT_TRIGGER_CHATTER_GUARD_MS;

  if (led == NULL) {
    return;
  }

  settings_read_trigger_chatter_guard_from_led(led, &enabled, &duration_ms);
  settings_write_trigger_chatter_guard_to_led(led, enabled, duration_ms);
}

static bool settings_read_led_usb_suspend_rgb_off_from_led(
    const settings_led_t *led) {
  uint8_t flags = settings_read_led_meta_flags(led);
  return (flags & SETTINGS_LED_META_FLAG_USB_SUSPEND_RGB_OFF) != 0u;
}

static void settings_write_led_usb_suspend_rgb_off_to_led(settings_led_t *led,
                                                          bool enabled) {
  uint8_t flags = 0u;

  if (led == NULL) {
    return;
  }

  flags = settings_read_led_meta_flags(led);
  if (enabled) {
    flags = (uint8_t)(flags | SETTINGS_LED_META_FLAG_USB_SUSPEND_RGB_OFF);
  } else {
    flags =
        (uint8_t)(flags & (uint8_t)~SETTINGS_LED_META_FLAG_USB_SUSPEND_RGB_OFF);
  }
  settings_write_led_meta_flags(led, flags);
}

static void settings_sanitize_led_usb_suspend_rgb_off_storage(settings_led_t *led) {
  bool enabled = false;

  if (led == NULL) {
    return;
  }

  enabled = settings_read_led_usb_suspend_rgb_off_from_led(led);
  settings_write_led_usb_suspend_rgb_off_to_led(led, enabled);
}

static uint8_t
settings_read_led_idle_timeout_seconds_from_led(const settings_led_t *led) {
  if (led == NULL) {
    return SETTINGS_DEFAULT_LED_IDLE_TIMEOUT_SECONDS;
  }

  return settings_sanitize_led_idle_timeout_seconds(
      led->reserved[SETTINGS_LED_META_LED_IDLE_TIMEOUT_SECONDS_INDEX]);
}

static void settings_write_led_idle_timeout_seconds_to_led(settings_led_t *led,
                                                           uint8_t timeout_seconds) {
  if (led == NULL) {
    return;
  }

  led->reserved[SETTINGS_LED_META_LED_IDLE_TIMEOUT_SECONDS_INDEX] =
      settings_sanitize_led_idle_timeout_seconds(timeout_seconds);
}

static void settings_sanitize_led_idle_timeout_storage(settings_led_t *led) {
  uint8_t timeout_seconds = SETTINGS_DEFAULT_LED_IDLE_TIMEOUT_SECONDS;

  if (led == NULL) {
    return;
  }

  timeout_seconds = settings_read_led_idle_timeout_seconds_from_led(led);
  settings_write_led_idle_timeout_seconds_to_led(led, timeout_seconds);
}

static bool settings_options_led_system_when_disabled(
    const settings_options_t *options) {
  uint8_t reserved = 0u;

  if (options == NULL) {
    return SETTINGS_DEFAULT_LED_ALLOW_SYSTEM_WHEN_DISABLED != 0u;
  }

  reserved = (uint8_t)(options->reserved &
                       SETTINGS_OPTIONS_RESERVED_LED_SYSTEM_WHEN_DISABLED_MASK);
  return reserved != 0u;
}

static bool settings_options_led_idle_third_party_stream_activity(
    const settings_options_t *options) {
  uint8_t reserved = 0u;

  if (options == NULL) {
    return SETTINGS_DEFAULT_LED_IDLE_THIRD_PARTY_STREAM_ACTIVITY != 0u;
  }

  reserved =
      (uint8_t)(options->reserved &
                SETTINGS_OPTIONS_RESERVED_LED_IDLE_THIRD_PARTY_STREAM_ACTIVITY_MASK);
  return reserved != 0u;
}

static void settings_options_set_led_system_when_disabled(
    settings_options_t *options, bool enabled) {
  uint8_t reserved = 0u;

  if (options == NULL) {
    return;
  }

  reserved = (uint8_t)(options->reserved & SETTINGS_OPTIONS_RESERVED_ALLOWED_MASK);
  if (enabled) {
    reserved = (uint8_t)(reserved |
                         SETTINGS_OPTIONS_RESERVED_LED_SYSTEM_WHEN_DISABLED_MASK);
  } else {
    reserved = (uint8_t)(reserved &
                         (uint8_t)~SETTINGS_OPTIONS_RESERVED_LED_SYSTEM_WHEN_DISABLED_MASK);
  }

  options->reserved = reserved;
}

static void settings_options_set_led_idle_third_party_stream_activity(
    settings_options_t *options, bool enabled) {
  uint8_t reserved = 0u;

  if (options == NULL) {
    return;
  }

  reserved = (uint8_t)(options->reserved & SETTINGS_OPTIONS_RESERVED_ALLOWED_MASK);
  if (enabled) {
    reserved =
        (uint8_t)(reserved |
                  SETTINGS_OPTIONS_RESERVED_LED_IDLE_THIRD_PARTY_STREAM_ACTIVITY_MASK);
  } else {
    reserved =
        (uint8_t)(reserved &
                  (uint8_t)~SETTINGS_OPTIONS_RESERVED_LED_IDLE_THIRD_PARTY_STREAM_ACTIVITY_MASK);
  }

  options->reserved = reserved;
}

static void settings_sanitize_options(settings_options_t *options) {
  if (options == NULL) {
    return;
  }

  options->keyboard_enabled = options->keyboard_enabled ? 1u : 0u;
  options->gamepad_enabled = options->gamepad_enabled ? 1u : 0u;
  options->raw_hid_echo = options->raw_hid_echo ? 1u : 0u;
  options->led_enabled = options->led_enabled ? 1u : 0u;
  options->nkro_enabled = options->nkro_enabled ? 1u : 0u;

  if (options->led_thermal_protection_enabled != 0u) {
    options->led_thermal_protection_enabled = 1u;
  }

  options->reserved =
      (uint8_t)(options->reserved & SETTINGS_OPTIONS_RESERVED_ALLOWED_MASK);

  settings_options_set_led_system_when_disabled(
      options, settings_options_led_system_when_disabled(options));
    settings_options_set_led_idle_third_party_stream_activity(
      options, settings_options_led_idle_third_party_stream_activity(options));
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

  settings_sanitize_rotary_binding(&rotary->cw_binding, &defaults.cw_binding);
  settings_sanitize_rotary_binding(&rotary->ccw_binding,
                                   &defaults.ccw_binding);
  settings_sanitize_rotary_binding(&rotary->click_binding,
                                   &defaults.click_binding);
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
  params[LED_EFFECT_PARAM_SPEED] = 50u;
  params[LED_EFFECT_PARAM_COLOR_R] = 255u;
  params[LED_EFFECT_PARAM_COLOR_G] = 0u;
  params[LED_EFFECT_PARAM_COLOR_B] = 0u;
  params[LED_EFFECT_PARAM_COLOR2_R] = 0u;
  params[LED_EFFECT_PARAM_COLOR2_G] = 160u;
  params[LED_EFFECT_PARAM_COLOR2_B] = 255u;

  switch ((led_effect_mode_t)effect_mode) {
  // --- Kept classic effects ---
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
  case LED_EFFECT_SPARKLE:
    params[0] = 48u;  // Density
    params[1] = 224u; // Sparkle brightness
    params[2] = 160u; // Rainbow mix
    params[3] = 24u;  // Ambient glow
    params[LED_EFFECT_PARAM_COLOR_R] = 255u;
    params[LED_EFFECT_PARAM_COLOR_G] = 210u;
    params[LED_EFFECT_PARAM_COLOR_B] = 96u;
    break;
  case LED_EFFECT_BREATHING_RAINBOW:
    params[0] = 24u;  // Brightness floor
    params[1] = 192u; // Hue drift
    params[2] = 255u; // Saturation
    break;
  case LED_EFFECT_COLOR_CYCLE:
    params[0] = 64u;  // Hue step
    params[1] = 255u; // Saturation
    params[2] = 255u; // Value
    params[3] = 0u;   // Effect-color mix
    break;
  case LED_EFFECT_DISTANCE_SENSOR:
    params[0] = 32u;  // Brightness floor
    params[1] = 170u; // Hue span
    params[2] = 255u; // Saturation
    params[3] = 0u;   // Reverse gradient
    break;
  case LED_EFFECT_IMPACT_RAINBOW:
    params[0] = 2u;   // Boost mode (0=speed, 1=white, 2=both)
    params[1] = 184u; // Boost decay
    params[2] = 96u;  // Key boost
    params[3] = 88u;  // Audio boost
    params[4] = 208u; // Max boost
    params[5] = 16u;  // Angle
    params[6] = 255u; // Saturation
    params[7] = 168u; // Wave drift
    break;
  case LED_EFFECT_REACTIVE_GHOST:
    params[0] = 100u; // Decay
    params[1] = 156u; // Spread
    params[2] = 184u; // Trail
    params[3] = 208u; // Gain
    params[LED_EFFECT_PARAM_COLOR_R] = 72u;
    params[LED_EFFECT_PARAM_COLOR_G] = 180u;
    params[LED_EFFECT_PARAM_COLOR_B] = 255u;
    break;
  case LED_EFFECT_AUDIO_SPECTRUM:
    params[0] = 176u; // Hue span
    params[1] = 32u;  // Base floor
    params[2] = 208u; // Peak gain
    params[3] = 1u;   // Mirror
    params[4] = 172u; // Decay
    params[LED_EFFECT_PARAM_COLOR_R] = 64u;
    params[LED_EFFECT_PARAM_COLOR_G] = 255u;
    params[LED_EFFECT_PARAM_COLOR_B] = 160u;
    break;
  case LED_EFFECT_KEY_STATE_DEMO:
    params[0] = 0u;   // Invert mapping
    params[1] = 255u; // Pressed brightness
    params[2] = 96u;  // Released brightness
    params[LED_EFFECT_PARAM_COLOR_R] = 0u;
    params[LED_EFFECT_PARAM_COLOR_G] = 255u;
    params[LED_EFFECT_PARAM_COLOR_B] = 0u;
    params[LED_EFFECT_PARAM_COLOR2_R] = 255u;
    params[LED_EFFECT_PARAM_COLOR2_G] = 0u;
    params[LED_EFFECT_PARAM_COLOR2_B] = 0u;
    break;
  // --- Fused / renamed effects (baseline: former MX visual) ---
  case LED_EFFECT_SOLID_COLOR:
    params[0] = 255u; // Brightness trim
    break;
  case LED_EFFECT_BREATHING:
    params[0] = 24u;  // Brightness floor
    params[1] = 255u; // Brightness ceiling
    params[2] = 48u;  // Plateau
    break;
  case LED_EFFECT_GRADIENT_LEFT_RIGHT:
    params[0] = 160u; // Horizontal scale
    params[1] = 120u; // Vertical scale
    params[2] = 144u; // Saturation
    params[3] = 255u; // Value
    break;
  case LED_EFFECT_GRADIENT_UP_DOWN:
    params[0] = 160u; // Vertical hue spread
    params[1] = 144u; // Saturation
    params[2] = 255u; // Value
    break;
  case LED_EFFECT_HUE_BREATHING:
  case LED_EFFECT_HUE_PENDULUM:
    params[0] = 12u;  // Hue swing range
    break;
  case LED_EFFECT_HUE_WAVE:
    params[0] = 24u;  // Wave amplitude
    break;
  case LED_EFFECT_ALPHA_MODS:
    params[0] = 0u;   // Hue offset (0=dynamic via speed)
    break;
  case LED_EFFECT_STARLIGHT_DUAL_SAT:
    params[0] = 31u;  // Saturation spread
    break;
  case LED_EFFECT_STARLIGHT_DUAL_HUE:
    params[0] = 31u;  // Hue spread
    break;
  case LED_EFFECT_PIXEL_FRACTAL:
    params[0] = 255u; // Saturation
    params[1] = 255u; // Value
    break;
  case LED_EFFECT_CYCLE_LEFT_RIGHT:
    params[0] = 160u; // Horizontal hue spread
    params[1] = 96u;  // Vertical hue contribution
    // params[2]: removed (was drift multiplier — caused jump on uint8 time wrap)
    params[3] = 255u; // Saturation
    params[4] = 0u;   // Gradient tilt angle
    params[5] = 0u;   // Sinusoidal warp
    break;
  case LED_EFFECT_DIGITAL_RAIN:
    params[0] = 64u;  // Trail length
    params[1] = 160u; // Head size
    params[2] = 96u;  // Density
    params[3] = 1u;   // White heads
    params[4] = 0u;   // Hue bias
    break;
  case LED_EFFECT_TYPING_HEATMAP:
    params[0] = 224u; // Heat gain
    params[1] = 168u; // Decay
    params[2] = 88u;  // Diffusion
    params[3] = 48u;  // Floor
    break;
  case LED_EFFECT_SPLASH:
  case LED_EFFECT_MULTI_SPLASH:
    params[0] = 72u;  // Decay
    params[1] = 128u; // Spread
    params[2] = 8u;   // Base glow
    params[3] = 1u;   // White core
    params[4] = 224u; // Gain
    params[5] = 0u;   // Palette mode (0=custom, 1=rainbow)
    params[LED_EFFECT_PARAM_COLOR_R] = 0u;
    params[LED_EFFECT_PARAM_COLOR_G] = 255u;
    params[LED_EFFECT_PARAM_COLOR_B] = 96u;
    break;
  default:
    break;
  }
}

static uint8_t settings_sanitize_effect_speed(uint8_t speed) {
  return speed > 0u ? speed : 1u;
}

static void settings_normalize_led_effect_speeds(void) {
  for (uint8_t effect = 0u; effect < LED_EFFECT_MAX; effect++) {
    uint8_t speed =
        current_settings.led_effect_params[effect][LED_EFFECT_PARAM_SPEED];
    if (speed == 0u) {
      /* No stored speed — fetch the schema default for this effect. */
      uint8_t defaults[LED_EFFECT_PARAM_COUNT];
      memset(defaults, 0, sizeof(defaults));
      settings_default_effect_params(effect, defaults);
      speed = defaults[LED_EFFECT_PARAM_SPEED];
      if (speed == 0u) {
        speed = 50u; /* Fallback if schema default is also 0 */
      }
    }
    current_settings.led_effect_params[effect][LED_EFFECT_PARAM_SPEED] =
        settings_sanitize_effect_speed(speed);
  }
}

static void settings_sync_led_effect_speed_cache(void) {
  uint8_t effect_mode = current_settings.led_effect_mode;

  if (effect_mode >= LED_EFFECT_MAX) {
    effect_mode = (uint8_t)LED_EFFECT_SOLID_COLOR;
  }

  current_settings.led_effect_speed = settings_sanitize_effect_speed(
      current_settings.led_effect_params[effect_mode][LED_EFFECT_PARAM_SPEED]);
  current_settings.led_effect_params[effect_mode][LED_EFFECT_PARAM_SPEED] =
      current_settings.led_effect_speed;
}

static void settings_get_effect_color_from_params(uint8_t effect_mode,
                                                  uint8_t *r, uint8_t *g,
                                                  uint8_t *b) {
  const uint8_t *params = NULL;

  if (effect_mode >= (uint8_t)LED_EFFECT_MAX) {
    effect_mode = (uint8_t)LED_EFFECT_SOLID_COLOR;
  }

  params = current_settings.led_effect_params[effect_mode];
  if (r != NULL) {
    *r = params[LED_EFFECT_PARAM_COLOR_R];
  }
  if (g != NULL) {
    *g = params[LED_EFFECT_PARAM_COLOR_G];
  }
  if (b != NULL) {
    *b = params[LED_EFFECT_PARAM_COLOR_B];
  }
}

static void settings_set_effect_color_in_params(uint8_t effect_mode, uint8_t r,
                                                uint8_t g, uint8_t b) {
  if (effect_mode >= (uint8_t)LED_EFFECT_MAX) {
    return;
  }

  current_settings.led_effect_params[effect_mode][LED_EFFECT_PARAM_COLOR_R] = r;
  current_settings.led_effect_params[effect_mode][LED_EFFECT_PARAM_COLOR_G] = g;
  current_settings.led_effect_params[effect_mode][LED_EFFECT_PARAM_COLOR_B] = b;
  current_settings.led_effect_color_r = r;
  current_settings.led_effect_color_g = g;
  current_settings.led_effect_color_b = b;
}

static uint16_t settings_profile_layer_primary_keycode(
    const settings_profile_t *profile, uint8_t layer_index, uint8_t key_index) {
  uint16_t layer_keycode = KC_NO;

  if (profile == NULL || key_index >= NUM_KEYS) {
    return KC_NO;
  }

  if (layer_index == 0u || layer_index >= SETTINGS_LAYER_COUNT) {
    return profile->keys[key_index].hid_keycode;
  }

  layer_keycode = profile->layer_keycodes[layer_index - 1u][key_index];
  if (layer_keycode == KC_NO || layer_keycode == KC_TRANSPARENT) {
    return profile->keys[key_index].hid_keycode;
  }

  return layer_keycode;
}

static void settings_sanitize_profile_advanced_layers(settings_profile_t *profile) {
  if (profile == NULL) {
    return;
  }

  for (uint8_t key_index = 0u; key_index < NUM_KEYS; key_index++) {
    if (profile->advanced_by_layer[0u][key_index].hold_threshold_10ms == 0u) {
      profile->advanced_by_layer[0u][key_index] = profile->keys[key_index].advanced;
    }
  }

  for (uint8_t layer_index = 0u; layer_index < SETTINGS_LAYER_COUNT;
       layer_index++) {
    for (uint8_t key_index = 0u; key_index < NUM_KEYS; key_index++) {
      settings_key_t temp = profile->keys[key_index];
      temp.advanced = profile->advanced_by_layer[layer_index][key_index];
      settings_sanitize_key_advanced(
          settings_profile_layer_primary_keycode(profile, layer_index, key_index),
          &temp);
      profile->advanced_by_layer[layer_index][key_index] = temp.advanced;
    }
  }

  for (uint8_t key_index = 0u; key_index < NUM_KEYS; key_index++) {
    profile->keys[key_index].advanced = profile->advanced_by_layer[0u][key_index];
  }
}

static void settings_populate_profile_defaults(settings_profile_t *profile) {
  settings_rotary_encoder_t default_rotary = {0};

  if (profile == NULL) {
    return;
  }

  memset(profile, 0, sizeof(*profile));

  for (uint8_t key_index = 0u; key_index < NUM_KEYS; key_index++) {
    profile->keys[key_index] = settings_default_key(key_index);
  }

  for (uint8_t layer = 1u; layer < SETTINGS_LAYER_COUNT; layer++) {
    for (uint8_t key = 0u; key < NUM_KEYS; key++) {
      profile->layer_keycodes[layer - 1u][key] =
          layout_get_default_layer_keycode(layer, key);
    }
  }

  for (uint8_t key = 0u; key < NUM_KEYS; key++) {
    profile->advanced_by_layer[0u][key] = profile->keys[key].advanced;
  }

  for (uint8_t layer = 1u; layer < SETTINGS_LAYER_COUNT; layer++) {
    for (uint8_t key = 0u; key < NUM_KEYS; key++) {
      settings_default_key_advanced(
          &profile->advanced_by_layer[layer][key],
          settings_profile_layer_primary_keycode(profile, layer, key));
    }
  }

  settings_default_gamepad(&profile->gamepad);

  memset(profile->led.pixels, 0, LED_MATRIX_DATA_BYTES);
  profile->led.brightness = 50u;

  profile->led_effect_mode = (uint8_t)LED_EFFECT_CYCLE_LEFT_RIGHT;
  profile->led_fps_limit = 60u;
  for (uint8_t effect = 0u; effect < LED_EFFECT_MAX; effect++) {
    settings_default_effect_params(effect, profile->led_effect_params[effect]);
  }
  profile->led_effect_speed = settings_sanitize_effect_speed(
      profile->led_effect_params[profile->led_effect_mode]
                                [LED_EFFECT_PARAM_SPEED]);
  profile->led_effect_params[profile->led_effect_mode][LED_EFFECT_PARAM_SPEED] =
      profile->led_effect_speed;
  profile->led_effect_color_r =
      profile->led_effect_params[profile->led_effect_mode]
                                [LED_EFFECT_PARAM_COLOR_R];
  profile->led_effect_color_g =
      profile->led_effect_params[profile->led_effect_mode]
                                [LED_EFFECT_PARAM_COLOR_G];
  profile->led_effect_color_b =
      profile->led_effect_params[profile->led_effect_mode]
                                [LED_EFFECT_PARAM_COLOR_B];

  profile->filter_enabled = FILTER_DEFAULT_ENABLED;
  profile->filter_noise_band = FILTER_DEFAULT_NOISE_BAND;
  profile->filter_alpha_min = FILTER_DEFAULT_ALPHA_MIN_DENOM;
  profile->filter_alpha_max = FILTER_DEFAULT_ALPHA_MAX_DENOM;
  profile->advanced_tick_rate = SETTINGS_DEFAULT_ADVANCED_TICK_RATE;
  settings_write_trigger_chatter_guard_to_led(
      &profile->led, SETTINGS_DEFAULT_TRIGGER_CHATTER_GUARD_ENABLED != 0u,
      SETTINGS_DEFAULT_TRIGGER_CHATTER_GUARD_MS);
    settings_write_led_idle_timeout_seconds_to_led(
      &profile->led, SETTINGS_DEFAULT_LED_IDLE_TIMEOUT_SECONDS);

  settings_default_rotary_encoder(&default_rotary);
  profile->rotary = default_rotary;

  settings_sanitize_profile_advanced_layers(profile);
}

static void settings_profile_capture_current_slot(uint8_t profile_index) {
  settings_profile_t *profile = NULL;

  if (profile_index >= SETTINGS_PROFILE_COUNT) {
    return;
  }

  profile = &current_settings.profiles[profile_index];
  memcpy(profile->keys, current_settings.keys, sizeof(profile->keys));
  memcpy(profile->layer_keycodes, current_settings.layer_keycodes,
         sizeof(profile->layer_keycodes));
  for (uint8_t key_index = 0u; key_index < NUM_KEYS; key_index++) {
    profile->advanced_by_layer[0u][key_index] = current_settings.keys[key_index].advanced;
  }
  profile->gamepad = current_settings.gamepad;
  profile->led = current_settings.led;
  profile->led_effect_mode = current_settings.led_effect_mode;
  profile->led_effect_speed = current_settings.led_effect_speed;
  profile->led_effect_color_r = current_settings.led_effect_color_r;
  profile->led_effect_color_g = current_settings.led_effect_color_g;
  profile->led_effect_color_b = current_settings.led_effect_color_b;
  memcpy(profile->led_effect_params, current_settings.led_effect_params,
         sizeof(profile->led_effect_params));
  profile->led_fps_limit = current_settings.led_fps_limit;
  profile->filter_enabled = current_settings.filter_enabled;
  profile->filter_noise_band = current_settings.filter_noise_band;
  profile->filter_alpha_min = current_settings.filter_alpha_min;
  profile->filter_alpha_max = current_settings.filter_alpha_max;
  profile->advanced_tick_rate = current_settings.advanced_tick_rate;
  profile->rotary = current_settings.rotary;
  settings_sanitize_profile_advanced_layers(profile);
}

static void settings_profile_apply_slot(uint8_t profile_index) {
  const settings_profile_t *profile = NULL;

  if (profile_index >= SETTINGS_PROFILE_COUNT) {
    return;
  }

  profile = &current_settings.profiles[profile_index];
  memcpy(current_settings.keys, profile->keys, sizeof(current_settings.keys));
  memcpy(current_settings.layer_keycodes, profile->layer_keycodes,
         sizeof(current_settings.layer_keycodes));
  for (uint8_t key_index = 0u; key_index < NUM_KEYS; key_index++) {
    current_settings.keys[key_index].advanced =
        profile->advanced_by_layer[0u][key_index];
  }
  current_settings.gamepad = profile->gamepad;
  current_settings.led = profile->led;
  current_settings.led_effect_mode = profile->led_effect_mode;
  current_settings.led_effect_speed = profile->led_effect_speed;
  current_settings.led_effect_color_r = profile->led_effect_color_r;
  current_settings.led_effect_color_g = profile->led_effect_color_g;
  current_settings.led_effect_color_b = profile->led_effect_color_b;
  memcpy(current_settings.led_effect_params, profile->led_effect_params,
         sizeof(current_settings.led_effect_params));
  current_settings.led_fps_limit = profile->led_fps_limit;
  current_settings.filter_enabled = profile->filter_enabled;
  current_settings.filter_noise_band = profile->filter_noise_band;
  current_settings.filter_alpha_min = profile->filter_alpha_min;
  current_settings.filter_alpha_max = profile->filter_alpha_max;
  current_settings.advanced_tick_rate = profile->advanced_tick_rate;
  current_settings.rotary = profile->rotary;
  settings_sync_led_effect_speed_cache();
  settings_get_effect_color_from_params(current_settings.led_effect_mode,
                                        &current_settings.led_effect_color_r,
                                        &current_settings.led_effect_color_g,
                                        &current_settings.led_effect_color_b);
}

static void settings_profile_sync_active_slot(void) {
  uint8_t used_mask =
      (uint8_t)(current_settings.profile_used_mask & SETTINGS_PROFILE_MASK_ALL);

  if (current_settings.active_profile_index >= SETTINGS_PROFILE_COUNT) {
    return;
  }

  if ((used_mask & (uint8_t)(1u << current_settings.active_profile_index)) ==
      0u) {
    return;
  }

  settings_profile_capture_current_slot(current_settings.active_profile_index);
}

static void settings_apply_runtime_from_current_profile(void) {
  uint8_t effect_mode = current_settings.led_effect_mode;
  bool chatter_guard_enabled = false;
  uint8_t chatter_guard_duration_ms = 0u;
  uint8_t led_idle_timeout_seconds = 0u;

  if (effect_mode >= LED_EFFECT_MAX) {
    effect_mode = (uint8_t)LED_EFFECT_SOLID_COLOR;
    current_settings.led_effect_mode = effect_mode;
  }

  gamepad_hid_set_enabled(current_settings.options.gamepad_enabled != 0u);
  gamepad_hid_reload_settings();

  // LED matrix must already be initialized in main.c before this runs.
  led_matrix_set_brightness(current_settings.led.brightness);
  led_matrix_set_raw_data(current_settings.led.pixels);
  led_matrix_set_enabled(current_settings.options.led_enabled != 0u);
  led_matrix_set_allow_system_indicators_when_disabled(
      settings_options_led_system_when_disabled(&current_settings.options));
    led_matrix_set_idle_third_party_stream_counts_as_activity(
      settings_options_led_idle_third_party_stream_activity(
        &current_settings.options));
  led_idle_timeout_seconds =
      settings_read_led_idle_timeout_seconds_from_led(&current_settings.led);
  led_matrix_set_idle_timeout_seconds(led_idle_timeout_seconds);

  led_matrix_set_effect((led_effect_mode_t)effect_mode);
  led_matrix_set_effect_params(
      current_settings.led_effect_params[effect_mode]);
  settings_sync_led_effect_speed_cache();
  led_matrix_set_fps_limit(current_settings.led_fps_limit);

  filter_set_params(current_settings.filter_noise_band,
                    current_settings.filter_alpha_min,
                    current_settings.filter_alpha_max);
  filter_set_enabled(current_settings.filter_enabled != 0u);
  trigger_reload_settings();
  settings_read_trigger_chatter_guard_from_led(
      &current_settings.led, &chatter_guard_enabled, &chatter_guard_duration_ms);
  (void)trigger_set_chatter_guard(chatter_guard_enabled,
                                  chatter_guard_duration_ms);
  settings_reset_led_effect_restore_state();
}

static void settings_mark_dirty(void) {
  settings_profile_sync_active_slot();
  settings_dirty = true;
  settings_change_counter++;
}

static void settings_set_default_keyboard_name(char *dst,
                                               uint8_t dst_size) {
  static const char default_name[] = "75HE Keyboard";
  uint8_t i = 0u;

  if (dst == NULL || dst_size == 0u) {
    return;
  }

  memset(dst, 0, dst_size);
  while (i + 1u < dst_size && default_name[i] != '\0') {
    dst[i] = default_name[i];
    i++;
  }
}

static void settings_set_default_profile_name(uint8_t profile_index, char *dst,
                                              uint8_t dst_size) {
  const char *default_name = "Default";
  uint8_t i = 0u;

  if (dst == NULL || dst_size == 0u) {
    return;
  }

  switch (profile_index) {
  case 0u:
    default_name = "Default";
    break;
  case 1u:
    default_name = "Profile 2";
    break;
  case 2u:
    default_name = "Profile 3";
    break;
  case 3u:
    default_name = "Profile 4";
    break;
  default:
    break;
  }

  memset(dst, 0, dst_size);
  while ((i + 1u) < dst_size && default_name[i] != '\0') {
    dst[i] = default_name[i];
    i++;
  }
}

static uint8_t settings_profile_first_used_slot(uint8_t used_mask) {
  for (uint8_t i = 0u; i < SETTINGS_PROFILE_COUNT; i++) {
    if ((used_mask & (uint8_t)(1u << i)) != 0u) {
      return i;
    }
  }

  return 0u;
}

static uint8_t settings_profile_used_count(uint8_t used_mask) {
  uint8_t count = 0u;

  for (uint8_t i = 0u; i < SETTINGS_PROFILE_COUNT; i++) {
    if ((used_mask & (uint8_t)(1u << i)) != 0u) {
      count++;
    }
  }

  return count;
}

static void settings_keyboard_name_cache_refresh(void) {
  memcpy(settings_keyboard_name_cache, current_settings.keyboard_name,
         SETTINGS_KEYBOARD_NAME_LENGTH);
  settings_keyboard_name_cache[SETTINGS_KEYBOARD_NAME_LENGTH] = '\0';
}

static void settings_profile_name_cache_refresh_slot(uint8_t profile_index) {
  if (profile_index >= SETTINGS_PROFILE_COUNT) {
    return;
  }

  memcpy(settings_profile_name_cache[profile_index],
         current_settings.profile_names[profile_index],
         SETTINGS_PROFILE_NAME_LENGTH);
  settings_profile_name_cache[profile_index][SETTINGS_PROFILE_NAME_LENGTH] =
      '\0';
}

static void settings_profile_name_cache_refresh(void) {
  for (uint8_t i = 0u; i < SETTINGS_PROFILE_COUNT; i++) {
    settings_profile_name_cache_refresh_slot(i);
  }
}

static void settings_sanitize_keyboard_name_from_bytes(char *dst,
                                                       const char *src,
                                                       uint8_t src_len) {
  uint8_t out_len = 0u;

  if (dst == NULL) {
    return;
  }

  memset(dst, 0, SETTINGS_KEYBOARD_NAME_LENGTH);
  if (src != NULL) {
    for (uint8_t i = 0u;
         i < src_len && i < SETTINGS_KEYBOARD_NAME_LENGTH;
         i++) {
      uint8_t c = (uint8_t)src[i];
      if (c == 0u) {
        break;
      }

      if (c < 32u || c > 126u) {
        continue;
      }

      dst[out_len++] = (char)c;
    }
  }

  while (out_len > 0u && dst[out_len - 1u] == ' ') {
    out_len--;
  }

  memset(&dst[out_len], 0, SETTINGS_KEYBOARD_NAME_LENGTH - out_len);

  if (out_len == 0u) {
    settings_set_default_keyboard_name(dst, SETTINGS_KEYBOARD_NAME_LENGTH);
  }
}

static void settings_sanitize_profile_name_from_bytes(uint8_t profile_index,
                                                      char *dst,
                                                      const char *src,
                                                      uint8_t src_len) {
  uint8_t out_len = 0u;

  if (dst == NULL) {
    return;
  }

  memset(dst, 0, SETTINGS_PROFILE_NAME_LENGTH);
  if (src != NULL) {
    for (uint8_t i = 0u;
         i < src_len && i < SETTINGS_PROFILE_NAME_LENGTH;
         i++) {
      uint8_t c = (uint8_t)src[i];
      if (c == 0u) {
        break;
      }

      if (c < 32u || c > 126u) {
        continue;
      }

      if ((out_len + 1u) >= SETTINGS_PROFILE_NAME_LENGTH) {
        break;
      }

      dst[out_len++] = (char)c;
    }
  }

  while (out_len > 0u && dst[out_len - 1u] == ' ') {
    out_len--;
  }

  memset(&dst[out_len], 0, SETTINGS_PROFILE_NAME_LENGTH - out_len);

  if (out_len == 0u) {
    settings_set_default_profile_name(profile_index, dst,
                                      SETTINGS_PROFILE_NAME_LENGTH);
  }
}

static void settings_sanitize_keyboard_name(void) {
  char sanitized[SETTINGS_KEYBOARD_NAME_LENGTH];
  settings_sanitize_keyboard_name_from_bytes(
      sanitized, current_settings.keyboard_name, SETTINGS_KEYBOARD_NAME_LENGTH);
  memcpy(current_settings.keyboard_name, sanitized,
         SETTINGS_KEYBOARD_NAME_LENGTH);
  settings_keyboard_name_cache_refresh();
}

static void settings_sanitize_profile_names(void) {
  char sanitized[SETTINGS_PROFILE_NAME_LENGTH];
  uint8_t used_mask =
      (uint8_t)(current_settings.profile_used_mask & SETTINGS_PROFILE_MASK_ALL);

  if (used_mask == 0u) {
    used_mask = 0x01u;
    settings_set_default_profile_name(0u, current_settings.profile_names[0],
                                      SETTINGS_PROFILE_NAME_LENGTH);
  }
  current_settings.profile_used_mask = used_mask;

  if (current_settings.active_profile_index >= SETTINGS_PROFILE_COUNT ||
      (used_mask & (uint8_t)(1u << current_settings.active_profile_index)) ==
          0u) {
    current_settings.active_profile_index = settings_profile_first_used_slot(used_mask);
  }

  for (uint8_t i = 0u; i < SETTINGS_PROFILE_COUNT; i++) {
    if ((used_mask & (uint8_t)(1u << i)) != 0u) {
      settings_sanitize_profile_name_from_bytes(
          i, sanitized, current_settings.profile_names[i],
          SETTINGS_PROFILE_NAME_LENGTH);
      memcpy(current_settings.profile_names[i], sanitized,
             SETTINGS_PROFILE_NAME_LENGTH);
    } else {
      memset(current_settings.profile_names[i], 0,
             SETTINGS_PROFILE_NAME_LENGTH);
    }
  }

  settings_profile_name_cache_refresh();
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

static settings_key_t settings_default_key(uint8_t key_index) {
  settings_key_t key = {
      .hid_keycode = layout_get_default_keycode(key_index),
  .actuation_point_mm = 12,
  .release_point_mm = 12,
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

  if (key->actuation_point_mm == 0u) {
    key->actuation_point_mm = 1u;
  }

  if (key->release_point_mm > key->actuation_point_mm) {
    key->release_point_mm = key->actuation_point_mm;
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
  current_settings.default_profile_index = SETTINGS_DEFAULT_PROFILE_NONE;
  current_settings.reserved_pad = 0u;

  // Default options
  current_settings.options.keyboard_enabled = 1;
  current_settings.options.gamepad_enabled = 0;
  current_settings.options.raw_hid_echo = 0;
  current_settings.options.led_enabled = 1;
  current_settings.options.nkro_enabled = 1;
  current_settings.options.led_thermal_protection_enabled = 1;
  current_settings.options.reserved = 0u;
    settings_options_set_led_system_when_disabled(
      &current_settings.options,
      SETTINGS_DEFAULT_LED_ALLOW_SYSTEM_WHEN_DISABLED != 0u);
    settings_options_set_led_idle_third_party_stream_activity(
      &current_settings.options,
      SETTINGS_DEFAULT_LED_IDLE_THIRD_PARTY_STREAM_ACTIVITY != 0u);

  // Default per-key settings follow the physical keyboard layout.
  for (uint8_t i = 0; i < NUM_KEYS; i++) {
    current_settings.keys[i] = settings_default_key(i);
  }
  settings_default_layer_keycodes();

  // Default gamepad settings
  settings_default_gamepad(&current_settings.gamepad);

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
  current_settings.led_effect_mode = LED_EFFECT_CYCLE_LEFT_RIGHT;
  current_settings.led_fps_limit = 60;
  for (uint8_t effect = 0; effect < LED_EFFECT_MAX; effect++) {
    settings_default_effect_params(effect,
                                   current_settings.led_effect_params[effect]);
  }
  settings_normalize_led_effect_speeds();
  settings_sync_led_effect_speed_cache();
  settings_get_effect_color_from_params(current_settings.led_effect_mode,
                                        &current_settings.led_effect_color_r,
                                        &current_settings.led_effect_color_g,
                                        &current_settings.led_effect_color_b);

  current_settings.filter_enabled = FILTER_DEFAULT_ENABLED;
  current_settings.filter_noise_band = FILTER_DEFAULT_NOISE_BAND;
  current_settings.filter_alpha_min = FILTER_DEFAULT_ALPHA_MIN_DENOM;
  current_settings.filter_alpha_max = FILTER_DEFAULT_ALPHA_MAX_DENOM;
  current_settings.advanced_tick_rate = SETTINGS_DEFAULT_ADVANCED_TICK_RATE;
  settings_write_trigger_chatter_guard_to_led(
      &current_settings.led,
      SETTINGS_DEFAULT_TRIGGER_CHATTER_GUARD_ENABLED != 0u,
      SETTINGS_DEFAULT_TRIGGER_CHATTER_GUARD_MS);
  settings_write_led_idle_timeout_seconds_to_led(
      &current_settings.led, SETTINGS_DEFAULT_LED_IDLE_TIMEOUT_SECONDS);
  settings_write_led_usb_suspend_rgb_off_to_led(
      &current_settings.led,
      SETTINGS_DEFAULT_LED_USB_SUSPEND_RGB_OFF != 0u);

  settings_default_rotary_encoder(&default_rotary);
  current_settings.rotary = default_rotary;

  settings_set_default_keyboard_name(current_settings.keyboard_name,
                                     SETTINGS_KEYBOARD_NAME_LENGTH);
  current_settings.active_profile_index = 0u;
  current_settings.profile_used_mask = 0x01u; /* Only slot 0 active by default */
  memset(current_settings.profiles, 0, sizeof(current_settings.profiles));
  /* Capture defaults into slot 0; additional slots are created on demand. */
  settings_profile_capture_current_slot(0u);
  settings_sanitize_profile_advanced_layers(&current_settings.profiles[0u]);
  memset(current_settings.profile_names, 0, sizeof(current_settings.profile_names));
  settings_set_default_profile_name(0u, current_settings.profile_names[0u],
                                      SETTINGS_PROFILE_NAME_LENGTH);
  settings_keyboard_name_cache_refresh();
  settings_profile_name_cache_refresh();
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

static bool settings_load_from_flash(void) {
  settings_t temp;
  uint16_t source_version = 0u;
  uint8_t original_used_mask = 0u;
  bool needs_resave = false;

  // Read settings from flash
  if (!flash_storage_read(0, &temp, sizeof(settings_t))) {
    return false;
  }

  source_version = temp.version;

  if (!settings_validate_current(&temp)) {
    /* Full validation failed.  If the magic bytes are intact but the version or
     * CRC has changed (e.g. after a firmware update), accept the raw data and
     * sanitise it rather than resetting everything to defaults.
     * A version mismatch means the struct layout may have changed, but any
     * byte that falls inside a still-valid field will be sanitised by the loop
     * below; out-of-range values are clamped to defaults.  The migrated image
     * is re-saved immediately so future boots do not repeat the migration. */
    if (temp.magic_start != SETTINGS_MAGIC_START ||
        temp.magic_end != SETTINGS_MAGIC_END) {
      return false;
    }
    temp.version = SETTINGS_VERSION;
    /* Old reserved byte (now default_profile_index) was always 0, which would
     * silently force boot-to-profile-0.  Reset it to NONE so behaviour is
     * identical to pre-migration firmware. */
    temp.default_profile_index = SETTINGS_DEFAULT_PROFILE_NONE;
    if (source_version < SETTINGS_VERSION) {
      temp.options.led_thermal_protection_enabled = 1u;
      settings_write_led_usb_suspend_rgb_off_to_led(
          &temp.led, SETTINGS_DEFAULT_LED_USB_SUSPEND_RGB_OFF != 0u);
      for (uint8_t profile_index = 0u; profile_index < SETTINGS_PROFILE_COUNT;
           profile_index++) {
        settings_write_led_usb_suspend_rgb_off_to_led(
            &temp.profiles[profile_index].led,
            SETTINGS_DEFAULT_LED_USB_SUSPEND_RGB_OFF != 0u);
      }
    }
    needs_resave = true;
  }

  memcpy(&current_settings, &temp, sizeof(settings_t));
  settings_sanitize_options(&current_settings.options);
  original_used_mask =
      (uint8_t)(current_settings.profile_used_mask & SETTINGS_PROFILE_MASK_ALL);

  settings_sanitize_keyboard_name();
  settings_sanitize_profile_names();

  if (original_used_mask == 0u) {
    /* Flash was written with no used-profile bits.  Treat the stored top-level
     * settings as profile 0 so the load loop can process it normally. */
    current_settings.profile_used_mask = 0x01u;
    current_settings.active_profile_index = 0u;
    settings_profile_capture_current_slot(0u);
  }

  for (uint8_t profile_index = 0u; profile_index < SETTINGS_PROFILE_COUNT;
       profile_index++) {
    if (!settings_is_profile_slot_used(profile_index)) {
      memset(&current_settings.profiles[profile_index], 0,
             sizeof(current_settings.profiles[profile_index]));
      continue;
    }

    settings_profile_apply_slot(profile_index);

    for (uint8_t key_index = 0u; key_index < NUM_KEYS; key_index++) {
      settings_sanitize_key_config(key_index, &current_settings.keys[key_index]);
    }
    settings_sanitize_layer_keycodes();
    settings_gamepad_sanitize(&current_settings.gamepad);
    settings_rotary_encoder_sanitize(&current_settings.rotary);
    settings_normalize_led_effect_speeds();
    settings_sync_led_effect_speed_cache();
    current_settings.advanced_tick_rate = settings_sanitize_advanced_tick_rate(
        current_settings.advanced_tick_rate);
    settings_sanitize_trigger_chatter_guard_storage(&current_settings.led);
    settings_sanitize_led_usb_suspend_rgb_off_storage(&current_settings.led);
    settings_sanitize_led_idle_timeout_storage(&current_settings.led);

    settings_profile_capture_current_slot(profile_index);
    settings_sanitize_profile_advanced_layers(
      &current_settings.profiles[profile_index]);
  }

  settings_profile_apply_slot(current_settings.active_profile_index);
  settings_reset_led_effect_restore_state();

  if (needs_resave) {
    (void)settings_save();
  }

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

  // If a default boot profile is configured and that slot is used, switch to it
  // now so the device always starts on the designated profile regardless of what
  // was last active before the previous power-off.
  uint8_t boot_default = current_settings.default_profile_index;
  if (boot_default != SETTINGS_DEFAULT_PROFILE_NONE &&
      boot_default < SETTINGS_PROFILE_COUNT &&
      settings_is_profile_slot_used(boot_default) &&
      boot_default != current_settings.active_profile_index) {
    (void)settings_set_active_profile_index(boot_default);
  }

  calibration_load_settings();
  settings_apply_runtime_from_current_profile();
  settings_sanitize_keyboard_name();
  settings_sanitize_profile_names();

  settings_ram_only_mode = false;
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
  bool was_enabled = current_settings.options.keyboard_enabled != 0u;
  current_settings.options.keyboard_enabled = enabled ? 1u : 0u;

  if (was_enabled && !enabled) {
    // Ensure no stale keyboard report remains active after disabling keyboard output.
    keyboard_hid_release_all();
    keyboard_hid_reset_state();
    keyboard_nkro_hid_release_all();
  }

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
  // Auto mode state is runtime-based; re-enumeration may be required to retry
  // NKRO after a runtime fallback has already been selected.
  settings_mark_dirty();
  return true;
}

bool settings_is_led_thermal_protection_enabled(void) {
  return current_settings.options.led_thermal_protection_enabled != 0u;
}

bool settings_set_led_thermal_protection_enabled(bool enabled) {
  current_settings.options.led_thermal_protection_enabled = enabled ? 1u : 0u;
  settings_mark_dirty();
  return true;
}

bool settings_set_options(settings_options_t options) {
  bool was_enabled = current_settings.options.keyboard_enabled != 0u;
  settings_sanitize_options(&options);
  current_settings.options = options;

  if (was_enabled && current_settings.options.keyboard_enabled == 0u) {
    keyboard_hid_release_all();
    keyboard_hid_reset_state();
    keyboard_nkro_hid_release_all();
  }

  gamepad_hid_set_enabled(current_settings.options.gamepad_enabled != 0u);
  gamepad_hid_reload_settings();
  led_matrix_set_allow_system_indicators_when_disabled(
      settings_options_led_system_when_disabled(&current_settings.options));
  led_matrix_set_idle_third_party_stream_counts_as_activity(
      settings_options_led_idle_third_party_stream_activity(
          &current_settings.options));
  settings_mark_dirty();
  return true;
}

settings_options_t settings_get_options(void) {
  return current_settings.options;
}

bool settings_reset(void) {
  if (settings_ram_only_mode) {
    return false;
  }

  settings_set_defaults();
  calibration_load_settings();
  settings_apply_runtime_from_current_profile();
  return settings_save();
}

bool settings_save(void) {
  if (settings_save_in_progress) {
    while (true) {
      flash_storage_async_result_t step =
          flash_storage_write_async_step(256u);
      if (step == FLASH_STORAGE_ASYNC_IN_PROGRESS) {
        continue;
      }

      settings_save_in_progress = false;
      if (step == FLASH_STORAGE_ASYNC_ERROR) {
        return false;
      }
      break;
    }
  }

  // RAM-only mode: writes stay in RAM and are never persisted to flash.
  if (settings_ram_only_mode) {
    return false;
  }

  settings_profile_sync_active_slot();

  // Compute CRC before saving
  current_settings.crc32 =
      crc32_compute(&current_settings, sizeof(settings_t) - sizeof(uint32_t));

  // Append a new settings snapshot. The storage backend consolidates only
  // when the reserved flash sector is full, which keeps normal saves cheap.
  if (!flash_storage_write(0, &current_settings, sizeof(settings_t))) {
    return false;
  }

  settings_dirty = false;
  settings_save_requested = false;
  settings_last_seen_change_counter = settings_change_counter;
  return true;
}

bool settings_request_save(void) {
  if (settings_ram_only_mode) {
    settings_save_requested = false;
    return false;
  }

  if (!settings_dirty) {
    settings_save_requested = false;
    return true;
  }

  settings_save_requested = true;
  return true;
}

static bool settings_start_async_save(void) {
  if (settings_save_in_progress) {
    return true;
  }

  // RAM-only mode: no flash write is scheduled.
  if (settings_ram_only_mode) {
    settings_dirty = false;
    settings_save_requested = false;
    settings_last_seen_change_counter = settings_change_counter;
    return true;
  }

  settings_profile_sync_active_slot();
  memcpy(&settings_save_snapshot, &current_settings, sizeof(settings_t));
  settings_save_snapshot.crc32 =
      crc32_compute(&settings_save_snapshot,
                    sizeof(settings_t) - sizeof(uint32_t));

  if (!flash_storage_write_async_begin(0u, &settings_save_snapshot,
                                       sizeof(settings_t))) {
    return false;
  }

  settings_save_in_progress = true;
  settings_save_snapshot_change_counter = settings_change_counter;
  return true;
}

void settings_task(uint32_t now_ms) {
  uint32_t save_delay_ms = SETTINGS_AUTOSAVE_DELAY_MS;

  if (settings_change_counter != settings_last_seen_change_counter) {
    settings_last_seen_change_counter = settings_change_counter;
    settings_last_change_ms = now_ms;
    settings_dirty = true;
  }

  if (settings_save_in_progress) {
    flash_storage_async_result_t step =
        flash_storage_write_async_step(SETTINGS_FLASH_WORDS_PER_STEP);

    if (step == FLASH_STORAGE_ASYNC_IN_PROGRESS) {
      return;
    }

    settings_save_in_progress = false;
    if (step == FLASH_STORAGE_ASYNC_ERROR) {
      settings_last_change_ms = now_ms;
      settings_dirty = true;
      return;
    }

    settings_save_requested = false;
    if (settings_change_counter == settings_save_snapshot_change_counter) {
      settings_dirty = false;
      settings_last_seen_change_counter = settings_change_counter;
      return;
    }

    settings_last_change_ms = now_ms;
    settings_dirty = true;
  }

  if (!settings_dirty) {
    settings_save_requested = false;
    return;
  }

  if (calibration_guided_is_active()) {
    return;
  }

  if (settings_save_requested) {
    save_delay_ms = SETTINGS_REQUEST_SAVE_DELAY_MS;
  }

  if ((uint32_t)(now_ms - settings_last_change_ms) < save_delay_ms) {
    return;
  }

  (void)settings_start_async_save();
}

bool settings_has_unsaved_changes(void) { return settings_dirty; }

uint8_t settings_get_firmware_version_major(void) {
  return FIRMWARE_VERSION_MAJOR;
}

uint8_t settings_get_firmware_version_minor(void) {
  return FIRMWARE_VERSION_MINOR;
}

uint8_t settings_get_firmware_version_patch(void) {
  return FIRMWARE_VERSION_PATCH;
}

const char *settings_get_keyboard_name(void) {
  return settings_keyboard_name_cache;
}

bool settings_set_keyboard_name(const char *name, uint8_t length) {
  char sanitized[SETTINGS_KEYBOARD_NAME_LENGTH];

  settings_sanitize_keyboard_name_from_bytes(sanitized, name, length);
  if (memcmp(current_settings.keyboard_name, sanitized,
             SETTINGS_KEYBOARD_NAME_LENGTH) == 0) {
    return true;
  }

  memcpy(current_settings.keyboard_name, sanitized,
         SETTINGS_KEYBOARD_NAME_LENGTH);
  settings_keyboard_name_cache_refresh();
  settings_mark_dirty();
  return true;
}

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

void settings_get_trigger_chatter_guard(bool *enabled, uint8_t *duration_ms) {
  settings_read_trigger_chatter_guard_from_led(&current_settings.led, enabled,
                                               duration_ms);
}

bool settings_set_trigger_chatter_guard(bool enabled, uint8_t duration_ms) {
  if (duration_ms > SETTINGS_TRIGGER_CHATTER_GUARD_MAX_MS) {
    return false;
  }

  duration_ms = settings_sanitize_trigger_chatter_guard_duration(duration_ms);
  settings_write_trigger_chatter_guard_to_led(&current_settings.led, enabled,
                                              duration_ms);
  (void)trigger_set_chatter_guard(enabled, duration_ms);
  settings_mark_dirty();
  return true;
}

uint8_t settings_get_active_profile_index(void) {
  uint8_t used_mask = settings_get_profile_used_mask();

  if (current_settings.active_profile_index >= SETTINGS_PROFILE_COUNT ||
      (used_mask & (uint8_t)(1u << current_settings.active_profile_index)) ==
          0u) {
    return settings_profile_first_used_slot(used_mask);
  }

  return current_settings.active_profile_index;
}

bool settings_set_active_profile_index(uint8_t profile_index) {
  if (profile_index >= SETTINGS_PROFILE_COUNT ||
      !settings_is_profile_slot_used(profile_index)) {
    return false;
  }

  if (current_settings.active_profile_index == profile_index) {
    return true;
  }

  settings_profile_sync_active_slot();
  current_settings.active_profile_index = profile_index;
  settings_profile_apply_slot(profile_index);
  settings_apply_runtime_from_current_profile();
  settings_mark_dirty();
  return true;
}

uint8_t settings_get_default_profile_index(void) {
  return current_settings.default_profile_index;
}

bool settings_set_default_profile_index(uint8_t profile_index) {
  if (profile_index != SETTINGS_DEFAULT_PROFILE_NONE &&
      profile_index >= SETTINGS_PROFILE_COUNT) {
    return false;
  }
  current_settings.default_profile_index = profile_index;
  settings_mark_dirty();
  return true;
}

bool settings_is_ram_only_mode(void) {
  return settings_ram_only_mode;
}

void settings_enter_ram_only_mode(void) {
  settings_ram_only_mode = true;
  settings_save_requested = false;
}

bool settings_exit_ram_only_mode(void) {
  settings_ram_only_mode = false;
  settings_save_requested = false;
  if (!settings_load_from_flash()) {
    settings_set_defaults();
    settings_save();
    settings_dirty = false;
    settings_last_seen_change_counter = settings_change_counter;
    return false;
  }
  calibration_load_settings();
  settings_apply_runtime_from_current_profile();
  // Discard any dirty state accumulated during the RAM-only session; the RAM
  // contents now match flash so there is nothing new to save.
  settings_dirty = false;
  settings_last_seen_change_counter = settings_change_counter;
  return true;
}

uint8_t settings_get_profile_used_mask(void) {
  return (uint8_t)(current_settings.profile_used_mask & SETTINGS_PROFILE_MASK_ALL);
}

bool settings_is_profile_slot_used(uint8_t profile_index) {
  if (profile_index >= SETTINGS_PROFILE_COUNT) {
    return false;
  }

  return (settings_get_profile_used_mask() & (uint8_t)(1u << profile_index)) !=
         0u;
}

int8_t settings_create_profile(const char *name, uint8_t length) {
  char sanitized[SETTINGS_PROFILE_NAME_LENGTH];
  uint8_t used_mask = settings_get_profile_used_mask();
  uint8_t source_profile_index = settings_get_active_profile_index();

  if (used_mask == SETTINGS_PROFILE_MASK_ALL) {
    return -1;
  }

  settings_profile_sync_active_slot();
  if (!settings_is_profile_slot_used(source_profile_index)) {
    source_profile_index = settings_profile_first_used_slot(used_mask);
  }

  for (uint8_t i = 0u; i < SETTINGS_PROFILE_COUNT; i++) {
    if ((used_mask & (uint8_t)(1u << i)) != 0u) {
      continue;
    }

    memcpy(&current_settings.profiles[i],
           &current_settings.profiles[source_profile_index],
           sizeof(current_settings.profiles[i]));
    settings_sanitize_profile_name_from_bytes(i, sanitized, name, length);
    memcpy(current_settings.profile_names[i], sanitized,
           SETTINGS_PROFILE_NAME_LENGTH);
    current_settings.profile_used_mask = (uint8_t)(used_mask | (uint8_t)(1u << i));
    settings_profile_name_cache_refresh_slot(i);
    settings_mark_dirty();
    return (int8_t)i;
  }

  return -1;
}

bool settings_delete_profile(uint8_t profile_index) {
  uint8_t used_mask = settings_get_profile_used_mask();

  if (profile_index >= SETTINGS_PROFILE_COUNT ||
      (used_mask & (uint8_t)(1u << profile_index)) == 0u) {
    return false;
  }

  if (settings_profile_used_count(used_mask) <= 1u) {
    return false;
  }

  if (current_settings.active_profile_index == profile_index) {
    settings_profile_sync_active_slot();
  }

  used_mask = (uint8_t)(used_mask & (uint8_t)(~(uint8_t)(1u << profile_index)));
  current_settings.profile_used_mask = used_mask;
  memset(&current_settings.profiles[profile_index], 0,
         sizeof(current_settings.profiles[profile_index]));
  memset(current_settings.profile_names[profile_index], 0,
         SETTINGS_PROFILE_NAME_LENGTH);
  settings_profile_name_cache_refresh_slot(profile_index);

  if (current_settings.active_profile_index == profile_index) {
    current_settings.active_profile_index =
        settings_profile_first_used_slot(used_mask);
    settings_profile_apply_slot(current_settings.active_profile_index);
    settings_apply_runtime_from_current_profile();
  }

  // If the deleted slot was the designated boot default, clear the preference
  // so the keyboard falls back to last-used behaviour.
  if (current_settings.default_profile_index == profile_index) {
    current_settings.default_profile_index = SETTINGS_DEFAULT_PROFILE_NONE;
  }

  settings_mark_dirty();
  return true;
}

bool settings_copy_profile_slot(uint8_t source_profile_index,
                                uint8_t target_profile_index) {
  uint8_t used_mask = settings_get_profile_used_mask();
  bool target_was_used = false;

  if (source_profile_index >= SETTINGS_PROFILE_COUNT ||
      target_profile_index >= SETTINGS_PROFILE_COUNT) {
    return false;
  }

  if (!settings_is_profile_slot_used(source_profile_index)) {
    return false;
  }

  if (source_profile_index == target_profile_index) {
    return true;
  }

  settings_profile_sync_active_slot();

  target_was_used =
      (used_mask & (uint8_t)(1u << target_profile_index)) != 0u;
  memcpy(&current_settings.profiles[target_profile_index],
         &current_settings.profiles[source_profile_index],
         sizeof(current_settings.profiles[target_profile_index]));
  settings_sanitize_profile_advanced_layers(
      &current_settings.profiles[target_profile_index]);

  if (!target_was_used) {
    used_mask = (uint8_t)(used_mask | (uint8_t)(1u << target_profile_index));
    current_settings.profile_used_mask = used_mask;
    settings_set_default_profile_name(target_profile_index,
                                      current_settings.profile_names
                                          [target_profile_index],
                                      SETTINGS_PROFILE_NAME_LENGTH);
    settings_profile_name_cache_refresh_slot(target_profile_index);
  }

  if (settings_get_active_profile_index() == target_profile_index) {
    settings_profile_apply_slot(target_profile_index);
    settings_apply_runtime_from_current_profile();
  }

  settings_mark_dirty();
  return true;
}

bool settings_reset_profile_slot(uint8_t profile_index) {
  if (profile_index >= SETTINGS_PROFILE_COUNT ||
      !settings_is_profile_slot_used(profile_index)) {
    return false;
  }

  settings_profile_sync_active_slot();
  settings_populate_profile_defaults(&current_settings.profiles[profile_index]);

  if (settings_get_active_profile_index() == profile_index) {
    settings_profile_apply_slot(profile_index);
    settings_apply_runtime_from_current_profile();
  }

  settings_mark_dirty();
  return true;
}

const char *settings_get_profile_name(uint8_t profile_index) {
  if (profile_index >= SETTINGS_PROFILE_COUNT ||
      !settings_is_profile_slot_used(profile_index)) {
    return NULL;
  }

  return settings_profile_name_cache[profile_index];
}

bool settings_set_profile_name(uint8_t profile_index, const char *name,
                               uint8_t length) {
  char sanitized[SETTINGS_PROFILE_NAME_LENGTH];

  if (profile_index >= SETTINGS_PROFILE_COUNT ||
      !settings_is_profile_slot_used(profile_index)) {
    return false;
  }

  settings_sanitize_profile_name_from_bytes(profile_index, sanitized, name,
                                            length);
  if (memcmp(current_settings.profile_names[profile_index], sanitized,
             SETTINGS_PROFILE_NAME_LENGTH) == 0) {
    return true;
  }

  memcpy(current_settings.profile_names[profile_index], sanitized,
         SETTINGS_PROFILE_NAME_LENGTH);
  settings_profile_name_cache_refresh_slot(profile_index);
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

uint8_t settings_get_led_idle_timeout_seconds(void) {
  return settings_read_led_idle_timeout_seconds_from_led(&current_settings.led);
}

bool settings_set_led_idle_timeout_seconds(uint8_t timeout_seconds) {
  if (timeout_seconds > SETTINGS_LED_IDLE_TIMEOUT_MAX_SECONDS) {
    return false;
  }

  timeout_seconds = settings_sanitize_led_idle_timeout_seconds(timeout_seconds);
  settings_write_led_idle_timeout_seconds_to_led(&current_settings.led,
                                                 timeout_seconds);
  led_matrix_set_idle_timeout_seconds(timeout_seconds);
  settings_mark_dirty();
  return true;
}

bool settings_is_led_system_indicators_allowed_when_disabled(void) {
  return settings_options_led_system_when_disabled(&current_settings.options);
}

bool settings_set_led_system_indicators_allowed_when_disabled(bool enabled) {
  settings_options_set_led_system_when_disabled(&current_settings.options,
                                                enabled);
  led_matrix_set_allow_system_indicators_when_disabled(enabled);
  settings_mark_dirty();
  return true;
}

bool settings_is_led_idle_third_party_stream_counts_as_activity(void) {
  return settings_options_led_idle_third_party_stream_activity(
      &current_settings.options);
}

bool settings_set_led_idle_third_party_stream_counts_as_activity(bool enabled) {
  settings_options_set_led_idle_third_party_stream_activity(
      &current_settings.options, enabled);
  led_matrix_set_idle_third_party_stream_counts_as_activity(enabled);
  settings_mark_dirty();
  return true;
}

bool settings_is_led_usb_suspend_rgb_off_enabled(void) {
  return settings_read_led_usb_suspend_rgb_off_from_led(&current_settings.led);
}

bool settings_set_led_usb_suspend_rgb_off_enabled(bool enabled) {
  settings_write_led_usb_suspend_rgb_off_to_led(&current_settings.led, enabled);
  if (!enabled) {
    led_matrix_set_usb_suspend_state(false);
  }
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

  if (current_settings.led_effect_mode == mode) {
    return true;
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
  settings_sync_led_effect_speed_cache();
  settings_get_effect_color_from_params(mode,
                                        &current_settings.led_effect_color_r,
                                        &current_settings.led_effect_color_g,
                                        &current_settings.led_effect_color_b);
  settings_mark_dirty();
  return true;
}

uint8_t settings_get_led_effect_speed(void) {
  uint8_t effect_mode = current_settings.led_effect_mode;

  if (effect_mode >= LED_EFFECT_MAX) {
    return 1u;
  }

  return settings_sanitize_effect_speed(
      current_settings.led_effect_params[effect_mode][LED_EFFECT_PARAM_SPEED]);
}

bool settings_set_led_effect_speed(uint8_t speed) {
  uint8_t effect_mode = current_settings.led_effect_mode;

  if (effect_mode >= LED_EFFECT_MAX) {
    return false;
  }

  speed = settings_sanitize_effect_speed(speed);
  current_settings.led_effect_params[effect_mode][LED_EFFECT_PARAM_SPEED] =
      speed;
  settings_sync_led_effect_speed_cache();
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
  settings_get_effect_color_from_params(current_settings.led_effect_mode, r, g,
                                        b);
}

bool settings_set_led_effect_color(uint8_t r, uint8_t g, uint8_t b) {
  uint8_t effect_mode = current_settings.led_effect_mode;
  if (effect_mode >= (uint8_t)LED_EFFECT_MAX) {
    return false;
  }

  settings_set_effect_color_in_params(effect_mode, r, g, b);
  led_matrix_set_effect_params(current_settings.led_effect_params[effect_mode]);
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
    settings_sync_led_effect_speed_cache();
    settings_get_effect_color_from_params(effect_mode,
                                          &current_settings.led_effect_color_r,
                                          &current_settings.led_effect_color_g,
                                          &current_settings.led_effect_color_b);
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

static const settings_profile_t *settings_profile_slot_const(
    uint8_t profile_index) {
  if (profile_index >= SETTINGS_PROFILE_COUNT ||
      !settings_is_profile_slot_used(profile_index)) {
    return NULL;
  }

  return &current_settings.profiles[profile_index];
}

static settings_profile_t *settings_profile_slot_mut(uint8_t profile_index) {
  if (profile_index >= SETTINGS_PROFILE_COUNT ||
      !settings_is_profile_slot_used(profile_index)) {
    return NULL;
  }

  return &current_settings.profiles[profile_index];
}

bool settings_get_profile_layer_key_settings(uint8_t profile_index,
                                             uint8_t layer_index,
                                             uint8_t key_index,
                                             settings_key_t *key) {
  const settings_profile_t *profile =
      settings_profile_slot_const(profile_index);

  if (profile == NULL || key == NULL || key_index >= NUM_KEYS ||
      layer_index >= SETTINGS_LAYER_COUNT) {
    return false;
  }

  *key = profile->keys[key_index];
  if (layer_index == 0u) {
    key->hid_keycode = profile->keys[key_index].hid_keycode;
  } else {
    key->hid_keycode = profile->layer_keycodes[layer_index - 1u][key_index];
    if (key->hid_keycode == 0xFFFFu) {
      key->hid_keycode = KC_TRANSPARENT;
    }
  }

  key->advanced = profile->advanced_by_layer[layer_index][key_index];
  settings_sanitize_key_advanced(
      settings_profile_layer_primary_keycode(profile, layer_index, key_index),
      key);
  return true;
}

bool settings_set_profile_layer_key_settings(uint8_t profile_index,
                                             uint8_t layer_index,
                                             uint8_t key_index,
                                             const settings_key_t *key) {
  settings_profile_t *profile = settings_profile_slot_mut(profile_index);
  bool is_active_profile =
      (settings_get_active_profile_index() == profile_index);

  if (profile == NULL || key == NULL || key_index >= NUM_KEYS ||
      layer_index >= SETTINGS_LAYER_COUNT) {
    return false;
  }

  if (layer_index == 0u) {
    settings_key_t sanitized = *key;
    settings_sanitize_key_config(key_index, &sanitized);
    profile->keys[key_index] = sanitized;
    profile->advanced_by_layer[0u][key_index] = sanitized.advanced;
  } else {
    settings_key_t temp = profile->keys[key_index];
    profile->layer_keycodes[layer_index - 1u][key_index] = key->hid_keycode;
    if (profile->layer_keycodes[layer_index - 1u][key_index] == 0xFFFFu) {
      profile->layer_keycodes[layer_index - 1u][key_index] = KC_TRANSPARENT;
    }

    temp.advanced = key->advanced;
    settings_sanitize_key_advanced(
        settings_profile_layer_primary_keycode(profile, layer_index, key_index),
        &temp);
    profile->advanced_by_layer[layer_index][key_index] = temp.advanced;
  }

  if (is_active_profile) {
    if (layer_index == 0u) {
      current_settings.keys[key_index] = profile->keys[key_index];
      current_settings.keys[key_index].advanced =
          profile->advanced_by_layer[0u][key_index];
      gamepad_hid_reload_settings();
    } else {
      current_settings.layer_keycodes[layer_index - 1u][key_index] =
          profile->layer_keycodes[layer_index - 1u][key_index];
    }

    trigger_reload_settings();
  }

  settings_mark_dirty();
  return true;
}

bool settings_get_key_for_layer(uint8_t key_index, uint8_t layer_index,
                                settings_key_t *key) {
  return settings_get_profile_layer_key_settings(
      settings_get_active_profile_index(), layer_index, key_index, key);
}

bool settings_set_key_for_layer(uint8_t key_index, uint8_t layer_index,
                                const settings_key_t *key) {
  return settings_set_profile_layer_key_settings(
      settings_get_active_profile_index(), layer_index, key_index, key);
}

bool settings_set_key(uint8_t key_index, const settings_key_t *key) {
  return settings_set_key_for_layer(key_index, 0u, key);
}

bool settings_reset_key_trigger_settings(uint8_t key_index) {
  const settings_key_t *current_key = NULL;
  settings_key_t key = {0};
  settings_key_t defaults = {0};

  if (key_index >= NUM_KEYS) {
    return false;
  }

  current_key = settings_get_key(key_index);
  if (current_key == NULL) {
    return false;
  }

  key = *current_key;
  defaults = settings_default_key(key_index);

  key.actuation_point_mm = defaults.actuation_point_mm;
  key.release_point_mm = defaults.release_point_mm;
  key.rapid_trigger_press = defaults.rapid_trigger_press;
  key.rapid_trigger_release = defaults.rapid_trigger_release;
  key.rapid_trigger_enabled = defaults.rapid_trigger_enabled;
  settings_key_set_continuous_rapid_trigger(
      &key, settings_key_is_continuous_rapid_trigger_enabled(&defaults));

  return settings_set_key(key_index, &key);
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

  if (!settings_get_key_for_layer(key_index, layer_index, &key)) {
    return false;
  }

  key.hid_keycode = keycode;
  return settings_set_key_for_layer(key_index, layer_index, &key);
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

bool settings_is_key_mapped_to_gamepad(uint8_t key_index) {
  const settings_gamepad_mapping_t *mapping = NULL;

  if (key_index >= NUM_KEYS) {
    return false;
  }

  mapping = &current_settings.keys[key_index].gamepad_map;
  return mapping->axis != (uint8_t)GAMEPAD_AXIS_NONE ||
         mapping->button != (uint8_t)GAMEPAD_BUTTON_NONE;
}

bool settings_is_key_mapped_to_gamepad_on_layer(uint8_t key_index,
                                                uint8_t layer_index) {
  const settings_gamepad_mapping_t *mapping = NULL;

  if (key_index >= NUM_KEYS || layer_index >= SETTINGS_LAYER_COUNT) {
    return false;
  }

  mapping = &current_settings.keys[key_index].gamepad_map;
  if (mapping->axis == (uint8_t)GAMEPAD_AXIS_NONE &&
      mapping->button == (uint8_t)GAMEPAD_BUTTON_NONE) {
    return false;
  }

  return settings_gamepad_mapping_is_active_on_layer(mapping, layer_index);
}
