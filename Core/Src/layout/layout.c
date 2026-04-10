#include "hid/keyboard_hid.h"
#include "hid/keyboard_nkro_hid.h"
#include "hid/consumer_hid.h"
#include "hid/mouse_hid.h"
#include "layout/keycodes.h"
#include "settings.h"
#include <stddef.h>
#include <stdint.h>
#include "board_config.h"
#include "layout/layout.h"
#include "class/hid/hid.h"

// Default layer matches the physical 75HE ISO-FR layout from
// `keyboard-layout(3).json`, but uses standard USB HID positional keycodes.
// Example: the physical key labeled "A" sends KC_Q so the host OS can apply
// the active FR layout normally.
uint16_t DEFAULT_BASE_LAYER[NUM_KEYS] = {
    KC_ESCAPE, KC_F1, KC_F2, KC_F3, KC_F4, KC_F5, KC_F6, KC_F7, KC_F8, KC_F9, KC_F10, KC_F11, KC_F12, KC_DELETE,
    KC_GRAVE, KC_1, KC_2, KC_3, KC_4, KC_5, KC_6, KC_7, KC_8, KC_9, KC_0, KC_MINUS, KC_EQUAL, KC_BACKSPACE, KC_PAGE_UP,
    KC_TAB, KC_Q, KC_W, KC_E, KC_R, KC_T, KC_Y, KC_U, KC_I, KC_O, KC_P, KC_LEFT_BRACKET, KC_RIGHT_BRACKET, KC_ENTER, KC_PAGE_DOWN,
    KC_CAPS_LOCK, KC_A, KC_S, KC_D, KC_F, KC_G, KC_H, KC_J, KC_K, KC_L, KC_SEMICOLON, KC_QUOTE, KC_NONUS_HASH, KC_HOME,
    KC_LEFT_SHIFT, KC_NONUS_BACKSLASH, KC_Z, KC_X, KC_C, KC_V, KC_B, KC_N, KC_M, KC_COMMA, KC_DOT, KC_SLASH, KC_RIGHT_SHIFT, KC_UP,
    KC_LEFT_CTRL, KC_LEFT_GUI, KC_LEFT_ALT, KC_SPACE, KC_RIGHT_ALT, CUSTOM_FN, KC_RIGHT_CTRL, KC_LEFT, KC_DOWN, KC_RIGHT
};

uint16_t DEFAULT_FN_LAYER[NUM_KEYS] = {
    KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO,
    KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO,
    KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO,
    KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO,
    KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO,
    KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO
};

// uint16_t DEFAULT_BASE_LAYER[NUM_KEYS] = {
//     KC_Q, KC_W, KC_E,
//     KC_A, KC_S, KC_D, 
// };

// uint16_t DEFAULT_FN_LAYER[NUM_KEYS] = {
//     KC_NO, KC_AUDIO_VOL_DOWN, KC_AUDIO_VOL_DOWN,
//     KC_MEDIA_PREV_TRACK, KC_MEDIA_PLAY_PAUSE, KC_MEDIA_NEXT_TRACK, 
// };

static uint16_t* layers[2] = {
    [0] = DEFAULT_BASE_LAYER,
    [1] = DEFAULT_FN_LAYER
};

static uint8_t active_layer = 0;

static void set_active_layer(uint8_t layer) {
    active_layer = layer;
}

static bool layout_should_emit_keyboard_for_key(uint8_t key) {
    const settings_key_t *settings_key = NULL;
    const settings_gamepad_t *gamepad = NULL;

    if (!settings_is_gamepad_enabled()) {
        return settings_is_keyboard_enabled();
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

static bool layout_should_use_nkro_keycode(uint16_t keycode) {
    if (!settings_is_nkro_enabled()) {
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

static void layout_dispatch_press(uint16_t keycode) {
    uint16_t consumer_usage = 0u;
    uint8_t mouse_button_mask = 0u;

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
    case KC_SYSTEM_POWER:
    case KC_SYSTEM_SLEEP:
    case KC_SYSTEM_WAKE:
    case CUSTOM_FN:
    case KC_ASSISTANT:
    case KC_MISSION_CONTROL:
    case KC_LAUNCHPAD:
    default:
        break;
    }
}

static void layout_dispatch_release(uint16_t keycode) {
    uint8_t mouse_button_mask = 0u;

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
    }
}

uint16_t layout_get_default_keycode(uint8_t key) {
    if (key >= NUM_KEYS) {
        return KC_NO;
    }
    return DEFAULT_BASE_LAYER[key];
}

uint16_t layout_get_active_keycode(uint8_t key) {
    if (key >= NUM_KEYS) {
        return KC_NO;
    }

    if (active_layer == 0) {
        const settings_key_t *settings_key = settings_get_key(key);
        if (settings_key != NULL) {
            return settings_key->hid_keycode;
        }
    }

    return layers[active_layer][key];
}

void layout_press(uint8_t key) {
    uint16_t keycode = layout_get_active_keycode(key);

    switch(keycode) {
        case CUSTOM_FN:
            set_active_layer(1);
            break;

        default:
            if (layout_should_emit_keyboard_for_key(key)) {
                layout_dispatch_press(keycode);
            }
            break;
    }
}

void layout_release(uint8_t key) {
    uint16_t keycode = layout_get_active_keycode(key);

    switch(keycode) {
        case CUSTOM_FN:
            set_active_layer(0);
            break;

        default:
            if (layout_should_emit_keyboard_for_key(key)) {
                layout_dispatch_release(keycode);
            }
            break;
    }
}

void layout_press_action_for_key(uint8_t source_key, uint16_t keycode) {
    if (layout_should_emit_keyboard_for_key(source_key)) {
        if (keycode == CUSTOM_FN) {
            set_active_layer(1);
        } else {
            layout_dispatch_press(keycode);
        }
    }
}

void layout_release_action_for_key(uint8_t source_key, uint16_t keycode) {
    if (layout_should_emit_keyboard_for_key(source_key)) {
        if (keycode == CUSTOM_FN) {
            set_active_layer(0);
        } else {
            layout_dispatch_release(keycode);
        }
    }
}

void layout_reset_state(void) {
    set_active_layer(0);
}
