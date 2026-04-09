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

    if (settings->resolution_mode == SETTINGS_SOCD_RESOLUTION_LAST_INPUT_WINS) {
        // Release the linked key
        layout_release(linked_key);
        socd_override_states[linked_key] = true;
    }
}

inline void socd_on_release(uint8_t key) {
    socd_key_settings_t *settings = &socd_key_settings[key];

    if (!settings->is_socd_enabled) {
        return;
    }

    uint8_t linked_key = settings->linked_key;

    if (settings->resolution_mode == SETTINGS_SOCD_RESOLUTION_LAST_INPUT_WINS) {
        key_state_e linked_key_state = trigger_get_key_state(linked_key);

        // If the linked key is still pressed and was overridden, clear the override state and press it again
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
    for (uint8_t i = 0; i < NUM_KEYS; i++) {
        const settings_key_t *key_settings = settings_get_key(i);
        uint8_t linked_key = SETTINGS_SOCD_PAIR_NONE;

        socd_reset_key_config(i);
        if (key_settings == NULL) {
            continue;
        }

        linked_key = key_settings->socd_pair;
        if (linked_key == SETTINGS_SOCD_PAIR_NONE || linked_key >= NUM_KEYS || linked_key == i) {
            continue;
        }

        socd_key_settings[i].resolution_mode =
            (socd_resolution_e)settings_key_get_socd_resolution(key_settings);
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
            handle_most_pressed_wins(key1, key2);
        }
    }
}
