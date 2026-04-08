#include "hid/keyboard_hid.h"
#include "layout/keycodes.h"
#include <stdint.h>
#include "board_config.h"
#include "layout/layout.h"

uint16_t DEFAULT_BASE_LAYER[NUM_KEYS] = {
    KC_ESCAPE, KC_F1, KC_F2, KC_F3, KC_F4, KC_F5, KC_F6, KC_F7, KC_F8, KC_F9, KC_F10, KC_F11, KC_F12, KC_DELETE,
    KC_GRAVE, KC_1, KC_2, KC_3, KC_4, KC_5, KC_6, KC_7, KC_8, KC_9, KC_0, KC_MINUS, KC_EQUAL, KC_BACKSPACE, KC_PAGE_UP,
    KC_TAB, KC_Q, KC_W, KC_E, KC_R, KC_T, KC_Y, KC_U, KC_I, KC_O, KC_P, KC_LEFT_BRACKET, KC_RIGHT_BRACKET, KC_ENTER, KC_PAGE_UP,
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

void layout_press(uint8_t key) {
    uint16_t keycode = layers[active_layer][key];

    if (DISABLE_INPUT) {
        return;
    }

    switch(keycode) {
        case CUSTOM_FN:
            set_active_layer(1);
            break;

        default:
            keyboard_hid_key_press(keycode);
            break;
    }
}

void layout_release(uint8_t key) {
    uint16_t keycode = layers[active_layer][key];

    if (DISABLE_INPUT) {
        return;
    }

    switch(keycode) {
        case CUSTOM_FN:
            set_active_layer(0);
            break;

        default:
            keyboard_hid_key_release(keycode);
            break;
    }
}