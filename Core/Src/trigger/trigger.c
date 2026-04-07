#include <stdbool.h>
#include <stdint.h>
#include "trigger/trigger.h"
#include "board_config.h"
#include "analog/analog.h"
#include "hid/keyboard_hid.h"
#include "layout/keycodes.h"

static key_trigger_settings_t key_trigger_settings[NUM_KEYS];

static key_rapid_trigger_data_t key_rapid_trigger_states[NUM_KEYS];

static key_state_e key_states[NUM_KEYS];

static inline bool is_below_actuation_point(int16_t distance, uint16_t actuation_point) {
    return distance >= actuation_point;
}

static inline void reset_rapid_trigger_extremums(uint8_t key, int16_t current_distance) {
    key_rapid_trigger_states[key].max_bottom_distance = current_distance;
    key_rapid_trigger_states[key].min_top_distance = current_distance;
}

static inline void press_key(uint8_t key) {
    if (key_states[key] == RELEASED) {
        keyboard_hid_key_press(KC_A + key); // Example: map key index to HID code (KC_A, KC_B, etc.)
        key_states[key] = PRESSED;
        return;
    }
}

static inline void release_key(uint8_t key) {
    if (key_states[key] == PRESSED) {
        keyboard_hid_key_release(KC_A + key);
        key_states[key] = RELEASED;
    }
}

static inline void handle_rapid_trigger(uint8_t key, int16_t current_distance, const key_trigger_settings_t *settings) {
    key_rapid_trigger_data_t *rt_data = &key_rapid_trigger_states[key];

    int16_t delta = current_distance - rt_data->last_distance;

    if (delta == 0) {
        return;
    }

    key_state_e state = key_states[key];

    if (delta > 0) {
        if (state == PRESSED) {
            if (current_distance > rt_data->max_bottom_distance) {
                rt_data->max_bottom_distance = current_distance;
            }
        } else {
            int16_t distance_from_min_top = current_distance - rt_data->min_top_distance;
            int16_t press_sensitivity = (int16_t)settings->rapid_trigger_press_sensitivity;

            if (distance_from_min_top >= press_sensitivity) {
                press_key(key);
                reset_rapid_trigger_extremums(key, current_distance);
            }
        }
    } else {
        if (state == RELEASED) {
            if (current_distance < rt_data->min_top_distance) {
                rt_data->min_top_distance = current_distance;
            }
        } else {
            int16_t release_sensitivity = (int16_t)settings->rapid_trigger_release_sensitivity;
            int16_t distance_from_max_bottom = rt_data->max_bottom_distance - current_distance;

            if (distance_from_max_bottom >= release_sensitivity) {
                release_key(key);
                reset_rapid_trigger_extremums(key, current_distance);
            }
        }
    }

    rt_data->last_distance = current_distance;
}

static inline void handle_trigger(uint8_t key) {
    // Get current distance
    int16_t current_distance = analog_read_distance_value(key);

    // Get trigger settings
    const key_trigger_settings_t *settings = &key_trigger_settings[key];

    // Get actuation point
    uint16_t actuation_point = settings->actuation_point;

    // Above actuation point
    if (!is_below_actuation_point(current_distance, actuation_point)) {
        // Release the key
        release_key(key);
        return;
    }

    // Below actuation point
    bool is_rapid_trigger_enabled = settings->is_rapid_trigger_enabled;

    // If rapid trigger is disabled
    if (!is_rapid_trigger_enabled) {
        // Press the key
        press_key(key);
        return;
    }
    
    // Handle rapid trigger logic
    handle_rapid_trigger(key, current_distance, settings);
}

void trigger_init() {
    for (int i = 0; i < NUM_KEYS; i++) {
        // key_trigger_settings[i].actuation_point = DEFAULT_ACTUATION_POINT;
        // key_trigger_settings[i].is_rapid_trigger_enabled = false;
        
        key_trigger_settings[i].actuation_point = 200;
        key_trigger_settings[i].is_rapid_trigger_enabled = true;

        key_trigger_settings[i].use_rapid_trigger_press_sensitivity = false;
        key_trigger_settings[i].rapid_trigger_press_sensitivity = DEFAULT_RAPID_TRIGGER_SENSITIVITY;
        key_trigger_settings[i].rapid_trigger_release_sensitivity = DEFAULT_RAPID_TRIGGER_SENSITIVITY;

        // Initialize rapid trigger states
        key_rapid_trigger_states[i].last_distance = 0;
        key_rapid_trigger_states[i].max_bottom_distance = 0;
        key_rapid_trigger_states[i].min_top_distance = 0;

        // Initialize key states
        key_states[i] = RELEASED;
    }
}

void trigger_task() {
    for (uint8_t key = 0; key < NUM_KEYS; key++) {
        // Handle trigger logic
        handle_trigger(key);
    }
}