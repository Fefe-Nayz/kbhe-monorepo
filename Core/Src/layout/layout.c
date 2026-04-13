#include "hid/consumer_hid.h"
#include "hid/gamepad_hid.h"
#include "hid/keyboard_hid.h"
#include "hid/keyboard_nkro_hid.h"
#include "hid/mouse_hid.h"
#include "layout/keycodes.h"
#include "settings.h"
#include <stdbool.h>
#include <stdint.h>
#include "board_config.h"
#include "class/hid/hid.h"
#include "layout/layout.h"

// Default layer matches the physical 75HE ISO-FR layout from
// `keyboard-layout(3).json`, but uses standard USB HID positional keycodes.
// Example: the physical key labeled "A" sends KC_Q so the host OS can apply
// the active FR layout normally.
static const uint16_t DEFAULT_BASE_LAYER[NUM_KEYS] = {
    KC_ESCAPE, KC_F1, KC_F2, KC_F3, KC_F4, KC_F5, KC_F6, KC_F7, KC_F8,
    KC_F9,     KC_F10, KC_F11, KC_F12, KC_DELETE, KC_GRAVE, KC_1,
    KC_2,      KC_3,   KC_4,   KC_5,   KC_6,      KC_7,     KC_8,
    KC_9,      KC_0,   KC_MINUS, KC_EQUAL, KC_BACKSPACE, KC_PAGE_UP, KC_TAB,
    KC_Q,      KC_W,   KC_E,   KC_R,   KC_T,      KC_Y,     KC_U,
    KC_I,      KC_O,   KC_P,   KC_LEFT_BRACKET, KC_RIGHT_BRACKET, KC_ENTER,
    KC_PAGE_DOWN, KC_CAPS_LOCK, KC_A, KC_S, KC_D, KC_F, KC_G, KC_H, KC_J,
    KC_K,      KC_L,   KC_SEMICOLON, KC_QUOTE, KC_NONUS_HASH, KC_HOME,
    KC_LEFT_SHIFT, KC_NONUS_BACKSLASH, KC_Z, KC_X, KC_C, KC_V, KC_B, KC_N,
    KC_M,      KC_COMMA, KC_DOT, KC_SLASH, KC_RIGHT_SHIFT, KC_UP, KC_LEFT_CTRL,
    KC_LEFT_GUI, KC_LEFT_ALT, KC_SPACE, KC_RIGHT_ALT, CUSTOM_FN, KC_RIGHT_CTRL,
    KC_LEFT, KC_DOWN, KC_RIGHT};

static const uint8_t LED_BRIGHTNESS_STEP = 16u;
static const uint8_t LED_SPEED_STEP = 8u;

static uint8_t layer_hold_counts[SETTINGS_LAYER_COUNT] = {0};
static uint8_t layer_toggle_mask = 0u;
static uint16_t pressed_keycodes[NUM_KEYS] = {0};
static bool pressed_host_output[NUM_KEYS] = {0};
static bool keyboard_route_initialized = false;
static bool keyboard_route_use_nkro = false;

static uint16_t layout_default_overlay_keycode(uint8_t layer, uint8_t key) {
  if (key >= NUM_KEYS) {
    return KC_NO;
  }

  switch (layer) {
  case 1u:
    switch (key) {
    case 0u:
      return CUSTOM_LED_TOGGLE;
    case 1u:
      return CUSTOM_LED_BRIGHTNESS_DOWN;
    case 2u:
      return CUSTOM_LED_BRIGHTNESS_UP;
    case 3u:
      return CUSTOM_LED_EFFECT_PREV;
    case 4u:
      return CUSTOM_LED_EFFECT_NEXT;
    case 5u:
      return CUSTOM_LED_SPEED_DOWN;
    case 6u:
      return CUSTOM_LED_SPEED_UP;
    case 7u:
      return KC_AUDIO_VOL_DOWN;
    case 8u:
      return KC_MEDIA_PLAY_PAUSE;
    case 9u:
      return KC_AUDIO_VOL_UP;
    case 10u:
      return CUSTOM_LAYER_TG_2;
    case 11u:
      return CUSTOM_LAYER_SET_0;
    case 12u:
      return CUSTOM_LAYER_CLEAR;
    case 13u:
      return CUSTOM_GAMEPAD_TOGGLE;
    case 71u:
      return CUSTOM_LED_BRIGHTNESS_UP;
    case 79u:
      return CUSTOM_LED_EFFECT_PREV;
    case 80u:
      return CUSTOM_LED_BRIGHTNESS_DOWN;
    case 81u:
      return CUSTOM_LED_EFFECT_NEXT;
    default:
      return KC_TRANSPARENT;
    }
  case 2u:
  case 3u:
  default:
    return KC_TRANSPARENT;
  }
}

static bool layout_is_modifier_keycode(uint16_t keycode) {
  return (keycode >= KC_LEFT_CTRL) && (keycode <= KC_RIGHT_GUI);
}

static bool layout_is_keyboard_page_keycode(uint16_t keycode) {
  if ((keycode == KC_NO) || (keycode == KC_TRANSPARENT)) {
    return false;
  }

  if (layout_is_modifier_keycode(keycode)) {
    return true;
  }

  return (keycode >= KC_A) && (keycode <= KC_EXSEL);
}

static bool layout_should_enable_nkro_route(void) {
  if (!settings_is_nkro_enabled()) {
    return false;
  }

  if (keyboard_hid_is_boot_protocol_active()) {
    return false;
  }

  return keyboard_nkro_hid_can_route_keycodes();
}

static void layout_sync_keyboard_route(void) {
  bool use_nkro = layout_should_enable_nkro_route();

  if (!keyboard_route_initialized) {
    keyboard_route_initialized = true;
    keyboard_route_use_nkro = use_nkro;
    return;
  }

  if (keyboard_route_use_nkro == use_nkro) {
    return;
  }

  for (uint8_t key = 0u; key < NUM_KEYS; key++) {
    if (pressed_keycodes[key] != KC_NO) {
      return;
    }
  }

  // On route transition, clear both interfaces so one logical keyboard path
  // remains active and no stale pressed key stays latched.
  (void)keyboard_hid_release_all();
  keyboard_hid_reset_state();
  keyboard_nkro_hid_release_all();
  keyboard_route_use_nkro = use_nkro;
}

static bool layout_should_use_nkro_keycode(uint16_t keycode) {
  layout_sync_keyboard_route();

  if (!keyboard_route_use_nkro) {
    return false;
  }

  return layout_is_modifier_keycode(keycode) || (keycode < 128u);
}

static uint16_t layout_consumer_usage_from_keycode(uint16_t keycode) {
  switch (keycode) {
  case KC_AUDIO_MUTE:
    return HID_USAGE_CONSUMER_MUTE;
  case KC_AUDIO_VOL_UP:
    return HID_USAGE_CONSUMER_VOLUME_INCREMENT;
  case KC_AUDIO_VOL_DOWN:
    return HID_USAGE_CONSUMER_VOLUME_DECREMENT;
  case KC_MEDIA_NEXT_TRACK:
    return HID_USAGE_CONSUMER_SCAN_NEXT_TRACK;
  case KC_MEDIA_PREV_TRACK:
    return HID_USAGE_CONSUMER_SCAN_PREVIOUS_TRACK;
  case KC_MEDIA_STOP:
    return HID_USAGE_CONSUMER_STOP;
  case KC_MEDIA_PLAY_PAUSE:
    return HID_USAGE_CONSUMER_PLAY_PAUSE;
  case KC_MEDIA_SELECT:
    return HID_USAGE_CONSUMER_MEDIA_SELECTION;
  case KC_MEDIA_EJECT:
    return HID_USAGE_CONSUMER_EJECT;
  case KC_MAIL:
    return HID_USAGE_CONSUMER_AL_EMAIL_READER;
  case KC_CALCULATOR:
    return HID_USAGE_CONSUMER_AL_CALCULATOR;
  case KC_MY_COMPUTER:
    return HID_USAGE_CONSUMER_MEDIA_SELECT_COMPUTER;
  case KC_WWW_SEARCH:
    return HID_USAGE_CONSUMER_AC_SEARCH;
  case KC_WWW_HOME:
    return HID_USAGE_CONSUMER_AC_HOME;
  case KC_WWW_BACK:
    return HID_USAGE_CONSUMER_AC_BACK;
  case KC_WWW_FORWARD:
    return HID_USAGE_CONSUMER_AC_FORWARD;
  case KC_WWW_STOP:
    return HID_USAGE_CONSUMER_AC_STOP;
  case KC_WWW_REFRESH:
    return HID_USAGE_CONSUMER_AC_REFRESH;
  case KC_WWW_FAVORITES:
    return HID_USAGE_CONSUMER_AC_BOOKMARKS;
  case KC_MEDIA_FAST_FORWARD:
    return HID_USAGE_CONSUMER_FAST_FORWARD;
  case KC_MEDIA_REWIND:
    return HID_USAGE_CONSUMER_REWIND;
  case KC_BRIGHTNESS_UP:
    return HID_USAGE_CONSUMER_BRIGHTNESS_INCREMENT;
  case KC_BRIGHTNESS_DOWN:
    return HID_USAGE_CONSUMER_BRIGHTNESS_DECREMENT;
  case KC_CONTROL_PANEL:
    return HID_USAGE_CONSUMER_AL_CONTROL_PANEL;
  default:
    return 0u;
  }
}

static uint8_t layout_mouse_button_mask_from_keycode(uint16_t keycode) {
  switch (keycode) {
  case CUSTOM_MOUSE_LEFT:
    return MOUSE_HID_BUTTON_LEFT;
  case CUSTOM_MOUSE_RIGHT:
    return MOUSE_HID_BUTTON_RIGHT;
  case CUSTOM_MOUSE_MIDDLE:
    return MOUSE_HID_BUTTON_MIDDLE;
  case CUSTOM_MOUSE_BACK:
    return MOUSE_HID_BUTTON_BACK;
  case CUSTOM_MOUSE_FORWARD:
    return MOUSE_HID_BUTTON_FORWARD;
  default:
    return 0u;
  }
}

static uint8_t layout_gamepad_button_from_keycode(uint16_t keycode) {
  switch (keycode) {
  case CUSTOM_GAMEPAD_A:
    return (uint8_t)GAMEPAD_BUTTON_A;
  case CUSTOM_GAMEPAD_B:
    return (uint8_t)GAMEPAD_BUTTON_B;
  case CUSTOM_GAMEPAD_X:
    return (uint8_t)GAMEPAD_BUTTON_X;
  case CUSTOM_GAMEPAD_Y:
    return (uint8_t)GAMEPAD_BUTTON_Y;
  case CUSTOM_GAMEPAD_LB:
    return (uint8_t)GAMEPAD_BUTTON_L1;
  case CUSTOM_GAMEPAD_RB:
    return (uint8_t)GAMEPAD_BUTTON_R1;
  case CUSTOM_GAMEPAD_BACK:
    return (uint8_t)GAMEPAD_BUTTON_SELECT;
  case CUSTOM_GAMEPAD_START:
    return (uint8_t)GAMEPAD_BUTTON_START;
  case CUSTOM_GAMEPAD_L3:
    return (uint8_t)GAMEPAD_BUTTON_L3;
  case CUSTOM_GAMEPAD_R3:
    return (uint8_t)GAMEPAD_BUTTON_R3;
  case CUSTOM_GAMEPAD_DPAD_UP:
    return (uint8_t)GAMEPAD_BUTTON_DPAD_UP;
  case CUSTOM_GAMEPAD_DPAD_DOWN:
    return (uint8_t)GAMEPAD_BUTTON_DPAD_DOWN;
  case CUSTOM_GAMEPAD_DPAD_LEFT:
    return (uint8_t)GAMEPAD_BUTTON_DPAD_LEFT;
  case CUSTOM_GAMEPAD_DPAD_RIGHT:
    return (uint8_t)GAMEPAD_BUTTON_DPAD_RIGHT;
  case CUSTOM_GAMEPAD_HOME:
    return (uint8_t)GAMEPAD_BUTTON_HOME;
  default:
    return (uint8_t)GAMEPAD_BUTTON_NONE;
  }
}

static bool layout_gamepad_axis_from_keycode(uint16_t keycode, uint8_t *axis,
                                             uint8_t *direction) {
  uint8_t resolved_axis = (uint8_t)GAMEPAD_AXIS_NONE;
  uint8_t resolved_direction = (uint8_t)GAMEPAD_DIR_POSITIVE;

  switch (keycode) {
  case CUSTOM_GAMEPAD_LT:
    resolved_axis = (uint8_t)GAMEPAD_AXIS_TRIGGER_L;
    resolved_direction = (uint8_t)GAMEPAD_DIR_POSITIVE;
    break;
  case CUSTOM_GAMEPAD_RT:
    resolved_axis = (uint8_t)GAMEPAD_AXIS_TRIGGER_R;
    resolved_direction = (uint8_t)GAMEPAD_DIR_POSITIVE;
    break;
  case CUSTOM_GAMEPAD_LS_RIGHT:
    resolved_axis = (uint8_t)GAMEPAD_AXIS_LEFT_X;
    resolved_direction = (uint8_t)GAMEPAD_DIR_POSITIVE;
    break;
  case CUSTOM_GAMEPAD_LS_LEFT:
    resolved_axis = (uint8_t)GAMEPAD_AXIS_LEFT_X;
    resolved_direction = (uint8_t)GAMEPAD_DIR_NEGATIVE;
    break;
  case CUSTOM_GAMEPAD_LS_DOWN:
    resolved_axis = (uint8_t)GAMEPAD_AXIS_LEFT_Y;
    resolved_direction = (uint8_t)GAMEPAD_DIR_POSITIVE;
    break;
  case CUSTOM_GAMEPAD_LS_UP:
    resolved_axis = (uint8_t)GAMEPAD_AXIS_LEFT_Y;
    resolved_direction = (uint8_t)GAMEPAD_DIR_NEGATIVE;
    break;
  case CUSTOM_GAMEPAD_RS_RIGHT:
    resolved_axis = (uint8_t)GAMEPAD_AXIS_RIGHT_X;
    resolved_direction = (uint8_t)GAMEPAD_DIR_POSITIVE;
    break;
  case CUSTOM_GAMEPAD_RS_LEFT:
    resolved_axis = (uint8_t)GAMEPAD_AXIS_RIGHT_X;
    resolved_direction = (uint8_t)GAMEPAD_DIR_NEGATIVE;
    break;
  case CUSTOM_GAMEPAD_RS_DOWN:
    resolved_axis = (uint8_t)GAMEPAD_AXIS_RIGHT_Y;
    resolved_direction = (uint8_t)GAMEPAD_DIR_POSITIVE;
    break;
  case CUSTOM_GAMEPAD_RS_UP:
    resolved_axis = (uint8_t)GAMEPAD_AXIS_RIGHT_Y;
    resolved_direction = (uint8_t)GAMEPAD_DIR_NEGATIVE;
    break;
  default:
    return false;
  }

  if (axis != NULL) {
    *axis = resolved_axis;
  }
  if (direction != NULL) {
    *direction = resolved_direction;
  }

  return true;
}

static bool layout_should_emit_keyboard_for_key(uint8_t key) {
  const settings_key_t *settings_key = NULL;
  const settings_gamepad_t *gamepad = NULL;

  // Keyboard master switch always has priority over routing modes.
  if (!settings_is_keyboard_enabled()) {
    return false;
  }

  if (!settings_is_gamepad_enabled()) {
    return true;
  }

  settings_key = settings_get_key(key);
  if (settings_key != NULL && settings_key->disable_kb_on_gamepad) {
    return false;
  }

  gamepad = settings_get_gamepad();
  if (gamepad == NULL) {
    return true;
  }

  switch ((gamepad_keyboard_routing_t)gamepad->keyboard_routing) {
  case GAMEPAD_KEYBOARD_ROUTING_DISABLED:
    return false;
  case GAMEPAD_KEYBOARD_ROUTING_UNMAPPED_ONLY:
    return !settings_is_key_mapped_to_gamepad(key);
  case GAMEPAD_KEYBOARD_ROUTING_ALL_KEYS:
  default:
    return true;
  }
}

static void layout_dispatch_press(uint16_t keycode) {
  uint16_t consumer_usage = 0u;
  uint8_t mouse_button_mask = 0u;
  uint8_t gamepad_button = 0u;
  uint8_t gamepad_axis = 0u;
  uint8_t gamepad_direction = 0u;

  if (layout_is_keyboard_page_keycode(keycode)) {
    if (layout_should_use_nkro_keycode(keycode)) {
      keyboard_nkro_hid_key_press((uint8_t)keycode);
    } else {
      keyboard_hid_key_press((uint8_t)keycode);
    }
    return;
  }

  consumer_usage = layout_consumer_usage_from_keycode(keycode);
  if (consumer_usage != 0u) {
    (void)consumer_hid_send_usage(consumer_usage);
    return;
  }

  mouse_button_mask = layout_mouse_button_mask_from_keycode(keycode);
  if (mouse_button_mask != 0u) {
    mouse_hid_button_press(mouse_button_mask);
    return;
  }

  gamepad_button = layout_gamepad_button_from_keycode(keycode);
  if (gamepad_button != (uint8_t)GAMEPAD_BUTTON_NONE) {
    gamepad_hid_custom_button_press(gamepad_button);
    return;
  }

  if (layout_gamepad_axis_from_keycode(keycode, &gamepad_axis,
                                       &gamepad_direction)) {
    gamepad_hid_custom_axis_press(gamepad_axis, gamepad_direction);
    return;
  }

  switch (keycode) {
  case CUSTOM_MOUSE_WHEEL_UP:
    (void)mouse_hid_scroll(1, 0);
    break;
  case CUSTOM_MOUSE_WHEEL_DOWN:
    (void)mouse_hid_scroll(-1, 0);
    break;
  case CUSTOM_MOUSE_WHEEL_LEFT:
    (void)mouse_hid_scroll(0, -1);
    break;
  case CUSTOM_MOUSE_WHEEL_RIGHT:
    (void)mouse_hid_scroll(0, 1);
    break;
  default:
    break;
  }
}

static void layout_dispatch_release(uint16_t keycode) {
  uint8_t mouse_button_mask = 0u;
  uint8_t gamepad_button = 0u;
  uint8_t gamepad_axis = 0u;
  uint8_t gamepad_direction = 0u;

  if (layout_is_keyboard_page_keycode(keycode)) {
    if (layout_should_use_nkro_keycode(keycode)) {
      keyboard_nkro_hid_key_release((uint8_t)keycode);
    } else {
      keyboard_hid_key_release((uint8_t)keycode);
    }
    return;
  }

  mouse_button_mask = layout_mouse_button_mask_from_keycode(keycode);
  if (mouse_button_mask != 0u) {
    mouse_hid_button_release(mouse_button_mask);
    return;
  }

  gamepad_button = layout_gamepad_button_from_keycode(keycode);
  if (gamepad_button != (uint8_t)GAMEPAD_BUTTON_NONE) {
    gamepad_hid_custom_button_release(gamepad_button);
    return;
  }

  if (layout_gamepad_axis_from_keycode(keycode, &gamepad_axis,
                                       &gamepad_direction)) {
    gamepad_hid_custom_axis_release(gamepad_axis, gamepad_direction);
  }
}

static bool layout_is_momentary_layer_keycode(uint16_t keycode) {
  return keycode == CUSTOM_LAYER_MO_1 || keycode == CUSTOM_LAYER_MO_2 ||
         keycode == CUSTOM_LAYER_MO_3;
}

static bool layout_is_toggle_layer_keycode(uint16_t keycode) {
  return keycode == CUSTOM_LAYER_TG_1 || keycode == CUSTOM_LAYER_TG_2 ||
         keycode == CUSTOM_LAYER_TG_3;
}

static bool layout_is_set_layer_keycode(uint16_t keycode) {
  return keycode == CUSTOM_LAYER_SET_0 || keycode == CUSTOM_LAYER_SET_1 ||
         keycode == CUSTOM_LAYER_SET_2 || keycode == CUSTOM_LAYER_SET_3;
}

static bool layout_is_led_control_keycode(uint16_t keycode) {
  return keycode == CUSTOM_LED_TOGGLE ||
         keycode == CUSTOM_LED_BRIGHTNESS_DOWN ||
         keycode == CUSTOM_LED_BRIGHTNESS_UP ||
         keycode == CUSTOM_LED_EFFECT_PREV ||
         keycode == CUSTOM_LED_EFFECT_NEXT ||
         keycode == CUSTOM_LED_SPEED_DOWN ||
         keycode == CUSTOM_LED_SPEED_UP ||
         keycode == CUSTOM_LED_COLOR_NEXT;
}

static bool layout_is_gamepad_control_keycode(uint16_t keycode) {
  return keycode == CUSTOM_GAMEPAD_ENABLE ||
         keycode == CUSTOM_GAMEPAD_DISABLE ||
         keycode == CUSTOM_GAMEPAD_TOGGLE;
}

static bool layout_is_gamepad_action_keycode(uint16_t keycode) {
  uint8_t axis = 0u;
  uint8_t direction = 0u;

  if (layout_gamepad_button_from_keycode(keycode) !=
      (uint8_t)GAMEPAD_BUTTON_NONE) {
    return true;
  }

  return layout_gamepad_axis_from_keycode(keycode, &axis, &direction);
}

static bool layout_is_internal_keycode(uint16_t keycode) {
  return layout_is_momentary_layer_keycode(keycode) ||
         layout_is_toggle_layer_keycode(keycode) ||
         layout_is_set_layer_keycode(keycode) ||
         keycode == CUSTOM_LAYER_CLEAR || layout_is_led_control_keycode(keycode) ||
         layout_is_gamepad_control_keycode(keycode);
}

static uint8_t layout_layer_from_keycode(uint16_t keycode) {
  switch (keycode) {
  case CUSTOM_LAYER_MO_1:
  case CUSTOM_LAYER_TG_1:
  case CUSTOM_LAYER_SET_1:
    return 1u;
  case CUSTOM_LAYER_MO_2:
  case CUSTOM_LAYER_TG_2:
  case CUSTOM_LAYER_SET_2:
    return 2u;
  case CUSTOM_LAYER_MO_3:
  case CUSTOM_LAYER_TG_3:
  case CUSTOM_LAYER_SET_3:
    return 3u;
  case CUSTOM_LAYER_SET_0:
  default:
    return 0u;
  }
}

static bool layout_is_layer_active(uint8_t layer) {
  if (layer == 0u) {
    return true;
  }

  if (layer >= SETTINGS_LAYER_COUNT) {
    return false;
  }

  return layer_hold_counts[layer] > 0u ||
         (layer_toggle_mask & (uint8_t)(1u << layer)) != 0u;
}

static void layout_toggle_layer(uint8_t layer) {
  if (layer == 0u || layer >= SETTINGS_LAYER_COUNT) {
    return;
  }

  layer_toggle_mask ^= (uint8_t)(1u << layer);
}

static void layout_set_exclusive_layer(uint8_t layer) {
  if (layer >= SETTINGS_LAYER_COUNT) {
    return;
  }

  layer_toggle_mask = 0u;
  if (layer > 0u) {
    layer_toggle_mask = (uint8_t)(1u << layer);
  }
}

static uint8_t layout_next_effect(uint8_t current, int8_t direction) {
  uint8_t effect = current;

  for (uint8_t i = 0u; i < (uint8_t)LED_EFFECT_MAX; i++) {
    if (direction >= 0) {
      effect = (uint8_t)((effect + 1u) % (uint8_t)LED_EFFECT_MAX);
    } else {
      effect = (effect == 0u) ? (uint8_t)LED_EFFECT_MAX - 1u
                              : (uint8_t)(effect - 1u);
    }

    if (effect != (uint8_t)LED_EFFECT_THIRD_PARTY) {
      return effect;
    }
  }

  return current;
}

static void layout_cycle_led_color(void) {
  static const uint8_t palette[][3] = {
      {255u, 0u, 0u},   {255u, 128u, 0u}, {255u, 255u, 0u}, {0u, 255u, 0u},
      {0u, 180u, 255u}, {0u, 0u, 255u},   {180u, 0u, 255u}, {255u, 0u, 160u},
  };
  uint8_t r = 0u;
  uint8_t g = 0u;
  uint8_t b = 0u;
  uint8_t next = 0u;

  settings_get_led_effect_color(&r, &g, &b);

  for (uint8_t i = 0u; i < (uint8_t)(sizeof(palette) / sizeof(palette[0]));
       i++) {
    if (palette[i][0] == r && palette[i][1] == g && palette[i][2] == b) {
      next = (uint8_t)((i + 1u) % (sizeof(palette) / sizeof(palette[0])));
      settings_set_led_effect_color(palette[next][0], palette[next][1],
                                    palette[next][2]);
      return;
    }
  }

  settings_set_led_effect_color(palette[0][0], palette[0][1], palette[0][2]);
}

static void layout_handle_internal_press(uint16_t keycode) {
  uint8_t layer = layout_layer_from_keycode(keycode);
  uint8_t value = 0u;

  if (layout_is_momentary_layer_keycode(keycode)) {
    if (layer < SETTINGS_LAYER_COUNT && layer_hold_counts[layer] < 0xFFu) {
      layer_hold_counts[layer]++;
    }
    return;
  }

  if (layout_is_toggle_layer_keycode(keycode)) {
    layout_toggle_layer(layer);
    return;
  }

  if (layout_is_set_layer_keycode(keycode)) {
    layout_set_exclusive_layer(layer);
    return;
  }

  switch (keycode) {
  case CUSTOM_LAYER_CLEAR:
    layer_toggle_mask = 0u;
    break;
  case CUSTOM_LED_TOGGLE:
    (void)settings_set_led_enabled(!settings_is_led_enabled());
    break;
  case CUSTOM_LED_BRIGHTNESS_DOWN:
    value = settings_get_led_brightness();
    value = (value > LED_BRIGHTNESS_STEP) ? (uint8_t)(value - LED_BRIGHTNESS_STEP)
                                          : 0u;
    (void)settings_set_led_brightness(value);
    break;
  case CUSTOM_LED_BRIGHTNESS_UP:
    value = settings_get_led_brightness();
    value = (value >= (uint8_t)(255u - LED_BRIGHTNESS_STEP))
                ? 255u
                : (uint8_t)(value + LED_BRIGHTNESS_STEP);
    (void)settings_set_led_brightness(value);
    break;
  case CUSTOM_LED_EFFECT_PREV:
    value = layout_next_effect(settings_get_led_effect_mode(), -1);
    (void)settings_set_led_effect_mode(value);
    break;
  case CUSTOM_LED_EFFECT_NEXT:
    value = layout_next_effect(settings_get_led_effect_mode(), 1);
    (void)settings_set_led_effect_mode(value);
    break;
  case CUSTOM_LED_SPEED_DOWN:
    value = settings_get_led_effect_speed();
    value = (value > LED_SPEED_STEP) ? (uint8_t)(value - LED_SPEED_STEP) : 1u;
    (void)settings_set_led_effect_speed(value);
    break;
  case CUSTOM_LED_SPEED_UP:
    value = settings_get_led_effect_speed();
    value =
        (value >= (uint8_t)(255u - LED_SPEED_STEP)) ? 255u
                                                    : (uint8_t)(value + LED_SPEED_STEP);
    (void)settings_set_led_effect_speed(value);
    break;
  case CUSTOM_LED_COLOR_NEXT:
    layout_cycle_led_color();
    break;
  case CUSTOM_GAMEPAD_ENABLE:
    (void)settings_set_gamepad_enabled_live(true);
    break;
  case CUSTOM_GAMEPAD_DISABLE:
    (void)settings_set_gamepad_enabled_live(false);
    break;
  case CUSTOM_GAMEPAD_TOGGLE:
    (void)settings_set_gamepad_enabled_live(!settings_is_gamepad_enabled());
    break;
  default:
    break;
  }
}

static void layout_handle_internal_release(uint16_t keycode) {
  uint8_t layer = layout_layer_from_keycode(keycode);

  if (!layout_is_momentary_layer_keycode(keycode)) {
    return;
  }

  if (layer > 0u && layer < SETTINGS_LAYER_COUNT &&
      layer_hold_counts[layer] > 0u) {
    layer_hold_counts[layer]--;
  }
}

uint16_t layout_get_default_keycode(uint8_t key) {
  if (key >= NUM_KEYS) {
    return KC_NO;
  }

  return DEFAULT_BASE_LAYER[key];
}

uint16_t layout_get_default_layer_keycode(uint8_t layer, uint8_t key) {
  if (key >= NUM_KEYS) {
    return KC_NO;
  }

  if (layer == 0u) {
    return DEFAULT_BASE_LAYER[key];
  }

  if (layer >= SETTINGS_LAYER_COUNT) {
    return KC_NO;
  }

  return layout_default_overlay_keycode(layer, key);
}

uint16_t layout_get_active_keycode(uint8_t key) {
  uint16_t keycode = KC_NO;

  if (key >= NUM_KEYS) {
    return KC_NO;
  }

  for (int8_t layer = (int8_t)SETTINGS_LAYER_COUNT - 1; layer >= 0; layer--) {
    if (!layout_is_layer_active((uint8_t)layer)) {
      continue;
    }

    keycode = settings_get_layer_keycode((uint8_t)layer, key);
    if (keycode != KC_TRANSPARENT) {
      return keycode;
    }
  }

  return KC_NO;
}

void layout_press(uint8_t key) {
  uint16_t keycode = KC_NO;
  bool emit_host = false;

  if (key >= NUM_KEYS) {
    return;
  }

  keycode = layout_get_active_keycode(key);
  pressed_keycodes[key] = keycode;
  pressed_host_output[key] = false;

  if (layout_is_internal_keycode(keycode)) {
    layout_handle_internal_press(keycode);
    return;
  }

  if (layout_is_gamepad_action_keycode(keycode)) {
    pressed_host_output[key] = true;
    layout_dispatch_press(keycode);
    return;
  }

  emit_host = layout_should_emit_keyboard_for_key(key);
  pressed_host_output[key] = emit_host;
  if (emit_host) {
    layout_dispatch_press(keycode);
  }
}

void layout_release(uint8_t key) {
  uint16_t keycode = KC_NO;
  bool emit_host = false;

  if (key >= NUM_KEYS) {
    return;
  }

  keycode = pressed_keycodes[key];
  emit_host = pressed_host_output[key];
  pressed_keycodes[key] = KC_NO;
  pressed_host_output[key] = false;

  if (layout_is_internal_keycode(keycode)) {
    layout_handle_internal_release(keycode);
    return;
  }

  if (layout_is_gamepad_action_keycode(keycode)) {
    layout_dispatch_release(keycode);
    return;
  }

  if (emit_host) {
    layout_dispatch_release(keycode);
  }
}

void layout_press_action_for_key(uint8_t source_key, uint16_t keycode) {
  if (layout_is_internal_keycode(keycode)) {
    layout_handle_internal_press(keycode);
    return;
  }

  if (layout_is_gamepad_action_keycode(keycode)) {
    layout_dispatch_press(keycode);
    return;
  }

  if (layout_should_emit_keyboard_for_key(source_key)) {
    layout_dispatch_press(keycode);
  }
}

void layout_release_action_for_key(uint8_t source_key, uint16_t keycode) {
  if (layout_is_internal_keycode(keycode)) {
    layout_handle_internal_release(keycode);
    return;
  }

  if (layout_is_gamepad_action_keycode(keycode)) {
    layout_dispatch_release(keycode);
    return;
  }

  if (layout_should_emit_keyboard_for_key(source_key)) {
    layout_dispatch_release(keycode);
  }
}

void layout_reset_state(void) {
  for (uint8_t layer = 0u; layer < SETTINGS_LAYER_COUNT; layer++) {
    layer_hold_counts[layer] = 0u;
  }

  layer_toggle_mask = 0u;

  for (uint8_t key = 0u; key < NUM_KEYS; key++) {
    pressed_keycodes[key] = KC_NO;
    pressed_host_output[key] = false;
  }

  keyboard_route_initialized = false;
  keyboard_route_use_nkro = false;

  gamepad_hid_custom_clear();
}
