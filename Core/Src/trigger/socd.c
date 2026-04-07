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

    if (settings->resolution_mode == LAST_INPUT_WINS) {
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

    if (settings->resolution_mode == LAST_INPUT_WINS) {
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
        socd_override_states[i] = false;

        socd_key_settings[i].resolution_mode = LAST_INPUT_WINS;
        socd_key_settings[i].is_socd_enabled = false;
        socd_key_settings[i].linked_key = i;
    }

    socd_load_settings();
}

void socd_load_settings() {
    // Debug settings
    socd_key_settings[3].resolution_mode = MOST_PRESSED_INPUT_WINS;
    socd_key_settings[3].is_socd_enabled = true;
    socd_key_settings[3].linked_key = 5;

    socd_key_settings[5].resolution_mode = MOST_PRESSED_INPUT_WINS;
    socd_key_settings[5].is_socd_enabled = true;
    socd_key_settings[5].linked_key = 3;
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

        if (settings->resolution_mode != MOST_PRESSED_INPUT_WINS) {
            continue;
        }

        key_state_e key1_state = trigger_get_key_state(key1);
        key_state_e key2_state = trigger_get_key_state(key2);

        if (key1_state == PRESSED && key2_state == PRESSED) {
            handle_most_pressed_wins(key1, key2);
        }
    }
}