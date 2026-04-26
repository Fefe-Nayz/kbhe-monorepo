/*
 * hid_protocol.c
 * RAW HID Command Protocol implementation
 */

#include "hid_protocol.h"
#include "adc_capture.h"
#include "analog/analog.h"
#include "diagnostics.h"
#include "led_indicator.h"
#include "led_matrix.h"
#include "layout/keycodes.h"
#include "main.h"
#include "rotary_encoder.h"
#include "settings.h"
#include "trigger/trigger.h"
#include "updater_app.h"
// #include "trigger.h"
#include "analog/calibration.h"
#include <stdint.h>
#include <string.h>

//--------------------------------------------------------------------+
// External declarations for debug data
//--------------------------------------------------------------------+

#define HID_ALPHA_MASK_MAX_BYTES ((NUM_KEYS + 7u) / 8u)

static inline bool is_valid_adc_calibration_value(int16_t value) {
  return value >= 0 && value <= 4095;
}

static inline void refresh_runtime_calibration(void) {
  calibration_load_settings();
}

static inline uint8_t hid_sanitize_socd_resolution(uint8_t resolution) {
  if (resolution >= (uint8_t)SETTINGS_SOCD_RESOLUTION_MAX) {
    return (uint8_t)SETTINGS_SOCD_RESOLUTION_LAST_INPUT_WINS;
  }
  return resolution;
}

static inline bool hid_is_valid_socd_pair(uint8_t key_index, uint8_t socd_pair) {
  return socd_pair == SETTINGS_SOCD_PAIR_NONE ||
         (socd_pair < NUM_KEYS && socd_pair != key_index);
}

static inline uint8_t hid_socd_resolution_flags(uint8_t resolution) {
  return (uint8_t)(hid_sanitize_socd_resolution(resolution) << 2);
}

static inline uint8_t hid_socd_resolution_from_flags(uint8_t flags) {
  return hid_sanitize_socd_resolution((uint8_t)((flags >> 2) & 0x07u));
}

static inline uint8_t hid_sanitize_dks_bottom_out_point(uint8_t point_tenths) {
  if (point_tenths < SETTINGS_DKS_BOTTOM_OUT_POINT_MIN_TENTHS ||
      point_tenths > SETTINGS_DKS_BOTTOM_OUT_POINT_MAX_TENTHS) {
    return SETTINGS_DKS_BOTTOM_OUT_POINT_DEFAULT_TENTHS;
  }

  return point_tenths;
}

static inline uint8_t
hid_sanitize_socd_fully_pressed_point(uint8_t point_tenths) {
  if (point_tenths < SETTINGS_SOCD_FULLY_PRESSED_POINT_MIN_TENTHS ||
      point_tenths > SETTINGS_SOCD_FULLY_PRESSED_POINT_MAX_TENTHS) {
    return SETTINGS_SOCD_FULLY_PRESSED_POINT_DEFAULT_TENTHS;
  }

  return point_tenths;
}

static inline uint8_t hid_gamepad_curve_x_01mm_to_deadzone(uint16_t x_01mm) {
  uint32_t scaled = ((uint32_t)x_01mm * 255u +
                     (GAMEPAD_CURVE_MAX_DISTANCE_01MM / 2u)) /
                    GAMEPAD_CURVE_MAX_DISTANCE_01MM;
  if (scaled > 255u) {
    scaled = 255u;
  }
  return (uint8_t)scaled;
}

static inline uint16_t hid_gamepad_deadzone_to_curve_x_01mm(uint8_t deadzone) {
  uint32_t scaled =
      ((uint32_t)deadzone * GAMEPAD_CURVE_MAX_DISTANCE_01MM + 127u) / 255u;
  if (scaled > GAMEPAD_CURVE_MAX_DISTANCE_01MM) {
    scaled = GAMEPAD_CURVE_MAX_DISTANCE_01MM;
  }
  return (uint16_t)scaled;
}

static inline uint8_t hid_key_flags_from_settings(const settings_key_t *key) {
  uint8_t flags = 0;
  if (key->rapid_trigger_enabled) {
    flags |= 0x01;
  }
  if (key->disable_kb_on_gamepad) {
    flags |= 0x02;
  }
  flags |= hid_socd_resolution_flags(
      (uint8_t)settings_key_get_socd_resolution(key));
  return flags;
}

static inline void hid_fill_key_settings_chunk_entry(
    hid_key_settings_chunk_entry_t *entry, const settings_key_t *key) {
  entry->hid_keycode = key->hid_keycode;
  entry->actuation_point_mm = key->actuation_point_mm;
  entry->release_point_mm = key->release_point_mm;
  entry->rapid_trigger_press = key->rapid_trigger_press;
  entry->rapid_trigger_release = key->rapid_trigger_release;
  entry->socd_pair = key->socd_pair;
  entry->flags = hid_key_flags_from_settings(key);
}

static inline void hid_fill_key_settings_packet(hid_packet_key_settings_t *resp,
                                                uint8_t profile_index,
                                                uint8_t layer_index,
                                                uint8_t key_index,
                                                const settings_key_t *key) {
  resp->key_index = key_index;
  resp->profile_index = profile_index;
  resp->layer_index = layer_index;
  resp->hid_keycode = key->hid_keycode;
  resp->actuation_point_mm = key->actuation_point_mm;
  resp->release_point_mm = key->release_point_mm;
  resp->rapid_trigger_press = key->rapid_trigger_press;
  resp->rapid_trigger_release = key->rapid_trigger_release;
  resp->socd_pair = key->socd_pair;
  resp->socd_resolution = (uint8_t)settings_key_get_socd_resolution(key);
  resp->rapid_trigger_enabled = key->rapid_trigger_enabled;
  resp->disable_kb_on_gamepad = key->disable_kb_on_gamepad;
  resp->continuous_rapid_trigger =
      settings_key_is_continuous_rapid_trigger_enabled(key) ? 1u : 0u;
  resp->behavior_mode = key->advanced.behavior_mode;
  resp->hold_threshold_10ms = key->advanced.hold_threshold_10ms;
  resp->secondary_hid_keycode = key->advanced.secondary_hid_keycode;
  memcpy(resp->dynamic_zones, key->advanced.dynamic_zones,
         sizeof(resp->dynamic_zones));
  resp->tap_hold_options = 0u;
  if (settings_key_is_tap_hold_hold_on_other_key_press(key)) {
    resp->tap_hold_options |= SETTINGS_KEY_ADV_TAP_HOLD_HOLD_ON_OTHER_MASK;
  }
  if (settings_key_is_tap_hold_uppercase_hold(key)) {
    resp->tap_hold_options |= SETTINGS_KEY_ADV_TAP_HOLD_UPPERCASE_HOLD_MASK;
  }
  resp->dks_bottom_out_point = key->advanced.dks_bottom_out_point_tenths;
  resp->socd_fully_pressed_enabled =
      settings_key_is_socd_fully_pressed_enabled(key) ? 1u : 0u;
  resp->socd_fully_pressed_point =
      key->advanced.socd_fully_pressed_point_tenths;
}

#define HID_DEVICE_SERIAL_PREFIX "75HE-NF-"
#define HID_DEVICE_UID_BYTES 12u
#define HID_DEVICE_UID_B62_CHARS 17u

_Static_assert((sizeof(HID_DEVICE_SERIAL_PREFIX) - 1u + HID_DEVICE_UID_B62_CHARS +
                1u) <= HID_DEVICE_SERIAL_MAX_LEN,
               "HID_DEVICE_SERIAL_MAX_LEN too small for configured prefix");

static char s_device_serial_cache[HID_DEVICE_SERIAL_MAX_LEN];

static void hid_encode_uid_base62_12(const uint8_t *uid_bytes, char *out_b62) {
  static const char base62_table[] =
      "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
  uint8_t work[HID_DEVICE_UID_BYTES];

  for (uint8_t i = 0u; i < HID_DEVICE_UID_BYTES; ++i) {
    work[i] = uid_bytes[HID_DEVICE_UID_BYTES - 1u - i];
  }

  for (int8_t out_index = (int8_t)(HID_DEVICE_UID_B62_CHARS - 1u);
       out_index >= 0; --out_index) {
    uint16_t remainder = 0u;
    for (uint8_t i = 0u; i < HID_DEVICE_UID_BYTES; ++i) {
      uint16_t acc = (uint16_t)(remainder * 256u + work[i]);
      work[i] = (uint8_t)(acc / 62u);
      remainder = (uint16_t)(acc % 62u);
    }
    out_b62[out_index] = base62_table[remainder];
  }

  out_b62[HID_DEVICE_UID_B62_CHARS] = '\0';
}

static void hid_prepare_device_serial_cache(void) {
  uint32_t uid0 = HAL_GetUIDw0();
  uint32_t uid1 = HAL_GetUIDw1();
  uint32_t uid2 = HAL_GetUIDw2();
  uint8_t uid_bytes[HID_DEVICE_UID_BYTES] = {
      (uint8_t)(uid0 & 0xFFu),
      (uint8_t)((uid0 >> 8u) & 0xFFu),
      (uint8_t)((uid0 >> 16u) & 0xFFu),
      (uint8_t)((uid0 >> 24u) & 0xFFu),
      (uint8_t)(uid1 & 0xFFu),
      (uint8_t)((uid1 >> 8u) & 0xFFu),
      (uint8_t)((uid1 >> 16u) & 0xFFu),
      (uint8_t)((uid1 >> 24u) & 0xFFu),
      (uint8_t)(uid2 & 0xFFu),
      (uint8_t)((uid2 >> 8u) & 0xFFu),
      (uint8_t)((uid2 >> 16u) & 0xFFu),
      (uint8_t)((uid2 >> 24u) & 0xFFu),
  };
  char uid_b62[HID_DEVICE_UID_B62_CHARS + 1u];
  const uint8_t prefix_len = (uint8_t)(sizeof(HID_DEVICE_SERIAL_PREFIX) - 1u);

  memset(s_device_serial_cache, 0, sizeof(s_device_serial_cache));
  hid_encode_uid_base62_12(uid_bytes, uid_b62);
  memcpy(s_device_serial_cache, HID_DEVICE_SERIAL_PREFIX, prefix_len);
  memcpy(&s_device_serial_cache[prefix_len], uid_b62, HID_DEVICE_UID_B62_CHARS);
}

//--------------------------------------------------------------------+
// Internal Functions - System Commands
//--------------------------------------------------------------------+

static void cmd_get_firmware_version(const uint8_t *in, uint8_t *out) {
  hid_resp_firmware_version_t *resp = (hid_resp_firmware_version_t *)out;
  resp->command_id = CMD_GET_FIRMWARE_VERSION;
  resp->status = HID_RESP_OK;
  resp->version = settings_get_firmware_version();
}

static void cmd_factory_reset(const uint8_t *in, uint8_t *out) {
  hid_packet_t *resp = (hid_packet_t *)out;
  (void)in;

  resp->command_id = CMD_FACTORY_RESET;

  bool success = settings_reset();
  if (success) {
    // Keep trigger-side offsets in sync with settings defaults after reset.
    refresh_runtime_calibration();
  }

  resp->status_or_len = success ? HID_RESP_OK : HID_RESP_ERROR;
}

static void cmd_usb_reenumerate(const uint8_t *in, uint8_t *out) {
  hid_packet_t *resp = (hid_packet_t *)out;
  (void)in;
  resp->command_id = CMD_USB_REENUMERATE;
  resp->status_or_len =
      updater_app_schedule_action(UPDATER_APP_ACTION_USB_REENUMERATE)
          ? HID_RESP_OK
          : HID_RESP_ERROR;
}

static void cmd_reboot(const uint8_t *in, uint8_t *out) {
  hid_packet_t *resp = (hid_packet_t *)out;
  (void)in;
  resp->command_id = CMD_REBOOT;
  resp->status_or_len = updater_app_schedule_action(UPDATER_APP_ACTION_REBOOT)
                            ? HID_RESP_OK
                            : HID_RESP_ERROR;
}

static void cmd_enter_bootloader(const uint8_t *in, uint8_t *out) {
  hid_packet_t *resp = (hid_packet_t *)out;
  (void)in;
  resp->command_id = CMD_ENTER_BOOTLOADER;

  if (settings_has_unsaved_changes() && !settings_save()) {
    resp->status_or_len = HID_RESP_ERROR;
    return;
  }

  resp->status_or_len =
      updater_app_schedule_action(UPDATER_APP_ACTION_ENTER_UPDATER)
          ? HID_RESP_OK
          : HID_RESP_ERROR;
}

//--------------------------------------------------------------------+
// Internal Functions - Settings Commands
//--------------------------------------------------------------------+

static void cmd_get_options(const uint8_t *in, uint8_t *out) {
  hid_packet_options_t *resp = (hid_packet_options_t *)out;
  settings_options_t opts = settings_get_options();

  resp->command_id = CMD_GET_OPTIONS;
  resp->status = HID_RESP_OK;
  resp->keyboard_enabled = opts.keyboard_enabled;
  resp->gamepad_enabled = opts.gamepad_enabled;
  resp->raw_hid_echo = opts.raw_hid_echo;
  resp->led_thermal_protection_enabled =
      opts.led_thermal_protection_enabled;
}

static void cmd_set_options(const uint8_t *in, uint8_t *out) {
  const hid_packet_options_t *req = (const hid_packet_options_t *)in;
  hid_packet_options_t *resp = (hid_packet_options_t *)out;

  settings_options_t opts = settings_get_options();
  opts.keyboard_enabled = req->keyboard_enabled ? 1 : 0;
  opts.gamepad_enabled = req->gamepad_enabled ? 1 : 0;
  opts.raw_hid_echo = req->raw_hid_echo ? 1 : 0;
  opts.led_thermal_protection_enabled =
      req->led_thermal_protection_enabled ? 1 : 0;

  bool success = settings_set_options(opts);

  resp->command_id = CMD_SET_OPTIONS;
  resp->status = success ? HID_RESP_OK : HID_RESP_ERROR;
  resp->keyboard_enabled = opts.keyboard_enabled;
  resp->gamepad_enabled = opts.gamepad_enabled;
  resp->raw_hid_echo = opts.raw_hid_echo;
  resp->led_thermal_protection_enabled =
      opts.led_thermal_protection_enabled;
}

static void cmd_get_keyboard_enabled(const uint8_t *in, uint8_t *out) {
  hid_packet_bool_t *resp = (hid_packet_bool_t *)out;
  resp->command_id = CMD_GET_KEYBOARD_ENABLED;
  resp->status = HID_RESP_OK;
  resp->value = settings_is_keyboard_enabled() ? 1 : 0;
}

static void cmd_set_keyboard_enabled(const uint8_t *in, uint8_t *out) {
  const hid_packet_bool_t *req = (const hid_packet_bool_t *)in;
  hid_packet_bool_t *resp = (hid_packet_bool_t *)out;

  bool success = settings_set_keyboard_enabled(req->value != 0);

  resp->command_id = CMD_SET_KEYBOARD_ENABLED;
  resp->status = success ? HID_RESP_OK : HID_RESP_ERROR;
  resp->value = settings_is_keyboard_enabled() ? 1 : 0;
}

static void cmd_get_gamepad_enabled(const uint8_t *in, uint8_t *out) {
  hid_packet_bool_t *resp = (hid_packet_bool_t *)out;
  resp->command_id = CMD_GET_GAMEPAD_ENABLED;
  resp->status = HID_RESP_OK;
  resp->value = settings_is_gamepad_enabled() ? 1 : 0;
}

static void cmd_set_gamepad_enabled(const uint8_t *in, uint8_t *out) {
  const hid_packet_bool_t *req = (const hid_packet_bool_t *)in;
  hid_packet_bool_t *resp = (hid_packet_bool_t *)out;

  bool success = settings_set_gamepad_enabled(req->value != 0);

  resp->command_id = CMD_SET_GAMEPAD_ENABLED;
  resp->status = success ? HID_RESP_OK : HID_RESP_ERROR;
  resp->value = settings_is_gamepad_enabled() ? 1 : 0;
}

static void cmd_save_settings(const uint8_t *in, uint8_t *out) {
  hid_packet_t *resp = (hid_packet_t *)out;
  (void)in;
  resp->command_id = CMD_SAVE_SETTINGS;
  resp->status_or_len = settings_request_save() ? HID_RESP_OK : HID_RESP_ERROR;
}

static void cmd_get_nkro_enabled(const uint8_t *in, uint8_t *out) {
  hid_packet_bool_t *resp = (hid_packet_bool_t *)out;
  resp->command_id = CMD_GET_NKRO_ENABLED;
  resp->status = HID_RESP_OK;
  resp->value = settings_is_nkro_enabled() ? 1 : 0;
}

static void cmd_set_nkro_enabled(const uint8_t *in, uint8_t *out) {
  const hid_packet_bool_t *req = (const hid_packet_bool_t *)in;
  hid_packet_bool_t *resp = (hid_packet_bool_t *)out;

  bool success = settings_set_nkro_enabled(req->value != 0);

  resp->command_id = CMD_SET_NKRO_ENABLED;
  resp->status = success ? HID_RESP_OK : HID_RESP_ERROR;
  resp->value = settings_is_nkro_enabled() ? 1 : 0;
}

static void cmd_get_advanced_tick_rate(const uint8_t *in, uint8_t *out) {
  hid_packet_tick_rate_t *resp = (hid_packet_tick_rate_t *)out;
  (void)in;

  resp->command_id = CMD_GET_ADVANCED_TICK_RATE;
  resp->status = HID_RESP_OK;
  resp->tick_rate = settings_get_advanced_tick_rate();
}

static void cmd_set_advanced_tick_rate(const uint8_t *in, uint8_t *out) {
  const hid_packet_tick_rate_t *req = (const hid_packet_tick_rate_t *)in;
  hid_packet_tick_rate_t *resp = (hid_packet_tick_rate_t *)out;
  bool success = settings_set_advanced_tick_rate(req->tick_rate);

  resp->command_id = CMD_SET_ADVANCED_TICK_RATE;
  resp->status = success ? HID_RESP_OK : HID_RESP_ERROR;
  resp->tick_rate = settings_get_advanced_tick_rate();
}

static void cmd_get_trigger_chatter_guard(const uint8_t *in, uint8_t *out) {
  hid_packet_trigger_chatter_guard_t *resp =
      (hid_packet_trigger_chatter_guard_t *)out;
  bool enabled = false;
  uint8_t duration_ms = 0u;
  (void)in;

  settings_get_trigger_chatter_guard(&enabled, &duration_ms);

  resp->command_id = CMD_GET_TRIGGER_CHATTER_GUARD;
  resp->status = HID_RESP_OK;
  resp->enabled = enabled ? 1u : 0u;
  resp->duration_ms = duration_ms;
}

static void cmd_set_trigger_chatter_guard(const uint8_t *in, uint8_t *out) {
  const hid_packet_trigger_chatter_guard_t *req =
      (const hid_packet_trigger_chatter_guard_t *)in;
  hid_packet_trigger_chatter_guard_t *resp =
      (hid_packet_trigger_chatter_guard_t *)out;
  bool enabled = false;
  uint8_t duration_ms = 0u;
  bool success =
      settings_set_trigger_chatter_guard(req->enabled != 0u, req->duration_ms);

  settings_get_trigger_chatter_guard(&enabled, &duration_ms);

  resp->command_id = CMD_SET_TRIGGER_CHATTER_GUARD;
  resp->status = success ? HID_RESP_OK : HID_RESP_INVALID_PARAM;
  resp->enabled = enabled ? 1u : 0u;
  resp->duration_ms = duration_ms;
}

static void cmd_get_device_info(const uint8_t *in, uint8_t *out) {
  hid_packet_device_info_t *resp = (hid_packet_device_info_t *)out;
  const char *name = settings_get_keyboard_name();
  (void)in;

  resp->command_id = CMD_GET_DEVICE_INFO;
  resp->status = HID_RESP_OK;
  resp->version = settings_get_firmware_version();
  memset(resp->serial, 0, sizeof(resp->serial));
  memset(resp->keyboard_name, 0, sizeof(resp->keyboard_name));
  memcpy(resp->serial, s_device_serial_cache, sizeof(resp->serial));
  memcpy(resp->keyboard_name, name, SETTINGS_KEYBOARD_NAME_LENGTH);
}

static void cmd_get_keyboard_name(const uint8_t *in, uint8_t *out) {
  hid_packet_keyboard_name_t *resp = (hid_packet_keyboard_name_t *)out;
  const char *name = settings_get_keyboard_name();
  (void)in;

  resp->command_id = CMD_GET_KEYBOARD_NAME;
  resp->status = HID_RESP_OK;
  memset(resp->keyboard_name, 0, sizeof(resp->keyboard_name));
  memcpy(resp->keyboard_name, name, SETTINGS_KEYBOARD_NAME_LENGTH);
}

static void cmd_set_keyboard_name(const uint8_t *in, uint8_t *out) {
  const hid_packet_keyboard_name_t *req = (const hid_packet_keyboard_name_t *)in;
  hid_packet_keyboard_name_t *resp = (hid_packet_keyboard_name_t *)out;
  const char *name = settings_get_keyboard_name();
  bool success =
      settings_set_keyboard_name(req->keyboard_name, SETTINGS_KEYBOARD_NAME_LENGTH);

  resp->command_id = CMD_SET_KEYBOARD_NAME;
  resp->status = success ? HID_RESP_OK : HID_RESP_ERROR;
  memset(resp->keyboard_name, 0, sizeof(resp->keyboard_name));
  memcpy(resp->keyboard_name, name, SETTINGS_KEYBOARD_NAME_LENGTH);
}

static void cmd_copy_profile_slot(const uint8_t *in, uint8_t *out) {
  const hid_packet_profile_copy_t *req = (const hid_packet_profile_copy_t *)in;
  hid_packet_profile_copy_t *resp = (hid_packet_profile_copy_t *)out;

  resp->command_id = CMD_COPY_PROFILE_SLOT;
  resp->source_profile_index = req->source_profile_index;
  resp->target_profile_index = req->target_profile_index;
  resp->profile_used_mask = settings_get_profile_used_mask();

  if (req->source_profile_index >= SETTINGS_PROFILE_COUNT ||
      req->target_profile_index >= SETTINGS_PROFILE_COUNT ||
      !settings_is_profile_slot_used(req->source_profile_index)) {
    resp->status = HID_RESP_INVALID_PARAM;
    return;
  }

  resp->status = settings_copy_profile_slot(req->source_profile_index,
                                            req->target_profile_index)
                     ? HID_RESP_OK
                     : HID_RESP_ERROR;
  resp->profile_used_mask = settings_get_profile_used_mask();
}

static void cmd_reset_profile_slot(const uint8_t *in, uint8_t *out) {
  const hid_packet_profile_index_t *req = (const hid_packet_profile_index_t *)in;
  hid_packet_profile_index_t *resp = (hid_packet_profile_index_t *)out;

  resp->command_id = CMD_RESET_PROFILE_SLOT;
  resp->profile_index = req->profile_index;
  resp->profile_used_mask = settings_get_profile_used_mask();

  if (req->profile_index >= SETTINGS_PROFILE_COUNT ||
      !settings_is_profile_slot_used(req->profile_index)) {
    resp->status = HID_RESP_INVALID_PARAM;
    return;
  }

  resp->status = settings_reset_profile_slot(req->profile_index)
                     ? HID_RESP_OK
                     : HID_RESP_ERROR;
  resp->profile_used_mask = settings_get_profile_used_mask();
}

static void cmd_get_default_profile(const uint8_t *in, uint8_t *out) {
  (void)in;
  hid_packet_profile_index_t *resp = (hid_packet_profile_index_t *)out;
  resp->command_id     = CMD_GET_DEFAULT_PROFILE;
  resp->status         = HID_RESP_OK;
  resp->profile_index  = settings_get_default_profile_index();
  resp->profile_used_mask = settings_get_profile_used_mask();
}

static void cmd_set_default_profile(const uint8_t *in, uint8_t *out) {
  const hid_packet_profile_index_t *req = (const hid_packet_profile_index_t *)in;
  hid_packet_profile_index_t       *resp = (hid_packet_profile_index_t *)out;
  resp->command_id = CMD_SET_DEFAULT_PROFILE;

  uint8_t idx = req->profile_index;
  if (idx != SETTINGS_DEFAULT_PROFILE_NONE &&
      (idx >= SETTINGS_PROFILE_COUNT || !settings_is_profile_slot_used(idx))) {
    resp->status = HID_RESP_INVALID_PARAM;
    resp->profile_index = settings_get_default_profile_index();
    resp->profile_used_mask = settings_get_profile_used_mask();
    return;
  }

  resp->status = settings_set_default_profile_index(idx)
                     ? HID_RESP_OK
                     : HID_RESP_ERROR;
  resp->profile_index  = settings_get_default_profile_index();
  resp->profile_used_mask = settings_get_profile_used_mask();
}

static void cmd_get_ram_only_mode(const uint8_t *in, uint8_t *out) {
  (void)in;
  hid_packet_t *resp = (hid_packet_t *)out;
  resp->command_id     = CMD_GET_RAM_ONLY_MODE;
  resp->status_or_len  = HID_RESP_OK;
  resp->payload[0]     = settings_is_ram_only_mode() ? 1u : 0u;
}

static void cmd_set_ram_only_mode(const uint8_t *in, uint8_t *out) {
  const hid_packet_t *req  = (const hid_packet_t *)in;
  hid_packet_t       *resp = (hid_packet_t *)out;
  resp->command_id = CMD_SET_RAM_ONLY_MODE;

  if (req->payload[0]) {
    settings_enter_ram_only_mode();
    resp->status_or_len = HID_RESP_OK;
  } else {
    /* enable=0 exits RAM-only mode and reloads the last-saved flash state,
     * identical to CMD_RELOAD_SETTINGS_FROM_FLASH. */
    bool ok = settings_exit_ram_only_mode();
    resp->status_or_len = ok ? HID_RESP_OK : HID_RESP_ERROR;
  }

  resp->payload[0] = settings_is_ram_only_mode() ? 1u : 0u;
}

static void cmd_reload_settings_from_flash(const uint8_t *in, uint8_t *out) {
  (void)in;
  hid_packet_t *resp = (hid_packet_t *)out;
  resp->command_id    = CMD_RELOAD_SETTINGS_FROM_FLASH;

  bool ok = settings_exit_ram_only_mode();
  resp->status_or_len = ok ? HID_RESP_OK : HID_RESP_ERROR;
  resp->payload[0]    = settings_is_ram_only_mode() ? 1u : 0u;
}

//--------------------------------------------------------------------+
// Internal Functions - Key Settings Commands
//--------------------------------------------------------------------+

static void cmd_get_key_settings(const uint8_t *in, uint8_t *out) {
  const hid_packet_key_settings_t *req = (const hid_packet_key_settings_t *)in;
  hid_packet_key_settings_t *resp = (hid_packet_key_settings_t *)out;
  settings_key_t key = {0};

  resp->command_id = CMD_GET_KEY_SETTINGS;

  if (req->key_index >= NUM_KEYS || req->profile_index >= SETTINGS_PROFILE_COUNT ||
      req->layer_index >= SETTINGS_LAYER_COUNT ||
      !settings_is_profile_slot_used(req->profile_index)) {
    resp->status = HID_RESP_INVALID_PARAM;
    return;
  }

  if (!settings_get_profile_layer_key_settings(req->profile_index,
                                               req->layer_index,
                                               req->key_index, &key)) {
    resp->status = HID_RESP_ERROR;
    return;
  }

  resp->status = HID_RESP_OK;
  hid_fill_key_settings_packet(resp, req->profile_index, req->layer_index,
                               req->key_index, &key);
}

static void cmd_set_key_settings(const uint8_t *in, uint8_t *out) {
  const hid_packet_key_settings_t *req = (const hid_packet_key_settings_t *)in;
  hid_packet_key_settings_t *resp = (hid_packet_key_settings_t *)out;
  settings_key_t key = {0};

  resp->command_id = CMD_SET_KEY_SETTINGS;

  if (req->key_index >= NUM_KEYS || req->profile_index >= SETTINGS_PROFILE_COUNT ||
      req->layer_index >= SETTINGS_LAYER_COUNT ||
      !settings_is_profile_slot_used(req->profile_index)) {
    resp->status = HID_RESP_INVALID_PARAM;
    return;
  }
  if (!hid_is_valid_socd_pair(req->key_index, req->socd_pair) ||
      req->socd_resolution >= (uint8_t)SETTINGS_SOCD_RESOLUTION_MAX ||
      req->behavior_mode >= (uint8_t)KEY_BEHAVIOR_MAX) {
    resp->status = HID_RESP_INVALID_PARAM;
    return;
  }

  if (!settings_get_profile_layer_key_settings(req->profile_index,
                                               req->layer_index,
                                               req->key_index, &key)) {
    resp->status = HID_RESP_ERROR;
    return;
  }

  key.hid_keycode = req->hid_keycode;
  key.actuation_point_mm = req->actuation_point_mm;
  key.release_point_mm = req->release_point_mm;
  key.rapid_trigger_press = req->rapid_trigger_press;
  key.rapid_trigger_release = req->rapid_trigger_release;
  key.socd_pair = req->socd_pair;
  settings_key_set_socd_resolution(
      &key, (settings_socd_resolution_t)req->socd_resolution);
  key.rapid_trigger_enabled = req->rapid_trigger_enabled ? 1 : 0;
  key.disable_kb_on_gamepad = req->disable_kb_on_gamepad ? 1 : 0;
  settings_key_set_continuous_rapid_trigger(
      &key, req->continuous_rapid_trigger != 0u);
    settings_key_set_socd_fully_pressed_enabled(
      &key, req->socd_fully_pressed_enabled != 0u);
  key.advanced.behavior_mode = req->behavior_mode;
  key.advanced.hold_threshold_10ms = req->hold_threshold_10ms;
  if (req->dks_bottom_out_point != 0u) {
    key.advanced.dks_bottom_out_point_tenths =
        hid_sanitize_dks_bottom_out_point(req->dks_bottom_out_point);
  }
  key.advanced.secondary_hid_keycode = req->secondary_hid_keycode;
    settings_key_set_tap_hold_hold_on_other_key_press(
      &key, (req->tap_hold_options &
         SETTINGS_KEY_ADV_TAP_HOLD_HOLD_ON_OTHER_MASK) != 0u);
    settings_key_set_tap_hold_uppercase_hold(
      &key, (req->tap_hold_options &
         SETTINGS_KEY_ADV_TAP_HOLD_UPPERCASE_HOLD_MASK) != 0u);
    key.advanced.socd_fully_pressed_point_tenths =
      hid_sanitize_socd_fully_pressed_point(req->socd_fully_pressed_point);
  memcpy(key.advanced.dynamic_zones, req->dynamic_zones,
         sizeof(key.advanced.dynamic_zones));

  bool success = settings_set_profile_layer_key_settings(
      req->profile_index, req->layer_index, req->key_index, &key);

  resp->status = success ? HID_RESP_OK : HID_RESP_ERROR;
  if (!settings_get_profile_layer_key_settings(req->profile_index,
                                               req->layer_index,
                                               req->key_index, &key)) {
    resp->status = HID_RESP_ERROR;
    return;
  }

  hid_fill_key_settings_packet(resp, req->profile_index, req->layer_index,
                               req->key_index, &key);
}

static void cmd_reset_key_trigger_settings(const uint8_t *in, uint8_t *out) {
  const hid_packet_key_index_t *req = (const hid_packet_key_index_t *)in;
  hid_packet_key_settings_t *resp = (hid_packet_key_settings_t *)out;

  resp->command_id = CMD_RESET_KEY_TRIGGER_SETTINGS;

  if (req->key_index >= NUM_KEYS) {
    resp->status = HID_RESP_INVALID_PARAM;
    return;
  }

  if (!settings_reset_key_trigger_settings(req->key_index)) {
    resp->status = HID_RESP_ERROR;
    return;
  }

  const settings_key_t *key = settings_get_key(req->key_index);
  if (key == NULL) {
    resp->status = HID_RESP_ERROR;
    return;
  }

  resp->status = HID_RESP_OK;
  hid_fill_key_settings_packet(resp, settings_get_active_profile_index(), 0u,
                               req->key_index, key);
}

static void cmd_get_all_key_settings(const uint8_t *in, uint8_t *out) {
  const hid_packet_all_keys_t *req = (const hid_packet_all_keys_t *)in;
  hid_packet_all_keys_t *resp = (hid_packet_all_keys_t *)out;

  resp->command_id = CMD_GET_ALL_KEY_SETTINGS;
  resp->start_index = req->start_index;
  resp->key_count = 0;

  if (req->start_index >= NUM_KEYS) {
    resp->status = HID_RESP_INVALID_PARAM;
    return;
  }

  const settings_t *s = settings_get();
  uint8_t count = (uint8_t)(NUM_KEYS - req->start_index);
  if (count > HID_KEY_SETTINGS_PER_CHUNK) {
    count = HID_KEY_SETTINGS_PER_CHUNK;
  }

  resp->status = HID_RESP_OK;
  resp->key_count = count;
  memset(resp->reserved, 0, sizeof(resp->reserved));
  for (uint8_t i = 0; i < count; i++) {
    hid_fill_key_settings_chunk_entry(
        &resp->keys[i], &s->keys[req->start_index + i]);
  }
}

static void cmd_set_all_key_settings(const uint8_t *in, uint8_t *out) {
  const hid_packet_all_keys_t *req = (const hid_packet_all_keys_t *)in;
  hid_packet_all_keys_t *resp = (hid_packet_all_keys_t *)out;

  resp->command_id = CMD_SET_ALL_KEY_SETTINGS;

  if (req->start_index >= NUM_KEYS || req->key_count == 0 ||
      req->key_count > HID_KEY_SETTINGS_PER_CHUNK ||
      (uint16_t)req->start_index + req->key_count > NUM_KEYS) {
    resp->status = HID_RESP_INVALID_PARAM;
    resp->start_index = req->start_index;
    resp->key_count = 0;
    return;
  }

  bool success = true;
  resp->start_index = req->start_index;
  resp->key_count = req->key_count;

  for (uint8_t i = 0; i < req->key_count; i++) {
    uint8_t key_index = (uint8_t)(req->start_index + i);
    const settings_key_t *current_key = settings_get_key(key_index);
    if (current_key == NULL) {
      success = false;
      continue;
    }

    settings_key_t key = *current_key;
    uint8_t socd_resolution = hid_socd_resolution_from_flags(req->keys[i].flags);

    if (!hid_is_valid_socd_pair(key_index, req->keys[i].socd_pair)) {
      success = false;
      hid_fill_key_settings_chunk_entry(&resp->keys[i], current_key);
      continue;
    }

    key.hid_keycode = req->keys[i].hid_keycode;
    key.actuation_point_mm = req->keys[i].actuation_point_mm;
    key.release_point_mm = req->keys[i].release_point_mm;
    key.rapid_trigger_press = req->keys[i].rapid_trigger_press;
    key.rapid_trigger_release = req->keys[i].rapid_trigger_release;
    key.socd_pair = req->keys[i].socd_pair;
    settings_key_set_socd_resolution(
        &key, (settings_socd_resolution_t)socd_resolution);
    key.rapid_trigger_enabled = (req->keys[i].flags & 0x01u) ? 1 : 0;
    key.disable_kb_on_gamepad = (req->keys[i].flags & 0x02u) ? 1 : 0;

    if (!settings_set_key(key_index, &key)) {
      success = false;
    }

    hid_fill_key_settings_chunk_entry(&resp->keys[i], &key);
  }

  resp->status = success ? HID_RESP_OK : HID_RESP_ERROR;
}

static void cmd_get_gamepad_settings(const uint8_t *in, uint8_t *out) {
  hid_packet_gamepad_settings_t *resp = (hid_packet_gamepad_settings_t *)out;
  const settings_t *s = settings_get();

  resp->command_id = CMD_GET_GAMEPAD_SETTINGS;
  resp->status = HID_RESP_OK;
  resp->keyboard_routing = s->gamepad.keyboard_routing;
  resp->square_mode = s->gamepad.square_mode;
  resp->reactive_stick = s->gamepad.reactive_stick;
  resp->api_mode = s->gamepad.api_mode;
  for (uint8_t i = 0; i < GAMEPAD_CURVE_POINT_COUNT; i++) {
    resp->curve[i].x_01mm = s->gamepad.curve[i].x_01mm;
    resp->curve[i].y = s->gamepad.curve[i].y;
  }
}

static void cmd_set_gamepad_settings(const uint8_t *in, uint8_t *out) {
  const hid_packet_gamepad_settings_t *req =
      (const hid_packet_gamepad_settings_t *)in;
  hid_packet_gamepad_settings_t *resp = (hid_packet_gamepad_settings_t *)out;
  settings_gamepad_t gamepad;
  bool invalid = req->keyboard_routing >= (uint8_t)GAMEPAD_KEYBOARD_ROUTING_MAX ||
                 req->api_mode >= (uint8_t)GAMEPAD_API_MAX;

  resp->command_id = CMD_SET_GAMEPAD_SETTINGS;

  gamepad.keyboard_routing = req->keyboard_routing;
  gamepad.square_mode = req->square_mode ? 1 : 0;
  gamepad.reactive_stick = req->reactive_stick ? 1 : 0;
  gamepad.api_mode = req->api_mode;
  for (uint8_t i = 0; i < GAMEPAD_CURVE_POINT_COUNT; i++) {
    gamepad.curve[i].x_01mm = req->curve[i].x_01mm;
    gamepad.curve[i].y = req->curve[i].y;
    if (gamepad.curve[i].x_01mm > GAMEPAD_CURVE_MAX_DISTANCE_01MM) {
      invalid = true;
    }
    if (i > 0u && gamepad.curve[i].x_01mm < gamepad.curve[i - 1u].x_01mm) {
      invalid = true;
    }
  }

  if (invalid) {
    resp->status = HID_RESP_INVALID_PARAM;
    return;
  }

  bool success = settings_set_gamepad(&gamepad);

  resp->status = success ? HID_RESP_OK : HID_RESP_ERROR;
  resp->keyboard_routing = gamepad.keyboard_routing;
  resp->square_mode = gamepad.square_mode;
  resp->reactive_stick = gamepad.reactive_stick;
  resp->api_mode = gamepad.api_mode;
  for (uint8_t i = 0; i < GAMEPAD_CURVE_POINT_COUNT; i++) {
    resp->curve[i].x_01mm = gamepad.curve[i].x_01mm;
    resp->curve[i].y = gamepad.curve[i].y;
  }
}

static void cmd_get_rotary_encoder_settings(const uint8_t *in, uint8_t *out) {
  hid_packet_rotary_encoder_settings_t *resp =
      (hid_packet_rotary_encoder_settings_t *)out;
  settings_rotary_encoder_t rotary = {0};
  (void)in;

  settings_get_rotary_encoder(&rotary);

  resp->command_id = CMD_GET_ROTARY_ENCODER_SETTINGS;
  resp->status = HID_RESP_OK;
  resp->rotation_action = rotary.rotation_action;
  resp->button_action = rotary.button_action;
  resp->sensitivity = rotary.sensitivity;
  resp->step_size = rotary.step_size;
  resp->invert_direction = rotary.invert_direction;
  resp->rgb_behavior = rotary.rgb_behavior;
  resp->rgb_effect_mode = rotary.rgb_effect_mode;
  resp->progress_style = rotary.progress_style;
  resp->progress_effect_mode = rotary.progress_effect_mode;
  resp->progress_color_r = rotary.progress_color_r;
  resp->progress_color_g = rotary.progress_color_g;
  resp->progress_color_b = rotary.progress_color_b;
  resp->cw_mode = rotary.cw_binding.mode;
  resp->cw_keycode = rotary.cw_binding.keycode;
  resp->cw_modifier_mask_exact = rotary.cw_binding.modifier_mask_exact;
  resp->cw_fallback_no_mod_keycode = rotary.cw_binding.fallback_no_mod_keycode;
  resp->cw_layer_mode = rotary.cw_binding.layer_mode;
  resp->cw_layer_index = rotary.cw_binding.layer_index;
  resp->ccw_mode = rotary.ccw_binding.mode;
  resp->ccw_keycode = rotary.ccw_binding.keycode;
  resp->ccw_modifier_mask_exact = rotary.ccw_binding.modifier_mask_exact;
  resp->ccw_fallback_no_mod_keycode =
      rotary.ccw_binding.fallback_no_mod_keycode;
  resp->ccw_layer_mode = rotary.ccw_binding.layer_mode;
  resp->ccw_layer_index = rotary.ccw_binding.layer_index;
  resp->click_mode = rotary.click_binding.mode;
  resp->click_keycode = rotary.click_binding.keycode;
  resp->click_modifier_mask_exact = rotary.click_binding.modifier_mask_exact;
  resp->click_fallback_no_mod_keycode =
      rotary.click_binding.fallback_no_mod_keycode;
  resp->click_layer_mode = rotary.click_binding.layer_mode;
  resp->click_layer_index = rotary.click_binding.layer_index;
}

static void cmd_set_rotary_encoder_settings(const uint8_t *in, uint8_t *out) {
  const hid_packet_rotary_encoder_settings_t *req =
      (const hid_packet_rotary_encoder_settings_t *)in;
  hid_packet_rotary_encoder_settings_t *resp =
      (hid_packet_rotary_encoder_settings_t *)out;
  settings_rotary_encoder_t rotary = {0};
  bool invalid =
      req->rotation_action >= ROTARY_ACTION_MAX ||
      req->button_action >= ROTARY_BUTTON_ACTION_MAX ||
      req->sensitivity == 0u || req->sensitivity > 16u ||
      req->step_size == 0u || req->step_size > 64u ||
      req->rgb_behavior >= ROTARY_RGB_BEHAVIOR_MAX ||
      req->rgb_effect_mode >= LED_EFFECT_MAX ||
      req->rgb_effect_mode == LED_EFFECT_THIRD_PARTY ||
      req->progress_style >= ROTARY_PROGRESS_STYLE_MAX ||
      req->progress_effect_mode >= LED_EFFECT_MAX ||
      req->progress_effect_mode == LED_EFFECT_THIRD_PARTY ||
      req->cw_mode >= (uint8_t)ROTARY_BINDING_MODE_MAX ||
      req->cw_layer_mode >= (uint8_t)ROTARY_BINDING_LAYER_MAX ||
      req->cw_layer_index >= SETTINGS_LAYER_COUNT ||
      req->ccw_mode >= (uint8_t)ROTARY_BINDING_MODE_MAX ||
      req->ccw_layer_mode >= (uint8_t)ROTARY_BINDING_LAYER_MAX ||
      req->ccw_layer_index >= SETTINGS_LAYER_COUNT ||
      req->click_mode >= (uint8_t)ROTARY_BINDING_MODE_MAX ||
      req->click_layer_mode >= (uint8_t)ROTARY_BINDING_LAYER_MAX ||
      req->click_layer_index >= SETTINGS_LAYER_COUNT;

  resp->command_id = CMD_SET_ROTARY_ENCODER_SETTINGS;
  if (invalid) {
    resp->status = HID_RESP_INVALID_PARAM;
    return;
  }

  rotary.rotation_action = req->rotation_action;
  rotary.button_action = req->button_action;
  rotary.sensitivity = req->sensitivity;
  rotary.step_size = req->step_size;
  rotary.invert_direction = req->invert_direction ? 1u : 0u;
  rotary.rgb_behavior = req->rgb_behavior;
  rotary.rgb_effect_mode = req->rgb_effect_mode;
  rotary.progress_style = req->progress_style;
  rotary.progress_effect_mode = req->progress_effect_mode;
  rotary.progress_color_r = req->progress_color_r;
  rotary.progress_color_g = req->progress_color_g;
  rotary.progress_color_b = req->progress_color_b;
    rotary.cw_binding.mode = req->cw_mode;
    rotary.cw_binding.keycode = req->cw_keycode;
    rotary.cw_binding.modifier_mask_exact = req->cw_modifier_mask_exact;
    rotary.cw_binding.fallback_no_mod_keycode = req->cw_fallback_no_mod_keycode;
    rotary.cw_binding.layer_mode = req->cw_layer_mode;
    rotary.cw_binding.layer_index = req->cw_layer_index;
    rotary.ccw_binding.mode = req->ccw_mode;
    rotary.ccw_binding.keycode = req->ccw_keycode;
    rotary.ccw_binding.modifier_mask_exact = req->ccw_modifier_mask_exact;
    rotary.ccw_binding.fallback_no_mod_keycode =
      req->ccw_fallback_no_mod_keycode;
    rotary.ccw_binding.layer_mode = req->ccw_layer_mode;
    rotary.ccw_binding.layer_index = req->ccw_layer_index;
    rotary.click_binding.mode = req->click_mode;
    rotary.click_binding.keycode = req->click_keycode;
    rotary.click_binding.modifier_mask_exact = req->click_modifier_mask_exact;
    rotary.click_binding.fallback_no_mod_keycode =
      req->click_fallback_no_mod_keycode;
    rotary.click_binding.layer_mode = req->click_layer_mode;
    rotary.click_binding.layer_index = req->click_layer_index;

  if (!settings_set_rotary_encoder(&rotary)) {
    resp->status = HID_RESP_ERROR;
    return;
  }

  resp->status = HID_RESP_OK;
  resp->rotation_action = rotary.rotation_action;
  resp->button_action = rotary.button_action;
  resp->sensitivity = rotary.sensitivity;
  resp->step_size = rotary.step_size;
  resp->invert_direction = rotary.invert_direction;
  resp->rgb_behavior = rotary.rgb_behavior;
  resp->rgb_effect_mode = rotary.rgb_effect_mode;
  resp->progress_style = rotary.progress_style;
  resp->progress_effect_mode = rotary.progress_effect_mode;
  resp->progress_color_r = rotary.progress_color_r;
  resp->progress_color_g = rotary.progress_color_g;
  resp->progress_color_b = rotary.progress_color_b;
  resp->cw_mode = rotary.cw_binding.mode;
  resp->cw_keycode = rotary.cw_binding.keycode;
  resp->cw_modifier_mask_exact = rotary.cw_binding.modifier_mask_exact;
  resp->cw_fallback_no_mod_keycode = rotary.cw_binding.fallback_no_mod_keycode;
  resp->cw_layer_mode = rotary.cw_binding.layer_mode;
  resp->cw_layer_index = rotary.cw_binding.layer_index;
  resp->ccw_mode = rotary.ccw_binding.mode;
  resp->ccw_keycode = rotary.ccw_binding.keycode;
  resp->ccw_modifier_mask_exact = rotary.ccw_binding.modifier_mask_exact;
  resp->ccw_fallback_no_mod_keycode =
      rotary.ccw_binding.fallback_no_mod_keycode;
  resp->ccw_layer_mode = rotary.ccw_binding.layer_mode;
  resp->ccw_layer_index = rotary.ccw_binding.layer_index;
  resp->click_mode = rotary.click_binding.mode;
  resp->click_keycode = rotary.click_binding.keycode;
  resp->click_modifier_mask_exact = rotary.click_binding.modifier_mask_exact;
  resp->click_fallback_no_mod_keycode =
      rotary.click_binding.fallback_no_mod_keycode;
  resp->click_layer_mode = rotary.click_binding.layer_mode;
  resp->click_layer_index = rotary.click_binding.layer_index;
}

static void cmd_get_rotary_state(const uint8_t *in, uint8_t *out) {
  hid_packet_rotary_state_t *resp = (hid_packet_rotary_state_t *)out;
  (void)in;

  resp->command_id = CMD_GET_ROTARY_STATE;
  resp->status = HID_RESP_OK;
  resp->button_pressed = rotary_encoder_is_button_pressed() ? 1u : 0u;
  resp->last_direction = rotary_encoder_get_last_direction();
  resp->step_counter = rotary_encoder_get_step_counter();
}

static void cmd_get_active_profile(const uint8_t *in, uint8_t *out) {
  hid_packet_profile_index_t *resp = (hid_packet_profile_index_t *)out;
  (void)in;

  resp->command_id = CMD_GET_ACTIVE_PROFILE;
  resp->status = HID_RESP_OK;
  resp->profile_index = settings_get_active_profile_index();
  resp->profile_used_mask = settings_get_profile_used_mask();
}

static void cmd_set_active_profile(const uint8_t *in, uint8_t *out) {
  const hid_packet_profile_index_t *req =
      (const hid_packet_profile_index_t *)in;
  hid_packet_profile_index_t *resp = (hid_packet_profile_index_t *)out;

  resp->command_id = CMD_SET_ACTIVE_PROFILE;
  resp->profile_index = req->profile_index;
  resp->profile_used_mask = settings_get_profile_used_mask();

  if (req->profile_index >= SETTINGS_PROFILE_COUNT ||
      !settings_is_profile_slot_used(req->profile_index)) {
    resp->status = HID_RESP_INVALID_PARAM;
    return;
  }

  resp->status = settings_set_active_profile_index(req->profile_index)
                     ? HID_RESP_OK
                     : HID_RESP_ERROR;
  resp->profile_index = settings_get_active_profile_index();
  resp->profile_used_mask = settings_get_profile_used_mask();
}

static void cmd_get_profile_name(const uint8_t *in, uint8_t *out) {
  const hid_packet_profile_name_t *req = (const hid_packet_profile_name_t *)in;
  hid_packet_profile_name_t *resp = (hid_packet_profile_name_t *)out;
  const char *name = NULL;

  resp->command_id = CMD_GET_PROFILE_NAME;
  resp->profile_index = req->profile_index;
  resp->profile_used_mask = settings_get_profile_used_mask();
  memset(resp->profile_name, 0, sizeof(resp->profile_name));

  if (req->profile_index >= SETTINGS_PROFILE_COUNT ||
      !settings_is_profile_slot_used(req->profile_index)) {
    resp->status = HID_RESP_INVALID_PARAM;
    return;
  }

  name = settings_get_profile_name(req->profile_index);
  if (name == NULL) {
    resp->status = HID_RESP_ERROR;
    return;
  }

  for (uint8_t i = 0u; i < SETTINGS_PROFILE_NAME_LENGTH; i++) {
    char c = name[i];
    resp->profile_name[i] = c;
    if (c == '\0') {
      break;
    }
  }

  resp->status = HID_RESP_OK;
}

static void cmd_set_profile_name(const uint8_t *in, uint8_t *out) {
  const hid_packet_profile_name_t *req = (const hid_packet_profile_name_t *)in;
  hid_packet_profile_name_t *resp = (hid_packet_profile_name_t *)out;
  const char *name = NULL;

  resp->command_id = CMD_SET_PROFILE_NAME;
  resp->profile_index = req->profile_index;
  resp->profile_used_mask = settings_get_profile_used_mask();
  memset(resp->profile_name, 0, sizeof(resp->profile_name));

  if (req->profile_index >= SETTINGS_PROFILE_COUNT ||
      !settings_is_profile_slot_used(req->profile_index)) {
    resp->status = HID_RESP_INVALID_PARAM;
    return;
  }

  if (!settings_set_profile_name(req->profile_index, req->profile_name,
                                 SETTINGS_PROFILE_NAME_LENGTH)) {
    resp->status = HID_RESP_ERROR;
    return;
  }

  name = settings_get_profile_name(req->profile_index);
  if (name == NULL) {
    resp->status = HID_RESP_ERROR;
    return;
  }

  for (uint8_t i = 0u; i < SETTINGS_PROFILE_NAME_LENGTH; i++) {
    char c = name[i];
    resp->profile_name[i] = c;
    if (c == '\0') {
      break;
    }
  }

  resp->status = HID_RESP_OK;
}

static void cmd_create_profile(const uint8_t *in, uint8_t *out) {
  const hid_packet_profile_name_t *req = (const hid_packet_profile_name_t *)in;
  hid_packet_profile_name_t *resp = (hid_packet_profile_name_t *)out;
  const char *name = NULL;
  int8_t created_slot = -1;

  resp->command_id = CMD_CREATE_PROFILE;
  resp->profile_index = 0xFFu;
  resp->profile_used_mask = settings_get_profile_used_mask();
  memset(resp->profile_name, 0, sizeof(resp->profile_name));

  if (resp->profile_used_mask ==
      (uint8_t)((1u << SETTINGS_PROFILE_COUNT) - 1u)) {
    resp->status = HID_RESP_INVALID_PARAM;
    return;
  }

  created_slot =
      settings_create_profile(req->profile_name, SETTINGS_PROFILE_NAME_LENGTH);
  if (created_slot < 0) {
    resp->status = HID_RESP_INVALID_PARAM;
    return;
  }

  resp->status = HID_RESP_OK;
  resp->profile_index = (uint8_t)created_slot;
  resp->profile_used_mask = settings_get_profile_used_mask();
  name = settings_get_profile_name((uint8_t)created_slot);
  if (name != NULL) {
    for (uint8_t i = 0u; i < SETTINGS_PROFILE_NAME_LENGTH; i++) {
      char c = name[i];
      resp->profile_name[i] = c;
      if (c == '\0') {
        break;
      }
    }
  }
}

static void cmd_delete_profile(const uint8_t *in, uint8_t *out) {
  const hid_packet_profile_index_t *req =
      (const hid_packet_profile_index_t *)in;
  hid_packet_profile_index_t *resp = (hid_packet_profile_index_t *)out;

  resp->command_id = CMD_DELETE_PROFILE;
  resp->profile_index = req->profile_index;
  resp->profile_used_mask = settings_get_profile_used_mask();

  if (req->profile_index >= SETTINGS_PROFILE_COUNT) {
    resp->status = HID_RESP_INVALID_PARAM;
    return;
  }

  if (!settings_is_profile_slot_used(req->profile_index)) {
    resp->status = HID_RESP_INVALID_PARAM;
    return;
  }

  if (!settings_delete_profile(req->profile_index)) {
    resp->status = HID_RESP_ERROR;
    return;
  }

  resp->status = HID_RESP_OK;
  resp->profile_index = settings_get_active_profile_index();
  resp->profile_used_mask = settings_get_profile_used_mask();
}

static void cmd_get_layer_keycode(const uint8_t *in, uint8_t *out) {
  const hid_packet_layer_keycode_t *req = (const hid_packet_layer_keycode_t *)in;
  hid_packet_layer_keycode_t *resp = (hid_packet_layer_keycode_t *)out;

  resp->command_id = CMD_GET_LAYER_KEYCODE;
  resp->layer_index = req->layer_index;
  resp->key_index = req->key_index;

  if (req->layer_index >= SETTINGS_LAYER_COUNT || req->key_index >= NUM_KEYS) {
    resp->status = HID_RESP_INVALID_PARAM;
    resp->hid_keycode = KC_NO;
    return;
  }

  resp->status = HID_RESP_OK;
  resp->hid_keycode =
      settings_get_layer_keycode(req->layer_index, req->key_index);
}

static void cmd_set_layer_keycode(const uint8_t *in, uint8_t *out) {
  const hid_packet_layer_keycode_t *req = (const hid_packet_layer_keycode_t *)in;
  hid_packet_layer_keycode_t *resp = (hid_packet_layer_keycode_t *)out;
  bool success = false;

  resp->command_id = CMD_SET_LAYER_KEYCODE;
  resp->layer_index = req->layer_index;
  resp->key_index = req->key_index;

  if (req->layer_index >= SETTINGS_LAYER_COUNT || req->key_index >= NUM_KEYS) {
    resp->status = HID_RESP_INVALID_PARAM;
    resp->hid_keycode = KC_NO;
    return;
  }

  success = settings_set_layer_keycode(req->layer_index, req->key_index,
                                       req->hid_keycode);
  resp->status = success ? HID_RESP_OK : HID_RESP_ERROR;
  resp->hid_keycode =
      settings_get_layer_keycode(req->layer_index, req->key_index);
}

//--------------------------------------------------------------------+
// Internal Functions - Calibration Commands
//--------------------------------------------------------------------+

static void cmd_get_calibration(const uint8_t *in, uint8_t *out) {
  const hid_packet_calibration_t *req = (const hid_packet_calibration_t *)in;
  hid_packet_calibration_t *resp = (hid_packet_calibration_t *)out;

  resp->command_id = CMD_GET_CALIBRATION;
  resp->start_index = req->start_index;
  resp->value_count = 0;

  if (req->start_index >= NUM_KEYS) {
    resp->status = HID_RESP_INVALID_PARAM;
    return;
  }

  const settings_calibration_t *cal = settings_get_calibration();
  uint8_t count = (uint8_t)(NUM_KEYS - req->start_index);
  if (count > HID_CALIBRATION_VALUES_PER_CHUNK) {
    count = HID_CALIBRATION_VALUES_PER_CHUNK;
  }

  resp->status = HID_RESP_OK;
  resp->value_count = count;
  resp->lut_zero_value = cal->lut_zero_value;
  for (uint8_t i = 0; i < count; i++) {
    resp->key_zero_values[i] = cal->key_zero_values[req->start_index + i];
  }
}

static void cmd_set_calibration(const uint8_t *in, uint8_t *out) {
  const hid_packet_calibration_t *req = (const hid_packet_calibration_t *)in;
  hid_packet_calibration_t *resp = (hid_packet_calibration_t *)out;

  resp->command_id = CMD_SET_CALIBRATION;

  if (req->start_index >= NUM_KEYS || req->value_count == 0 ||
      req->value_count > HID_CALIBRATION_VALUES_PER_CHUNK ||
      (uint16_t)req->start_index + req->value_count > NUM_KEYS) {
    resp->status = HID_RESP_INVALID_PARAM;
    resp->start_index = req->start_index;
    resp->value_count = 0;
    return;
  }

  const settings_calibration_t *current = settings_get_calibration();
  settings_calibration_t cal = *current;
  cal.lut_zero_value = req->lut_zero_value;
  for (uint8_t i = 0; i < req->value_count; i++) {
    cal.key_zero_values[req->start_index + i] = req->key_zero_values[i];
  }

  if (!is_valid_adc_calibration_value(cal.lut_zero_value)) {
    resp->status = HID_RESP_INVALID_PARAM;
    return;
  }

  for (uint8_t i = 0; i < NUM_KEYS; i++) {
    if (!is_valid_adc_calibration_value(cal.key_zero_values[i])) {
      resp->status = HID_RESP_INVALID_PARAM;
      return;
    }
  }

  bool success = settings_set_calibration(&cal);

  // Recalculate offsets with new calibration
  if (success) {
    refresh_runtime_calibration();
  }

  resp->status = success ? HID_RESP_OK : HID_RESP_ERROR;
  resp->start_index = req->start_index;
  resp->value_count = req->value_count;
  resp->lut_zero_value = cal.lut_zero_value;
  for (uint8_t i = 0; i < req->value_count; i++) {
    resp->key_zero_values[i] = cal.key_zero_values[req->start_index + i];
  }
}

static void cmd_get_calibration_max(const uint8_t *in, uint8_t *out) {
  const hid_packet_calibration_max_t *req =
      (const hid_packet_calibration_max_t *)in;
  hid_packet_calibration_max_t *resp = (hid_packet_calibration_max_t *)out;

  resp->command_id = CMD_GET_CALIBRATION_MAX;
  resp->start_index = req->start_index;
  resp->value_count = 0;

  if (req->start_index >= NUM_KEYS) {
    resp->status = HID_RESP_INVALID_PARAM;
    return;
  }

  const settings_calibration_t *cal = settings_get_calibration();
  uint8_t count = (uint8_t)(NUM_KEYS - req->start_index);
  if (count > HID_CALIBRATION_VALUES_PER_CHUNK) {
    count = HID_CALIBRATION_VALUES_PER_CHUNK;
  }

  resp->status = HID_RESP_OK;
  resp->value_count = count;
  resp->lut_zero_value = cal->lut_zero_value;
  for (uint8_t i = 0; i < count; i++) {
    resp->key_max_values[i] = cal->key_max_values[req->start_index + i];
  }
}

static void cmd_set_calibration_max(const uint8_t *in, uint8_t *out) {
  const hid_packet_calibration_max_t *req =
      (const hid_packet_calibration_max_t *)in;
  hid_packet_calibration_max_t *resp = (hid_packet_calibration_max_t *)out;

  resp->command_id = CMD_SET_CALIBRATION_MAX;

  if (req->start_index >= NUM_KEYS || req->value_count == 0 ||
      req->value_count > HID_CALIBRATION_VALUES_PER_CHUNK ||
      (uint16_t)req->start_index + req->value_count > NUM_KEYS) {
    resp->status = HID_RESP_INVALID_PARAM;
    resp->start_index = req->start_index;
    resp->value_count = 0;
    return;
  }

  const settings_calibration_t *current = settings_get_calibration();
  settings_calibration_t cal = *current;
  for (uint8_t i = 0; i < req->value_count; i++) {
    cal.key_max_values[req->start_index + i] = req->key_max_values[i];
  }

  for (uint8_t i = 0; i < NUM_KEYS; i++) {
    if (!is_valid_adc_calibration_value(cal.key_max_values[i])) {
      resp->status = HID_RESP_INVALID_PARAM;
      return;
    }
  }

  bool success = settings_set_calibration(&cal);
  if (success) {
    refresh_runtime_calibration();
  }

  resp->status = success ? HID_RESP_OK : HID_RESP_ERROR;
  resp->start_index = req->start_index;
  resp->value_count = req->value_count;
  resp->lut_zero_value = cal.lut_zero_value;
  for (uint8_t i = 0; i < req->value_count; i++) {
    resp->key_max_values[i] = cal.key_max_values[req->start_index + i];
  }
}

static void cmd_auto_calibrate(const uint8_t *in, uint8_t *out) {
  const hid_packet_auto_calibrate_t *req =
      (const hid_packet_auto_calibrate_t *)in;
  hid_packet_calibration_t *resp = (hid_packet_calibration_t *)out;

  resp->command_id = CMD_AUTO_CALIBRATE;

  uint8_t key_index = req->key_index;

  bool success = true;

  if (key_index == 0xFF) {
    // Auto-calibrate all keys
    for (int i = 0; i < NUM_KEYS; i++) {
      if (!settings_set_key_calibration(i, (int16_t)analog_read_raw_value(i))) {
        success = false;
      }
    }
  } else if (key_index < NUM_KEYS) {
    // Auto-calibrate single key
    success = settings_set_key_calibration(
        key_index, (int16_t)analog_read_raw_value(key_index));
  } else {
    resp->status = HID_RESP_INVALID_PARAM;
    return;
  }

  resp->status = success ? HID_RESP_OK : HID_RESP_ERROR;
  if (!success) {
    return;
  }

  // Recalculate offsets
  refresh_runtime_calibration();

  // Return updated calibration
  const settings_calibration_t *cal = settings_get_calibration();
  resp->start_index = 0;
  resp->value_count =
      (NUM_KEYS < HID_CALIBRATION_VALUES_PER_CHUNK) ? NUM_KEYS
                                                    : HID_CALIBRATION_VALUES_PER_CHUNK;
  resp->lut_zero_value = cal->lut_zero_value;
  for (uint8_t i = 0; i < resp->value_count; i++) {
    resp->key_zero_values[i] = cal->key_zero_values[i];
  }
}

static void cmd_guided_calibration_start(const uint8_t *in, uint8_t *out) {
  hid_packet_guided_calibration_status_t *resp =
      (hid_packet_guided_calibration_status_t *)out;
  calibration_guided_status_t status = {0};
  (void)in;

  resp->command_id = CMD_GUIDED_CALIBRATION_START;
  resp->status = calibration_guided_start() ? HID_RESP_OK : HID_RESP_ERROR;
  calibration_guided_get_status(&status);
  resp->active = status.active;
  resp->phase = status.phase;
  resp->current_key = status.current_key;
  resp->progress_percent = status.progress_percent;
  resp->sample_count = status.sample_count;
  resp->phase_elapsed_ms = status.phase_elapsed_ms;
  memset(resp->reserved, 0, sizeof(resp->reserved));
}

static void cmd_guided_calibration_status(const uint8_t *in, uint8_t *out) {
  hid_packet_guided_calibration_status_t *resp =
      (hid_packet_guided_calibration_status_t *)out;
  calibration_guided_status_t status = {0};
  (void)in;

  resp->command_id = CMD_GUIDED_CALIBRATION_STATUS;
  resp->status = HID_RESP_OK;
  calibration_guided_get_status(&status);
  resp->active = status.active;
  resp->phase = status.phase;
  resp->current_key = status.current_key;
  resp->progress_percent = status.progress_percent;
  resp->sample_count = status.sample_count;
  resp->phase_elapsed_ms = status.phase_elapsed_ms;
  memset(resp->reserved, 0, sizeof(resp->reserved));
}

static void cmd_guided_calibration_abort(const uint8_t *in, uint8_t *out) {
  hid_packet_guided_calibration_status_t *resp =
      (hid_packet_guided_calibration_status_t *)out;
  calibration_guided_status_t status = {0};
  (void)in;

  calibration_guided_abort();
  resp->command_id = CMD_GUIDED_CALIBRATION_ABORT;
  resp->status = HID_RESP_OK;
  calibration_guided_get_status(&status);
  resp->active = status.active;
  resp->phase = status.phase;
  resp->current_key = status.current_key;
  resp->progress_percent = status.progress_percent;
  resp->sample_count = status.sample_count;
  resp->phase_elapsed_ms = status.phase_elapsed_ms;
  memset(resp->reserved, 0, sizeof(resp->reserved));
}

//--------------------------------------------------------------------+
// Internal Functions - Per-Key Curve Commands
//--------------------------------------------------------------------+

static void cmd_get_key_curve(const uint8_t *in, uint8_t *out) {
  const hid_packet_key_curve_t *req = (const hid_packet_key_curve_t *)in;
  hid_packet_key_curve_t *resp = (hid_packet_key_curve_t *)out;

  resp->command_id = CMD_GET_KEY_CURVE;

  uint8_t key_index = req->key_index;
  if (key_index >= NUM_KEYS) {
    resp->status = HID_RESP_INVALID_PARAM;
    return;
  }

  const settings_curve_t *curve = settings_get_key_curve(key_index);

  resp->status = HID_RESP_OK;
  resp->key_index = key_index;
  resp->curve_enabled = settings_is_key_curve_enabled(key_index) ? 1 : 0;
  resp->p1_x = curve->p1.x;
  resp->p1_y = curve->p1.y;
  resp->p2_x = curve->p2.x;
  resp->p2_y = curve->p2.y;
}

static void cmd_set_key_curve(const uint8_t *in, uint8_t *out) {
  const hid_packet_key_curve_t *req = (const hid_packet_key_curve_t *)in;
  hid_packet_key_curve_t *resp = (hid_packet_key_curve_t *)out;

  resp->command_id = CMD_SET_KEY_CURVE;

  uint8_t key_index = req->key_index;
  if (key_index >= NUM_KEYS) {
    resp->status = HID_RESP_INVALID_PARAM;
    return;
  }

  settings_curve_t curve;
  curve.p1.x = req->p1_x;
  curve.p1.y = req->p1_y;
  curve.p2.x = req->p2_x;
  curve.p2.y = req->p2_y;

  settings_set_key_curve(key_index, &curve);
  settings_set_key_curve_enabled(key_index, req->curve_enabled != 0);

  resp->status = HID_RESP_OK;
  resp->key_index = key_index;
  resp->curve_enabled = req->curve_enabled;
  resp->p1_x = req->p1_x;
  resp->p1_y = req->p1_y;
  resp->p2_x = req->p2_x;
  resp->p2_y = req->p2_y;
}

//--------------------------------------------------------------------+
// Internal Functions - Per-Key Gamepad Mapping Commands
//--------------------------------------------------------------------+

static void cmd_get_key_gamepad_map(const uint8_t *in, uint8_t *out) {
  const hid_packet_key_gamepad_map_t *req =
      (const hid_packet_key_gamepad_map_t *)in;
  hid_packet_key_gamepad_map_t *resp = (hid_packet_key_gamepad_map_t *)out;
  const settings_gamepad_mapping_t *mapping = NULL;

  resp->command_id = CMD_GET_KEY_GAMEPAD_MAP;

  uint8_t key_index = req->key_index;
  if (key_index >= NUM_KEYS) {
    resp->status = HID_RESP_INVALID_PARAM;
    return;
  }

  mapping = settings_get_key_gamepad_mapping(key_index);

  resp->status = HID_RESP_OK;
  resp->key_index = key_index;
  resp->axis = mapping->axis;
  resp->direction = mapping->direction;
  resp->button = mapping->button;
  resp->layer_mask = settings_gamepad_mapping_get_layer_mask(mapping);
}

static void cmd_set_key_gamepad_map(const uint8_t *in, uint8_t *out) {
  const hid_packet_key_gamepad_map_t *req =
      (const hid_packet_key_gamepad_map_t *)in;
  hid_packet_key_gamepad_map_t *resp = (hid_packet_key_gamepad_map_t *)out;
  const settings_gamepad_mapping_t *applied = NULL;

  resp->command_id = CMD_SET_KEY_GAMEPAD_MAP;

  uint8_t key_index = req->key_index;
  if (key_index >= NUM_KEYS) {
    resp->status = HID_RESP_INVALID_PARAM;
    return;
  }

  settings_gamepad_mapping_t mapping;
  mapping.axis = req->axis;
  mapping.direction = req->direction;
  mapping.button = req->button;
  settings_gamepad_mapping_set_layer_mask(&mapping, req->layer_mask);

  if (!settings_set_key_gamepad_mapping(key_index, &mapping)) {
    resp->status = HID_RESP_ERROR;
    return;
  }

  applied = settings_get_key_gamepad_mapping(key_index);

  resp->status = HID_RESP_OK;
  resp->key_index = key_index;
  resp->axis = applied->axis;
  resp->direction = applied->direction;
  resp->button = applied->button;
  resp->layer_mask = settings_gamepad_mapping_get_layer_mask(applied);
}

//--------------------------------------------------------------------+
// Internal Functions - LED Matrix Commands
//--------------------------------------------------------------------+

static void cmd_get_led_enabled(const uint8_t *in, uint8_t *out) {
  hid_packet_bool_t *resp = (hid_packet_bool_t *)out;
  resp->command_id = CMD_GET_LED_ENABLED;
  resp->status = HID_RESP_OK;
  resp->value = settings_is_led_enabled() ? 1 : 0;
}

static void cmd_set_led_enabled(const uint8_t *in, uint8_t *out) {
  const hid_packet_bool_t *req = (const hid_packet_bool_t *)in;
  hid_packet_bool_t *resp = (hid_packet_bool_t *)out;

  settings_set_led_enabled(req->value != 0);

  resp->command_id = CMD_SET_LED_ENABLED;
  resp->status = HID_RESP_OK;
  resp->value = settings_is_led_enabled() ? 1 : 0;
}

static void cmd_get_led_brightness(const uint8_t *in, uint8_t *out) {
  hid_packet_led_brightness_t *resp = (hid_packet_led_brightness_t *)out;
  resp->command_id = CMD_GET_LED_BRIGHTNESS;
  resp->status = HID_RESP_OK;
  resp->brightness = settings_get_led_brightness();
}

static void cmd_set_led_brightness(const uint8_t *in, uint8_t *out) {
  const hid_packet_led_brightness_t *req =
      (const hid_packet_led_brightness_t *)in;
  hid_packet_led_brightness_t *resp = (hid_packet_led_brightness_t *)out;

  settings_set_led_brightness(req->brightness);

  resp->command_id = CMD_SET_LED_BRIGHTNESS;
  resp->status = HID_RESP_OK;
  resp->brightness = settings_get_led_brightness();
}

static void cmd_get_led_pixel(const uint8_t *in, uint8_t *out) {
  const hid_packet_led_pixel_t *req = (const hid_packet_led_pixel_t *)in;
  hid_packet_led_pixel_t *resp = (hid_packet_led_pixel_t *)out;

  resp->command_id = CMD_GET_LED_PIXEL;

  if (req->index >= LED_MATRIX_SIZE) {
    resp->status = HID_RESP_INVALID_PARAM;
    return;
  }

  const uint8_t *pixels = led_matrix_get_raw_data();
  resp->status = HID_RESP_OK;
  resp->index = req->index;
  resp->r = pixels[req->index * 3 + 0];
  resp->g = pixels[req->index * 3 + 1];
  resp->b = pixels[req->index * 3 + 2];
}

static void cmd_set_led_pixel(const uint8_t *in, uint8_t *out) {
  const hid_packet_led_pixel_t *req = (const hid_packet_led_pixel_t *)in;
  hid_packet_led_pixel_t *resp = (hid_packet_led_pixel_t *)out;
  bool third_party_mode =
      settings_get_led_effect_mode() == (uint8_t)LED_EFFECT_THIRD_PARTY;

  resp->command_id = CMD_SET_LED_PIXEL;

  if (req->index >= LED_MATRIX_SIZE) {
    resp->status = HID_RESP_INVALID_PARAM;
    return;
  }

  if (third_party_mode) {
    led_matrix_set_pixel_idx(req->index, req->r, req->g, req->b);
  } else {
  settings_set_led_pixel(req->index, req->r, req->g, req->b);
  }

  resp->status = HID_RESP_OK;
  resp->index = req->index;
  resp->r = req->r;
  resp->g = req->g;
  resp->b = req->b;
}

static void cmd_get_led_row(const uint8_t *in, uint8_t *out) {
  const hid_packet_led_row_t *req = (const hid_packet_led_row_t *)in;
  hid_packet_led_row_t *resp = (hid_packet_led_row_t *)out;

  resp->command_id = CMD_GET_LED_ROW;

  if (req->row >= LED_MATRIX_HEIGHT) {
    resp->status = HID_RESP_INVALID_PARAM;
    return;
  }

  const uint8_t *pixels = led_matrix_get_raw_data();
  uint16_t offset = req->row * LED_MATRIX_WIDTH * 3; // Row offset in bytes

  resp->status = HID_RESP_OK;
  resp->row = req->row;
  memcpy(resp->pixels, &pixels[offset], 24); // 8 LEDs * 3 bytes
}

static void cmd_set_led_row(const uint8_t *in, uint8_t *out) {
  const hid_packet_led_row_t *req = (const hid_packet_led_row_t *)in;
  hid_packet_led_row_t *resp = (hid_packet_led_row_t *)out;
  bool third_party_mode =
      settings_get_led_effect_mode() == (uint8_t)LED_EFFECT_THIRD_PARTY;

  resp->command_id = CMD_SET_LED_ROW;

  if (req->row >= LED_MATRIX_HEIGHT) {
    resp->status = HID_RESP_INVALID_PARAM;
    return;
  }

  if (third_party_mode) {
    // Third-party streaming must stay runtime-only and avoid dirty autosaves.
    led_matrix_begin_pixel_batch();
  for (int x = 0; x < LED_MATRIX_WIDTH; x++) {
    uint8_t idx = req->row * LED_MATRIX_WIDTH + x;
      led_matrix_set_pixel_idx(idx, req->pixels[x * 3], req->pixels[x * 3 + 1],
                           req->pixels[x * 3 + 2]);
  }
    led_matrix_end_pixel_batch();
  } else {
    uint8_t pixels[LED_MATRIX_DATA_BYTES];
    uint16_t offset = req->row * LED_MATRIX_WIDTH * 3u;

    memcpy(pixels, settings_get_led_pixels(), LED_MATRIX_DATA_BYTES);
    memcpy(&pixels[offset], req->pixels, 24u);

    if (!settings_set_led_pixels(pixels)) {
      resp->status = HID_RESP_ERROR;
      return;
    }
  }

  resp->status = HID_RESP_OK;
  resp->row = req->row;
  memcpy(resp->pixels, req->pixels, 24);
}

// LED bulk sync chunk size: max payload that still fits in one HID packet.
#define LED_CHUNK_SIZE HID_LED_BYTES_PER_CHUNK

static void cmd_get_led_all(const uint8_t *in, uint8_t *out) {
  const hid_packet_led_chunk_t *req = (const hid_packet_led_chunk_t *)in;
  hid_packet_led_chunk_t *resp = (hid_packet_led_chunk_t *)out;

  resp->command_id = CMD_GET_LED_ALL;
  resp->chunk_index = req->chunk_index;
  resp->chunk_size = 0;

  uint16_t offset = (uint16_t)req->chunk_index * LED_CHUNK_SIZE;
  if (offset >= LED_MATRIX_DATA_BYTES) {
    resp->status = HID_RESP_INVALID_PARAM;
    return;
  }

  uint16_t remaining = LED_MATRIX_DATA_BYTES - offset;
  uint8_t size = (remaining > LED_CHUNK_SIZE) ? LED_CHUNK_SIZE : (uint8_t)remaining;

  const uint8_t *pixels = led_matrix_get_raw_data();
  resp->status = HID_RESP_OK;
  resp->chunk_size = size;
  memcpy(resp->data, &pixels[offset], size);
}

static void cmd_set_led_all_chunk(const uint8_t *in, uint8_t *out) {
  const hid_packet_led_chunk_t *req = (const hid_packet_led_chunk_t *)in;
  hid_packet_led_chunk_t *resp = (hid_packet_led_chunk_t *)out;
  bool third_party_mode =
      settings_get_led_effect_mode() == (uint8_t)LED_EFFECT_THIRD_PARTY;

  resp->command_id = CMD_SET_LED_ALL_CHUNK;
  resp->chunk_index = req->chunk_index;
  resp->chunk_size = 0;

  if (req->chunk_size == 0 || req->chunk_size > LED_CHUNK_SIZE) {
    resp->status = HID_RESP_INVALID_PARAM;
    return;
  }

  uint16_t offset = req->chunk_index * LED_CHUNK_SIZE;
  if (offset >= LED_MATRIX_DATA_BYTES ||
      (uint32_t)offset + req->chunk_size > LED_MATRIX_DATA_BYTES) {
    resp->status = HID_RESP_INVALID_PARAM;
    return;
  }

  if (third_party_mode) {
    // Third-party live chunks should not mutate persisted matrix storage.
    led_matrix_begin_pixel_batch();
  for (uint16_t i = 0; i + 2 < req->chunk_size; i += 3) {
      uint8_t idx = (offset + i) / 3u;
    if (idx < LED_MATRIX_SIZE) {
        led_matrix_set_pixel_idx(idx, req->data[i], req->data[i + 1],
                             req->data[i + 2]);
    }
  }
    led_matrix_end_pixel_batch();
  } else {
    uint8_t pixels[LED_MATRIX_DATA_BYTES];

    memcpy(pixels, settings_get_led_pixels(), LED_MATRIX_DATA_BYTES);
    memcpy(&pixels[offset], req->data, req->chunk_size);

    if (!settings_set_led_pixels(pixels)) {
      resp->status = HID_RESP_ERROR;
      return;
    }
  }

  resp->status = HID_RESP_OK;
  resp->chunk_size = req->chunk_size;
}

static void cmd_led_clear(const uint8_t *in, uint8_t *out) {
  hid_packet_t *resp = (hid_packet_t *)out;
  bool third_party_mode =
      settings_get_led_effect_mode() == (uint8_t)LED_EFFECT_THIRD_PARTY;
  (void)in;
  resp->command_id = CMD_LED_CLEAR;

  if (third_party_mode) {
    led_matrix_clear();
  } else {
    uint8_t pixels[LED_MATRIX_DATA_BYTES] = {0};
    if (!settings_set_led_pixels(pixels)) {
      resp->status_or_len = HID_RESP_ERROR;
      return;
    }
  }

  resp->status_or_len = HID_RESP_OK;
}

static void cmd_led_fill(const uint8_t *in, uint8_t *out) {
  const hid_packet_led_fill_t *req = (const hid_packet_led_fill_t *)in;
  hid_packet_led_fill_t *resp = (hid_packet_led_fill_t *)out;
  bool third_party_mode =
      settings_get_led_effect_mode() == (uint8_t)LED_EFFECT_THIRD_PARTY;

  resp->command_id = CMD_LED_FILL;

  if (third_party_mode) {
    led_matrix_begin_pixel_batch();
    led_matrix_fill(req->r, req->g, req->b);
    led_matrix_end_pixel_batch();
  } else {
    uint8_t pixels[LED_MATRIX_DATA_BYTES];

    for (uint8_t i = 0u; i < LED_MATRIX_SIZE; i++) {
      pixels[i * 3u + 0u] = req->r;
      pixels[i * 3u + 1u] = req->g;
      pixels[i * 3u + 2u] = req->b;
    }

    if (!settings_set_led_pixels(pixels)) {
      resp->status = HID_RESP_ERROR;
      return;
    }
  }

  resp->status = HID_RESP_OK;
  resp->r = req->r;
  resp->g = req->g;
  resp->b = req->b;
}

static void cmd_led_test_rainbow(const uint8_t *in, uint8_t *out) {
  hid_packet_t *resp = (hid_packet_t *)out;
  resp->command_id = CMD_LED_TEST_RAINBOW;

  // Show rainbow test pattern
  led_matrix_test_rainbow(0);

  resp->status_or_len = HID_RESP_OK;
}

static void cmd_get_led_effect(const uint8_t *in, uint8_t *out) {
  hid_packet_t *resp = (hid_packet_t *)out;
  resp->command_id = CMD_GET_LED_EFFECT;
  resp->status_or_len = HID_RESP_OK;
  resp->payload[0] = settings_get_led_effect_mode();
}

static void cmd_set_led_effect(const uint8_t *in, uint8_t *out) {
  hid_packet_t *req = (hid_packet_t *)in;
  hid_packet_t *resp = (hid_packet_t *)out;
  resp->command_id = CMD_SET_LED_EFFECT;

  uint8_t mode = req->payload[0];
  if (mode < LED_EFFECT_MAX) {
    settings_set_led_effect_mode(mode);
    resp->status_or_len = HID_RESP_OK;
  } else {
    resp->status_or_len = HID_RESP_INVALID_PARAM;
  }
}

static void cmd_get_led_effect_params(const uint8_t *in, uint8_t *out) {
  const hid_packet_t *req = (const hid_packet_t *)in;
  hid_packet_t *resp = (hid_packet_t *)out;
  uint8_t effect_mode = req->payload[0];

  resp->command_id = CMD_GET_LED_EFFECT_PARAMS;
  if (effect_mode >= LED_EFFECT_MAX) {
    resp->status_or_len = HID_RESP_INVALID_PARAM;
    return;
  }

  resp->status_or_len = HID_RESP_OK;
  resp->payload[0] = effect_mode;
  resp->payload[1] = LED_EFFECT_PARAM_COUNT;
  settings_get_led_effect_params(effect_mode, &resp->payload[2]);
}

static void cmd_set_led_effect_params(const uint8_t *in, uint8_t *out) {
  const hid_packet_t *req = (const hid_packet_t *)in;
  hid_packet_t *resp = (hid_packet_t *)out;
  uint8_t effect_mode = req->payload[0];

  resp->command_id = CMD_SET_LED_EFFECT_PARAMS;
  if (effect_mode >= LED_EFFECT_MAX) {
    resp->status_or_len = HID_RESP_INVALID_PARAM;
    return;
  }

  if (!settings_set_led_effect_params(effect_mode, &req->payload[1])) {
    resp->status_or_len = HID_RESP_ERROR;
    return;
  }

  resp->status_or_len = HID_RESP_OK;
  resp->payload[0] = effect_mode;
  resp->payload[1] = LED_EFFECT_PARAM_COUNT;
  settings_get_led_effect_params(effect_mode, &resp->payload[2]);
}

/**
 * CMD_GET_LED_EFFECT_SCHEMA (0x77)
 *
 * Request:
 *   payload[0] = effect_id
 *   payload[1] = chunk_index  (0-based)
 *
 * Response:
 *   payload[0]   = effect_id
 *   payload[1]   = chunk_index
 *   payload[2]   = total_chunks
 *   payload[3]   = total_active_params (type != NONE across all chunks)
 *   payload[4..] = param descriptors for this chunk
 *                  each entry: [id, type, min, max, default_val, step] (6 bytes)
 *                  up to LED_SCHEMA_PARAMS_PER_CHUNK entries per chunk.
 */
static void cmd_get_led_effect_schema(const uint8_t *in, uint8_t *out) {
  const hid_packet_t *req  = (const hid_packet_t *)in;
  hid_packet_t       *resp = (hid_packet_t *)out;

  uint8_t effect_id   = req->payload[0];
  uint8_t chunk_index = req->payload[1];

  resp->command_id = CMD_GET_LED_EFFECT_SCHEMA;

  if (effect_id >= (uint8_t)LED_EFFECT_MAX) {
    resp->status_or_len = HID_RESP_INVALID_PARAM;
    return;
  }

  led_param_desc_t all_descs[LED_EFFECT_PARAM_COUNT];
  uint8_t total_active =
      led_matrix_get_effect_schema((led_effect_mode_t)effect_id, all_descs);

  // Collect only the active (non-NONE) descriptors in order.
  led_param_desc_t active[LED_EFFECT_PARAM_COUNT];
  uint8_t active_count = 0u;
  for (uint8_t i = 0u; i < LED_EFFECT_PARAM_COUNT; i++) {
    if (all_descs[i].type != LED_PARAM_TYPE_NONE) {
      active[active_count++] = all_descs[i];
    }
  }

  uint8_t total_chunks = (active_count == 0u)
      ? 1u
      : (uint8_t)((active_count + LED_SCHEMA_PARAMS_PER_CHUNK - 1u) /
                  LED_SCHEMA_PARAMS_PER_CHUNK);

  if (chunk_index >= total_chunks) {
    resp->status_or_len = HID_RESP_INVALID_PARAM;
    return;
  }

  uint8_t chunk_start = chunk_index * LED_SCHEMA_PARAMS_PER_CHUNK;
  uint8_t chunk_count = active_count - chunk_start;
  if (chunk_count > LED_SCHEMA_PARAMS_PER_CHUNK) {
    chunk_count = LED_SCHEMA_PARAMS_PER_CHUNK;
  }

  resp->status_or_len = HID_RESP_OK;
  resp->payload[0] = effect_id;
  resp->payload[1] = chunk_index;
  resp->payload[2] = total_chunks;
  resp->payload[3] = total_active;

  // Serialise chunk entries starting at payload[4].
  // Each entry: [id, type, min, max, default_val, step] = 6 bytes.
  uint8_t *dst = &resp->payload[4];
  for (uint8_t i = 0u; i < chunk_count; i++) {
    const led_param_desc_t *d = &active[chunk_start + i];
    *dst++ = d->id;
    *dst++ = d->type;
    *dst++ = d->min;
    *dst++ = d->max;
    *dst++ = d->default_val;
    *dst++ = d->step;
  }
}


static void cmd_get_led_fps_limit(const uint8_t *in, uint8_t *out) {
  hid_packet_t *resp = (hid_packet_t *)out;
  resp->command_id = CMD_GET_LED_FPS_LIMIT;
  resp->status_or_len = HID_RESP_OK;
  resp->payload[0] = settings_get_led_fps_limit();
}

static void cmd_set_led_fps_limit(const uint8_t *in, uint8_t *out) {
  hid_packet_t *req = (hid_packet_t *)in;
  hid_packet_t *resp = (hid_packet_t *)out;
  resp->command_id = CMD_SET_LED_FPS_LIMIT;

  settings_set_led_fps_limit(req->payload[0]);
  resp->status_or_len = HID_RESP_OK;
  resp->payload[0] = settings_get_led_fps_limit();
}

static void cmd_set_led_volume_overlay(const uint8_t *in, uint8_t *out) {
  const hid_packet_t *req = (const hid_packet_t *)in;
  hid_packet_t *resp = (hid_packet_t *)out;

  resp->command_id = CMD_SET_LED_VOLUME_OVERLAY;
  led_matrix_set_host_volume_level(req->payload[0]);
  resp->status_or_len = HID_RESP_OK;
  resp->payload[0] = req->payload[0];
}

static void cmd_clear_led_volume_overlay(const uint8_t *in, uint8_t *out) {
  hid_packet_t *resp = (hid_packet_t *)out;
  (void)in;

  resp->command_id = CMD_CLEAR_LED_VOLUME_OVERLAY;
  led_matrix_clear_host_volume_level();
  led_matrix_clear_volume_overlay();
  resp->status_or_len = HID_RESP_OK;
}

static void cmd_restore_led_effect_before_third_party(const uint8_t *in,
                                                      uint8_t *out) {
  hid_packet_t *resp = (hid_packet_t *)out;
  (void)in;

  resp->command_id = CMD_RESTORE_LED_EFFECT_BEFORE_THIRD_PARTY;
  resp->status_or_len = settings_restore_led_effect_before_third_party()
                            ? HID_RESP_OK
                            : HID_RESP_ERROR;
  resp->payload[0] = settings_get_led_effect_mode();
}

static void cmd_set_led_audio_spectrum(const uint8_t *in, uint8_t *out) {
  const hid_packet_t *req = (const hid_packet_t *)in;
  hid_packet_t *resp = (hid_packet_t *)out;
  uint8_t band_count = req->payload[0];
  uint8_t impact_level = 0u;
  uint8_t bands[LED_AUDIO_SPECTRUM_BAND_COUNT] = {0};

  resp->command_id = CMD_SET_LED_AUDIO_SPECTRUM;
  if (band_count == 0u || band_count > LED_AUDIO_SPECTRUM_BAND_COUNT) {
    resp->status_or_len = HID_RESP_INVALID_PARAM;
    return;
  }

  memcpy(bands, &req->payload[1], band_count);
  impact_level = req->payload[1u + band_count];
  led_matrix_set_audio_spectrum(bands, band_count, impact_level);

  resp->status_or_len = HID_RESP_OK;
  resp->payload[0] = band_count;
  memcpy(&resp->payload[1], bands, band_count);
  resp->payload[1u + band_count] = impact_level;
}

static void cmd_clear_led_audio_spectrum(const uint8_t *in, uint8_t *out) {
  hid_packet_t *resp = (hid_packet_t *)out;
  (void)in;

  resp->command_id = CMD_CLEAR_LED_AUDIO_SPECTRUM;
  led_matrix_clear_audio_spectrum();
  resp->status_or_len = HID_RESP_OK;
}

static void cmd_set_led_alpha_mask(const uint8_t *in, uint8_t *out) {
  const hid_packet_t *req = (const hid_packet_t *)in;
  hid_packet_t *resp = (hid_packet_t *)out;
  uint8_t mask_len = req->payload[0];
  uint8_t mask_offset = 1u;

  resp->command_id = CMD_SET_LED_ALPHA_MASK;

  // Compatibility path for clients that keep a reserved byte at payload[0].
  if (mask_len == 0u && req->payload[1] != 0u) {
    mask_len = req->payload[1];
    mask_offset = 2u;
  }

  if (mask_len > HID_ALPHA_MASK_MAX_BYTES) {
    resp->status_or_len = HID_RESP_INVALID_PARAM;
    return;
  }

  if (mask_len > (uint8_t)(sizeof(req->payload) - mask_offset)) {
    resp->status_or_len = HID_RESP_INVALID_PARAM;
    return;
  }

  led_matrix_set_alpha_mask(mask_len == 0u ? NULL : &req->payload[mask_offset],
                            mask_len);

  resp->status_or_len = HID_RESP_OK;
  resp->payload[0] = mask_len;
}

static void cmd_get_led_idle_options(const uint8_t *in, uint8_t *out) {
  hid_packet_led_idle_options_t *resp = (hid_packet_led_idle_options_t *)out;
  (void)in;

  resp->command_id = CMD_GET_LED_IDLE_OPTIONS;
  resp->status = HID_RESP_OK;
  resp->idle_timeout_seconds = settings_get_led_idle_timeout_seconds();
  resp->allow_system_when_disabled =
      settings_is_led_system_indicators_allowed_when_disabled() ? 1u : 0u;
  resp->third_party_stream_counts_as_activity =
      settings_is_led_idle_third_party_stream_counts_as_activity() ? 1u : 0u;
}

static void cmd_set_led_idle_options(const uint8_t *in, uint8_t *out) {
  const hid_packet_led_idle_options_t *req =
      (const hid_packet_led_idle_options_t *)in;
  hid_packet_led_idle_options_t *resp = (hid_packet_led_idle_options_t *)out;
  bool ok_timeout = false;
  bool ok_policy = false;
  bool ok_third_party_activity = false;

  ok_timeout =
      settings_set_led_idle_timeout_seconds(req->idle_timeout_seconds);
  ok_policy = settings_set_led_system_indicators_allowed_when_disabled(
      req->allow_system_when_disabled != 0u);
  ok_third_party_activity =
      settings_set_led_idle_third_party_stream_counts_as_activity(
          req->third_party_stream_counts_as_activity != 0u);

  resp->command_id = CMD_SET_LED_IDLE_OPTIONS;
  resp->status = (ok_timeout && ok_policy && ok_third_party_activity)
                     ? HID_RESP_OK
                     : HID_RESP_INVALID_PARAM;
  resp->idle_timeout_seconds = settings_get_led_idle_timeout_seconds();
  resp->allow_system_when_disabled =
      settings_is_led_system_indicators_allowed_when_disabled() ? 1u : 0u;
  resp->third_party_stream_counts_as_activity =
      settings_is_led_idle_third_party_stream_counts_as_activity() ? 1u : 0u;
}

static void cmd_get_led_usb_suspend_rgb_off(const uint8_t *in, uint8_t *out) {
  hid_packet_bool_t *resp = (hid_packet_bool_t *)out;
  (void)in;

  resp->command_id = CMD_GET_LED_USB_SUSPEND_RGB_OFF;
  resp->status = HID_RESP_OK;
  resp->value = settings_is_led_usb_suspend_rgb_off_enabled() ? 1u : 0u;
}

static void cmd_set_led_usb_suspend_rgb_off(const uint8_t *in, uint8_t *out) {
  const hid_packet_bool_t *req = (const hid_packet_bool_t *)in;
  hid_packet_bool_t *resp = (hid_packet_bool_t *)out;
  bool success = settings_set_led_usb_suspend_rgb_off_enabled(req->value != 0u);

  resp->command_id = CMD_SET_LED_USB_SUSPEND_RGB_OFF;
  resp->status = success ? HID_RESP_OK : HID_RESP_ERROR;
  resp->value = settings_is_led_usb_suspend_rgb_off_enabled() ? 1u : 0u;
}

//--------------------------------------------------------------------+
// Internal Functions - Filter Commands
//--------------------------------------------------------------------+

static void cmd_get_filter_enabled(const uint8_t *in, uint8_t *out) {
  hid_packet_t *resp = (hid_packet_t *)out;
  (void)in;
  resp->command_id = CMD_GET_FILTER_ENABLED;
  resp->status_or_len = HID_RESP_OK;
  resp->payload[0] = settings_is_filter_enabled() ? 1u : 0u;
}

static void cmd_set_filter_enabled(const uint8_t *in, uint8_t *out) {
  const hid_packet_t *req = (const hid_packet_t *)in;
  hid_packet_t *resp = (hid_packet_t *)out;
  resp->command_id = CMD_SET_FILTER_ENABLED;

  bool success = settings_set_filter_enabled(req->payload[0] != 0);
  resp->status_or_len = success ? HID_RESP_OK : HID_RESP_ERROR;
  resp->payload[0] = settings_is_filter_enabled() ? 1u : 0u;
}

static void cmd_get_filter_params(const uint8_t *in, uint8_t *out) {
  hid_packet_t *resp = (hid_packet_t *)out;
  (void)in;
  resp->command_id = CMD_GET_FILTER_PARAMS;
  resp->status_or_len = HID_RESP_OK;

  settings_get_filter_params(&resp->payload[0], &resp->payload[1],
                             &resp->payload[2]);
}

static void cmd_set_filter_params(const uint8_t *in, uint8_t *out) {
  const hid_packet_t *req = (const hid_packet_t *)in;
  hid_packet_t *resp = (hid_packet_t *)out;
  resp->command_id = CMD_SET_FILTER_PARAMS;

  bool success =
      settings_set_filter_params(req->payload[0], req->payload[1], req->payload[2]);
  resp->status_or_len = success ? HID_RESP_OK : HID_RESP_ERROR;
  settings_get_filter_params(&resp->payload[0], &resp->payload[1],
                             &resp->payload[2]);
}

//--------------------------------------------------------------------+
// Internal Functions - Debug Commands
//--------------------------------------------------------------------+

static void cmd_get_adc_values(const uint8_t *in, uint8_t *out) {
  hid_resp_adc_values_t *resp = (hid_resp_adc_values_t *)out;
  (void)in;
  diagnostics_live_ping();
  resp->command_id = CMD_GET_ADC_VALUES;
  resp->status = HID_RESP_OK;

  for (int i = 0; i < 6; i++) {
    resp->adc_raw[i] = analog_read_raw_value(i);
    resp->adc_filtered[i] = analog_read_filtered_value((uint8_t)i);
  }

  // Include timing information from main loop
  resp->scan_time_us = (uint16_t)adc_full_cycle_us;

  // Calculate scan rate in Hz (1,000,000 / scan_time_us)
  if (adc_full_cycle_us > 0) {
    resp->scan_rate_hz = (uint16_t)(1000000 / adc_full_cycle_us);
  } else {
    resp->scan_rate_hz = 0;
  }

  resp->task_analog_us = (uint16_t)task_analog_us;
  resp->task_trigger_us = (uint16_t)task_trigger_us;
  resp->task_socd_us = (uint16_t)task_socd_us;
  resp->task_keyboard_us = (uint16_t)task_keyboard_us;
  resp->task_keyboard_nkro_us = (uint16_t)task_keyboard_nkro_us;
  resp->task_gamepad_us = (uint16_t)task_gamepad_us;
  resp->task_led_us = (uint16_t)task_led_us;
  resp->task_total_us = (uint16_t)task_total_us;

  analog_task_monitor_t analog_monitor = {0};
  analog_get_task_monitor(&analog_monitor);
  resp->analog_raw_us = analog_monitor.raw_us;
  resp->analog_filter_us = analog_monitor.filter_us;
  resp->analog_calibration_us = analog_monitor.calibration_us;
  resp->analog_lut_us = analog_monitor.lut_us;
  resp->analog_store_us = analog_monitor.store_us;
  resp->analog_key_min_us = analog_monitor.key_min_us;
  resp->analog_key_max_us = analog_monitor.key_max_us;
  resp->analog_key_avg_us = analog_monitor.key_avg_us;
  resp->analog_nonzero_keys = analog_monitor.nonzero_keys;
}

static void cmd_get_raw_adc_chunk(const uint8_t *in, uint8_t *out) {
  const hid_req_raw_adc_chunk_t *req = (const hid_req_raw_adc_chunk_t *)in;
  hid_resp_adc_chunk_t *resp = (hid_resp_adc_chunk_t *)out;
  diagnostics_live_ping();

  resp->command_id = CMD_GET_RAW_ADC_CHUNK;
  resp->status = HID_RESP_OK;
  resp->start_index = req->start_index;
  resp->value_count = 0;
  memset(resp->adc_values, 0, sizeof(resp->adc_values));

  if (req->start_index >= NUM_KEYS) {
    resp->status = HID_RESP_INVALID_PARAM;
    return;
  }

  uint8_t remaining = (uint8_t)(NUM_KEYS - req->start_index);
  uint8_t count = remaining;
  if (count > HID_RAW_ADC_VALUES_PER_CHUNK) {
    count = HID_RAW_ADC_VALUES_PER_CHUNK;
  }

  resp->value_count = count;
  for (uint8_t i = 0; i < count; i++) {
    resp->adc_values[i] = analog_read_raw_value((uint8_t)(req->start_index + i));
  }
}

static void cmd_get_filtered_adc_chunk(const uint8_t *in, uint8_t *out) {
  const hid_req_raw_adc_chunk_t *req = (const hid_req_raw_adc_chunk_t *)in;
  hid_resp_adc_chunk_t *resp = (hid_resp_adc_chunk_t *)out;
  diagnostics_live_ping();

  resp->command_id = CMD_GET_FILTERED_ADC_CHUNK;
  resp->status = HID_RESP_OK;
  resp->start_index = req->start_index;
  resp->value_count = 0;
  memset(resp->adc_values, 0, sizeof(resp->adc_values));

  if (req->start_index >= NUM_KEYS) {
    resp->status = HID_RESP_INVALID_PARAM;
    return;
  }

  uint8_t remaining = (uint8_t)(NUM_KEYS - req->start_index);
  uint8_t count = remaining;
  if (count > HID_RAW_ADC_VALUES_PER_CHUNK) {
    count = HID_RAW_ADC_VALUES_PER_CHUNK;
  }

  resp->value_count = count;
  for (uint8_t i = 0; i < count; i++) {
    resp->adc_values[i] =
        analog_read_filtered_value((uint8_t)(req->start_index + i));
  }
}

static void cmd_get_calibrated_adc_chunk(const uint8_t *in, uint8_t *out) {
  const hid_req_raw_adc_chunk_t *req = (const hid_req_raw_adc_chunk_t *)in;
  hid_resp_adc_chunk_t *resp = (hid_resp_adc_chunk_t *)out;
  diagnostics_live_ping();

  resp->command_id = CMD_GET_CALIBRATED_ADC_CHUNK;
  resp->status = HID_RESP_OK;
  resp->start_index = req->start_index;
  resp->value_count = 0;
  memset(resp->adc_values, 0, sizeof(resp->adc_values));

  if (req->start_index >= NUM_KEYS) {
    resp->status = HID_RESP_INVALID_PARAM;
    return;
  }

  uint8_t remaining = (uint8_t)(NUM_KEYS - req->start_index);
  uint8_t count = remaining;
  if (count > HID_RAW_ADC_VALUES_PER_CHUNK) {
    count = HID_RAW_ADC_VALUES_PER_CHUNK;
  }

  resp->value_count = count;
  for (uint8_t i = 0; i < count; i++) {
    resp->adc_values[i] =
        analog_read_calibrated_value((uint8_t)(req->start_index + i));
  }
}

uint16_t triggerGetDistance01mm(int keyIndex) {
  if (keyIndex < 0 || keyIndex >= NUM_KEYS)
    return 0;
  return trigger_get_distance_01mm((uint8_t)keyIndex);
}

static void cmd_get_key_states(const uint8_t *in, uint8_t *out) {
  const hid_resp_key_states_t *req = (const hid_resp_key_states_t *)in;
  hid_resp_key_states_t *resp = (hid_resp_key_states_t *)out;
  diagnostics_live_ping();
  resp->command_id = CMD_GET_KEY_STATES;
  resp->start_index = req->start_index;
  resp->key_count = 0;

  if (req->start_index >= NUM_KEYS) {
    resp->status = HID_RESP_INVALID_PARAM;
    return;
  }

  uint8_t count = (uint8_t)(NUM_KEYS - req->start_index);
  if (count > HID_KEY_STATES_PER_CHUNK) {
    count = HID_KEY_STATES_PER_CHUNK;
  }

  resp->status = HID_RESP_OK;
  resp->key_count = count;

  for (uint8_t i = 0; i < count; i++) {
    uint8_t key = (uint8_t)(req->start_index + i);
    resp->keys[i].state = trigger_get_key_state(key) == PRESSED ? 1 : 0;
    resp->keys[i].distance_norm = analog_read_normalized_value(key);
    resp->keys[i].distance_01mm = triggerGetDistance01mm(key);
  }
}

static void cmd_get_mcu_metrics(const uint8_t *in, uint8_t *out) {
  hid_resp_mcu_metrics_t *resp = (hid_resp_mcu_metrics_t *)out;
  (void)in;
  diagnostics_live_ping();

  resp->command_id = CMD_GET_MCU_METRICS;
  resp->status = HID_RESP_OK;
  resp->temperature_c = mcu_temperature_c_live;
  resp->vref_mv = mcu_vref_mv_live;
  resp->core_clock_hz = SystemCoreClock;
  resp->scan_cycle_us = (uint16_t)mcu_scan_cycle_us_live;
  resp->scan_rate_hz = mcu_scan_cycle_us_live > 0u
                           ? (uint16_t)(1000000u / mcu_scan_cycle_us_live)
                           : 0u;
  resp->work_us = (uint16_t)mcu_work_us_live;
  resp->load_permille = mcu_load_permille_live;
  resp->temp_valid = mcu_temperature_valid_live ? 1u : 0u;
  memset(resp->reserved, 0, sizeof(resp->reserved));
}

static void cmd_get_lock_states(const uint8_t *in, uint8_t *out) {
  hid_packet_t *resp = (hid_packet_t *)out;
  diagnostics_live_ping();
  resp->command_id = CMD_GET_LOCK_STATES;
  resp->status_or_len = HID_RESP_OK;

  // Byte 2: Lock states as bit flags
  // Bit 0: Num Lock
  // Bit 1: Caps Lock
  // Bit 2: Scroll Lock
  uint8_t lock_state = 0;
  if (led_indicator_is_num_lock())
    lock_state |= 0x01;
  if (led_indicator_is_caps_lock())
    lock_state |= 0x02;
  if (led_indicator_is_scroll_lock())
    lock_state |= 0x04;
  resp->payload[0] = lock_state;
}

static void cmd_adc_capture_start(const uint8_t *in, uint8_t *out) {
  const hid_req_adc_capture_start_t *req =
      (const hid_req_adc_capture_start_t *)in;
  hid_resp_adc_capture_status_t *resp = (hid_resp_adc_capture_status_t *)out;
  diagnostics_live_ping();

  resp->command_id = CMD_ADC_CAPTURE_START;

  if (req->key_index >= NUM_KEYS || req->duration_ms == 0) {
    resp->status = HID_RESP_INVALID_PARAM;
    resp->active = 0;
    resp->key_index = req->key_index;
    resp->duration_ms = 0;
    resp->sample_count = 0;
    resp->overflow_count = 0;
    return;
  }

  bool ok = adc_capture_start(req->key_index, req->duration_ms);
  resp->status = ok ? HID_RESP_OK : HID_RESP_ERROR;
  resp->active = adc_capture_is_active() ? 1 : 0;
  resp->key_index = adc_capture_key_index();
  resp->duration_ms = adc_capture_duration_ms();
  resp->sample_count = adc_capture_sample_count();
  resp->overflow_count = adc_capture_overflow_count();
}

static void cmd_adc_capture_status(const uint8_t *in, uint8_t *out) {
  (void)in;
  hid_resp_adc_capture_status_t *resp = (hid_resp_adc_capture_status_t *)out;
  diagnostics_live_ping();

  resp->command_id = CMD_ADC_CAPTURE_STATUS;
  resp->status = HID_RESP_OK;
  resp->active = adc_capture_is_active() ? 1 : 0;
  resp->key_index = adc_capture_key_index();
  resp->duration_ms = adc_capture_duration_ms();
  resp->sample_count = adc_capture_sample_count();
  resp->overflow_count = adc_capture_overflow_count();
}

static void cmd_adc_capture_read(const uint8_t *in, uint8_t *out) {
  const hid_req_adc_capture_read_t *req = (const hid_req_adc_capture_read_t *)in;
  hid_resp_adc_capture_read_t *resp = (hid_resp_adc_capture_read_t *)out;
  diagnostics_live_ping();

  uint16_t raw_samples[ADC_CAPTURE_MAX_READ_SAMPLES] = {0};
  uint16_t filtered_samples[ADC_CAPTURE_MAX_READ_SAMPLES] = {0};
  uint32_t total_samples = 0;

  uint8_t max_samples = req->max_samples;
  if (max_samples == 0 || max_samples > ADC_CAPTURE_MAX_READ_SAMPLES) {
    max_samples = ADC_CAPTURE_MAX_READ_SAMPLES;
  }

  uint8_t count =
      adc_capture_read_chunk(req->start_index, max_samples, raw_samples,
                             filtered_samples, &total_samples);

  resp->command_id = CMD_ADC_CAPTURE_READ;
  resp->status = HID_RESP_OK;
  resp->active = adc_capture_is_active() ? 1 : 0;
  resp->key_index = adc_capture_key_index();
  resp->total_samples = total_samples;
  resp->start_index = req->start_index;
  resp->sample_count = count;

  for (uint8_t i = 0; i < count; i++) {
    resp->raw_samples[i] = raw_samples[i];
    resp->filtered_samples[i] = filtered_samples[i];
  }
}

static void cmd_echo(const uint8_t *in, uint8_t *out) {
  // Simply copy input to output with OK status
  memcpy(out, in, HID_PROTOCOL_PACKET_SIZE);
  out[0] = CMD_ECHO;
  out[1] = HID_RESP_OK;
}

static void cmd_unknown(const uint8_t *in, uint8_t *out) {
  hid_packet_t *resp = (hid_packet_t *)out;
  resp->command_id = in[0];
  resp->status_or_len = HID_RESP_INVALID_CMD;
}

//--------------------------------------------------------------------+
// Public API Implementation
//--------------------------------------------------------------------+

void hid_protocol_init(void) {
  hid_prepare_device_serial_cache();
  adc_capture_init();
}

bool hid_protocol_process(const uint8_t *in_packet, uint8_t *out_packet) {
  // Clear output buffer
  memset(out_packet, 0, HID_PROTOCOL_PACKET_SIZE);

  uint8_t cmd_id = in_packet[0];

  switch (cmd_id) {
  // System commands
  case CMD_GET_FIRMWARE_VERSION:
    cmd_get_firmware_version(in_packet, out_packet);
    break;

  case CMD_REBOOT:
    cmd_reboot(in_packet, out_packet);
    break;

  case CMD_ENTER_BOOTLOADER:
    cmd_enter_bootloader(in_packet, out_packet);
    break;

  case CMD_FACTORY_RESET:
    cmd_factory_reset(in_packet, out_packet);
    break;

  case CMD_USB_REENUMERATE:
    cmd_usb_reenumerate(in_packet, out_packet);
    break;

  // Settings commands
  case CMD_GET_OPTIONS:
    cmd_get_options(in_packet, out_packet);
    break;

  case CMD_SET_OPTIONS:
    cmd_set_options(in_packet, out_packet);
    break;

  case CMD_GET_KEYBOARD_ENABLED:
    cmd_get_keyboard_enabled(in_packet, out_packet);
    break;

  case CMD_SET_KEYBOARD_ENABLED:
    cmd_set_keyboard_enabled(in_packet, out_packet);
    break;

  case CMD_GET_GAMEPAD_ENABLED:
    cmd_get_gamepad_enabled(in_packet, out_packet);
    break;

  case CMD_SET_GAMEPAD_ENABLED:
    cmd_set_gamepad_enabled(in_packet, out_packet);
    break;

  case CMD_SAVE_SETTINGS:
    cmd_save_settings(in_packet, out_packet);
    break;

  case CMD_GET_NKRO_ENABLED:
    cmd_get_nkro_enabled(in_packet, out_packet);
    break;

  case CMD_SET_NKRO_ENABLED:
    cmd_set_nkro_enabled(in_packet, out_packet);
    break;

  case CMD_GET_ADVANCED_TICK_RATE:
    cmd_get_advanced_tick_rate(in_packet, out_packet);
    break;

  case CMD_SET_ADVANCED_TICK_RATE:
    cmd_set_advanced_tick_rate(in_packet, out_packet);
    break;

  case CMD_GET_TRIGGER_CHATTER_GUARD:
    cmd_get_trigger_chatter_guard(in_packet, out_packet);
    break;

  case CMD_SET_TRIGGER_CHATTER_GUARD:
    cmd_set_trigger_chatter_guard(in_packet, out_packet);
    break;

  case CMD_GET_DEVICE_INFO:
    cmd_get_device_info(in_packet, out_packet);
    break;

  case CMD_GET_KEYBOARD_NAME:
    cmd_get_keyboard_name(in_packet, out_packet);
    break;

  case CMD_SET_KEYBOARD_NAME:
    cmd_set_keyboard_name(in_packet, out_packet);
    break;

  case CMD_COPY_PROFILE_SLOT:
    cmd_copy_profile_slot(in_packet, out_packet);
    break;

  case CMD_RESET_PROFILE_SLOT:
    cmd_reset_profile_slot(in_packet, out_packet);
    break;

  case CMD_GET_DEFAULT_PROFILE:
    cmd_get_default_profile(in_packet, out_packet);
    break;

  case CMD_SET_DEFAULT_PROFILE:
    cmd_set_default_profile(in_packet, out_packet);
    break;

  case CMD_GET_RAM_ONLY_MODE:
    cmd_get_ram_only_mode(in_packet, out_packet);
    break;

  case CMD_SET_RAM_ONLY_MODE:
    cmd_set_ram_only_mode(in_packet, out_packet);
    break;

  case CMD_RELOAD_SETTINGS_FROM_FLASH:
    cmd_reload_settings_from_flash(in_packet, out_packet);
    break;

  // Key settings commands
  case CMD_GET_KEY_SETTINGS:
    cmd_get_key_settings(in_packet, out_packet);
    break;

  case CMD_SET_KEY_SETTINGS:
    cmd_set_key_settings(in_packet, out_packet);
    break;

  case CMD_GET_ALL_KEY_SETTINGS:
    cmd_get_all_key_settings(in_packet, out_packet);
    break;

  case CMD_SET_ALL_KEY_SETTINGS:
    cmd_set_all_key_settings(in_packet, out_packet);
    break;

  case CMD_GET_GAMEPAD_SETTINGS:
    cmd_get_gamepad_settings(in_packet, out_packet);
    break;

  case CMD_SET_GAMEPAD_SETTINGS:
    cmd_set_gamepad_settings(in_packet, out_packet);
    break;

  case CMD_GET_CALIBRATION:
    cmd_get_calibration(in_packet, out_packet);
    break;

  case CMD_SET_CALIBRATION:
    cmd_set_calibration(in_packet, out_packet);
    break;

  case CMD_AUTO_CALIBRATE:
    cmd_auto_calibrate(in_packet, out_packet);
    break;

  case CMD_GET_KEY_CURVE:
    cmd_get_key_curve(in_packet, out_packet);
    break;

  case CMD_SET_KEY_CURVE:
    cmd_set_key_curve(in_packet, out_packet);
    break;

  case CMD_GET_KEY_GAMEPAD_MAP:
    cmd_get_key_gamepad_map(in_packet, out_packet);
    break;

  case CMD_SET_KEY_GAMEPAD_MAP:
    cmd_set_key_gamepad_map(in_packet, out_packet);
    break;

  case CMD_GET_CALIBRATION_MAX:
    cmd_get_calibration_max(in_packet, out_packet);
    break;

  case CMD_SET_CALIBRATION_MAX:
    cmd_set_calibration_max(in_packet, out_packet);
    break;

  case CMD_GUIDED_CALIBRATION_START:
    cmd_guided_calibration_start(in_packet, out_packet);
    break;

  case CMD_GUIDED_CALIBRATION_STATUS:
    cmd_guided_calibration_status(in_packet, out_packet);
    break;

  case CMD_GUIDED_CALIBRATION_ABORT:
    cmd_guided_calibration_abort(in_packet, out_packet);
    break;

  case CMD_GET_ROTARY_ENCODER_SETTINGS:
    cmd_get_rotary_encoder_settings(in_packet, out_packet);
    break;

  case CMD_SET_ROTARY_ENCODER_SETTINGS:
    cmd_set_rotary_encoder_settings(in_packet, out_packet);
    break;

  case CMD_GET_LAYER_KEYCODE:
    cmd_get_layer_keycode(in_packet, out_packet);
    break;

  case CMD_SET_LAYER_KEYCODE:
    cmd_set_layer_keycode(in_packet, out_packet);
    break;

  case CMD_RESET_KEY_TRIGGER_SETTINGS:
    cmd_reset_key_trigger_settings(in_packet, out_packet);
    break;

  case CMD_GET_ROTARY_STATE:
    cmd_get_rotary_state(in_packet, out_packet);
    break;

  case CMD_GET_ACTIVE_PROFILE:
    cmd_get_active_profile(in_packet, out_packet);
    break;

  case CMD_SET_ACTIVE_PROFILE:
    cmd_set_active_profile(in_packet, out_packet);
    break;

  case CMD_GET_PROFILE_NAME:
    cmd_get_profile_name(in_packet, out_packet);
    break;

  case CMD_SET_PROFILE_NAME:
    cmd_set_profile_name(in_packet, out_packet);
    break;

  case CMD_CREATE_PROFILE:
    cmd_create_profile(in_packet, out_packet);
    break;

  case CMD_DELETE_PROFILE:
    cmd_delete_profile(in_packet, out_packet);
    break;

  // LED Matrix commands
  case CMD_GET_LED_ENABLED:
    cmd_get_led_enabled(in_packet, out_packet);
    break;

  case CMD_SET_LED_ENABLED:
    cmd_set_led_enabled(in_packet, out_packet);
    break;

  case CMD_GET_LED_BRIGHTNESS:
    cmd_get_led_brightness(in_packet, out_packet);
    break;

  case CMD_SET_LED_BRIGHTNESS:
    cmd_set_led_brightness(in_packet, out_packet);
    break;

  case CMD_GET_LED_PIXEL:
    cmd_get_led_pixel(in_packet, out_packet);
    break;

  case CMD_SET_LED_PIXEL:
    cmd_set_led_pixel(in_packet, out_packet);
    break;

  case CMD_GET_LED_ROW:
    cmd_get_led_row(in_packet, out_packet);
    break;

  case CMD_SET_LED_ROW:
    cmd_set_led_row(in_packet, out_packet);
    break;

  case CMD_GET_LED_ALL:
    cmd_get_led_all(in_packet, out_packet);
    break;

  case CMD_SET_LED_ALL_CHUNK:
    cmd_set_led_all_chunk(in_packet, out_packet);
    break;

  case CMD_LED_CLEAR:
    cmd_led_clear(in_packet, out_packet);
    break;

  case CMD_LED_FILL:
    cmd_led_fill(in_packet, out_packet);
    break;

  case CMD_LED_TEST_RAINBOW:
    cmd_led_test_rainbow(in_packet, out_packet);
    break;

  case CMD_GET_LED_EFFECT:
    cmd_get_led_effect(in_packet, out_packet);
    break;

  case CMD_SET_LED_EFFECT:
    cmd_set_led_effect(in_packet, out_packet);
    break;

  case CMD_GET_LED_FPS_LIMIT:
    cmd_get_led_fps_limit(in_packet, out_packet);
    break;

  case CMD_SET_LED_FPS_LIMIT:
    cmd_set_led_fps_limit(in_packet, out_packet);
    break;

  case CMD_GET_LED_EFFECT_PARAMS:
    cmd_get_led_effect_params(in_packet, out_packet);
    break;

  case CMD_SET_LED_EFFECT_PARAMS:
    cmd_set_led_effect_params(in_packet, out_packet);
    break;

  case CMD_SET_LED_VOLUME_OVERLAY:
    cmd_set_led_volume_overlay(in_packet, out_packet);
    break;

  case CMD_CLEAR_LED_VOLUME_OVERLAY:
    cmd_clear_led_volume_overlay(in_packet, out_packet);
    break;

  case CMD_RESTORE_LED_EFFECT_BEFORE_THIRD_PARTY:
    cmd_restore_led_effect_before_third_party(in_packet, out_packet);
    break;

  case CMD_GET_LED_EFFECT_SCHEMA:
    cmd_get_led_effect_schema(in_packet, out_packet);
    break;

  case CMD_SET_LED_AUDIO_SPECTRUM:
    cmd_set_led_audio_spectrum(in_packet, out_packet);
    break;

  case CMD_CLEAR_LED_AUDIO_SPECTRUM:
    cmd_clear_led_audio_spectrum(in_packet, out_packet);
    break;

  case CMD_SET_LED_ALPHA_MASK:
    cmd_set_led_alpha_mask(in_packet, out_packet);
    break;

  case CMD_GET_LED_IDLE_OPTIONS:
    cmd_get_led_idle_options(in_packet, out_packet);
    break;

  case CMD_SET_LED_IDLE_OPTIONS:
    cmd_set_led_idle_options(in_packet, out_packet);
    break;

  case CMD_GET_LED_USB_SUSPEND_RGB_OFF:
    cmd_get_led_usb_suspend_rgb_off(in_packet, out_packet);
    break;

  case CMD_SET_LED_USB_SUSPEND_RGB_OFF:
    cmd_set_led_usb_suspend_rgb_off(in_packet, out_packet);
    break;

  // Filter commands
  case CMD_GET_FILTER_ENABLED:
    cmd_get_filter_enabled(in_packet, out_packet);
    break;

  case CMD_SET_FILTER_ENABLED:
    cmd_set_filter_enabled(in_packet, out_packet);
    break;

  case CMD_GET_FILTER_PARAMS:
    cmd_get_filter_params(in_packet, out_packet);
    break;

  case CMD_SET_FILTER_PARAMS:
    cmd_set_filter_params(in_packet, out_packet);
    break;

  // Debug commands
  case CMD_GET_ADC_VALUES:
    cmd_get_adc_values(in_packet, out_packet);
    break;

  case CMD_GET_KEY_STATES:
    cmd_get_key_states(in_packet, out_packet);
    break;

  case CMD_GET_LOCK_STATES:
    cmd_get_lock_states(in_packet, out_packet);
    break;

  case CMD_ADC_CAPTURE_START:
    cmd_adc_capture_start(in_packet, out_packet);
    break;

  case CMD_ADC_CAPTURE_STATUS:
    cmd_adc_capture_status(in_packet, out_packet);
    break;

  case CMD_ADC_CAPTURE_READ:
    cmd_adc_capture_read(in_packet, out_packet);
    break;

  case CMD_GET_RAW_ADC_CHUNK:
    cmd_get_raw_adc_chunk(in_packet, out_packet);
    break;

  case CMD_GET_FILTERED_ADC_CHUNK:
    cmd_get_filtered_adc_chunk(in_packet, out_packet);
    break;

  case CMD_GET_CALIBRATED_ADC_CHUNK:
    cmd_get_calibrated_adc_chunk(in_packet, out_packet);
    break;

  case CMD_GET_MCU_METRICS:
    cmd_get_mcu_metrics(in_packet, out_packet);
    break;

  // Echo for testing
  case CMD_ECHO:
    cmd_echo(in_packet, out_packet);
    break;

  default:
    cmd_unknown(in_packet, out_packet);
    break;
  }

  return true; // Always send a response
}
