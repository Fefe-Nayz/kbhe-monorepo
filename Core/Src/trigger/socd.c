#include <stdbool.h>
#include <stdint.h>
#include "trigger/socd.h"
#include "board_config.h"
#include "hid/keyboard_hid.h"
#include "analog/analog.h"
#include "layout/layout.h"
#include "trigger/trigger.h"

static socd_key_settings_t socd_key_settings[NUM_KEYS];

static bool socd_override_states[NUM_KEYS];

static void socd_reset_key_config(uint8_t key) {
    socd_override_states[key] = false;
    socd_key_settings[key].resolution_mode = SETTINGS_SOCD_RESOLUTION_LAST_INPUT_WINS;
    socd_key_settings[key].is_socd_enabled = false;
    socd_key_settings[key].fully_pressed_enabled = false;
    socd_key_settings[key].fully_pressed_point_um =
        (uint16_t)SETTINGS_SOCD_FULLY_PRESSED_POINT_DEFAULT_TENTHS * 100u;
    socd_key_settings[key].linked_key = key;
}

/*
 * Most pressed wins resolution logic
 */

static inline void handle_most_pressed_wins(uint8_t key1, uint8_t key2) {
    int16_t key1_distance = analog_read_distance_value(key1);
    int16_t key2_distance = analog_read_distance_value(key2);

    if (key1_distance >= key2_distance) {
        // Release key 2
        layout_release(key2);
        socd_override_states[key2] = true;
        
        // Press key 1
        layout_press(key1);
        socd_override_states[key1] = false;
    } else {
        // Release key 1
        layout_release(key1);
        socd_override_states[key1] = true;
        
        // Press key 2
        layout_press(key2);
        socd_override_states[key2] = false;
    }
}

/*
 * Last input wins resolution logic
 */

inline void socd_on_press(uint8_t key) {
    socd_key_settings_t *settings = &socd_key_settings[key];

    if (!settings->is_socd_enabled) {
        return;
    }

    uint8_t linked_key = settings->linked_key;

    switch (settings->resolution_mode) {
    case SETTINGS_SOCD_RESOLUTION_LAST_INPUT_WINS:
        layout_release(linked_key);
        socd_override_states[linked_key] = true;
        break;

    case SETTINGS_SOCD_RESOLUTION_ABSOLUTE_PRIORITY_KEY1:
        if (key < linked_key) {
            layout_release(linked_key);
            socd_override_states[linked_key] = true;
        } else {
            layout_release(key);
            socd_override_states[key] = true;
        }
        break;

    case SETTINGS_SOCD_RESOLUTION_ABSOLUTE_PRIORITY_KEY2:
        if (key < linked_key) {
            layout_release(key);
            socd_override_states[key] = true;
        } else {
            layout_release(linked_key);
            socd_override_states[linked_key] = true;
        }
        break;

    case SETTINGS_SOCD_RESOLUTION_NEUTRAL:
        layout_release(key);
        layout_release(linked_key);
        socd_override_states[key] = true;
        socd_override_states[linked_key] = true;
        break;

    default:
        break;
    }
}

inline void socd_on_release(uint8_t key) {
    socd_key_settings_t *settings = &socd_key_settings[key];

    if (!settings->is_socd_enabled) {
        return;
    }

    uint8_t linked_key = settings->linked_key;

    if (settings->resolution_mode == SETTINGS_SOCD_RESOLUTION_LAST_INPUT_WINS ||
        settings->resolution_mode == SETTINGS_SOCD_RESOLUTION_ABSOLUTE_PRIORITY_KEY1 ||
        settings->resolution_mode == SETTINGS_SOCD_RESOLUTION_ABSOLUTE_PRIORITY_KEY2 ||
        settings->resolution_mode == SETTINGS_SOCD_RESOLUTION_NEUTRAL) {
        key_state_e key_state = trigger_get_key_state(key);
        key_state_e linked_key_state = trigger_get_key_state(linked_key);

        if (key_state == PRESSED && socd_override_states[key]) {
            layout_press(key);
            socd_override_states[key] = false;
        }

        if (linked_key_state == PRESSED && socd_override_states[linked_key]) {
            layout_press(linked_key);
            socd_override_states[linked_key] = false;
        }
    }
}

void socd_init() {
    for (uint8_t i = 0; i < NUM_KEYS; i++) {
        socd_reset_key_config(i);
    }

    socd_load_settings();
}

void socd_load_settings() {
    uint8_t active_layer = layout_get_active_layer_top();

    if (active_layer >= SETTINGS_LAYER_COUNT) {
        active_layer = 0u;
    }

    for (uint8_t i = 0; i < NUM_KEYS; i++) {
        settings_key_t key_settings_storage = {0};
        const settings_key_t *key_settings = NULL;
        uint8_t linked_key = SETTINGS_SOCD_PAIR_NONE;

        socd_reset_key_config(i);
        if (!settings_get_key_for_layer(i, active_layer, &key_settings_storage)) {
            continue;
        }

        key_settings = &key_settings_storage;

        linked_key = key_settings->socd_pair;
        if (linked_key == SETTINGS_SOCD_PAIR_NONE || linked_key >= NUM_KEYS || linked_key == i) {
            continue;
        }

        socd_key_settings[i].resolution_mode =
            (socd_resolution_e)settings_key_get_socd_resolution(key_settings);
        socd_key_settings[i].fully_pressed_enabled =
            settings_key_is_socd_fully_pressed_enabled(key_settings);
        socd_key_settings[i].fully_pressed_point_um =
            (uint16_t)key_settings->advanced.socd_fully_pressed_point_tenths * 100u;
        socd_key_settings[i].is_socd_enabled = true;
        socd_key_settings[i].linked_key = linked_key;
    }
}

void socd_task() {
    for (uint8_t key1 = 0; key1 < NUM_KEYS; key1++) {
        socd_key_settings_t *settings = &socd_key_settings[key1];

        if (!settings->is_socd_enabled) {
            continue;
        }
        
        uint8_t key2 = settings->linked_key;

        if (key1 >= key2) {
            continue;
        }

        if (settings->resolution_mode != SETTINGS_SOCD_RESOLUTION_MOST_PRESSED_WINS) {
            continue;
        }

        key_state_e key1_state = trigger_get_key_state(key1);
        key_state_e key2_state = trigger_get_key_state(key2);

        if (key1_state == PRESSED && key2_state == PRESSED) {
            if (settings->fully_pressed_enabled) {
                int16_t key1_distance = analog_read_distance_value(key1);
                int16_t key2_distance = analog_read_distance_value(key2);

                if (key1_distance >= (int16_t)settings->fully_pressed_point_um &&
                    key2_distance >= (int16_t)settings->fully_pressed_point_um) {
                    layout_press(key1);
                    layout_press(key2);
                    socd_override_states[key1] = false;
                    socd_override_states[key2] = false;
                    continue;
                }
            }

            handle_most_pressed_wins(key1, key2);
        }
    }
}
