#include <stdbool.h>
#include <stdint.h>
#include "trigger/trigger.h"
#include "board_config.h"
#include "analog/analog.h"
#include "usb_hid.h"
#include "layout/keycodes.h"

static key_trigger_settings_t key_trigger_settings[NUM_KEYS];

static key_rapid_trigger_data_t key_rapid_trigger_states[NUM_KEYS];

static key_state_e key_states[NUM_KEYS];

bool is_below_actuation_point(int16_t distance, uint16_t actuation_point) {
    return distance >= actuation_point;
}

uint8_t get_key_direction(int16_t current_distance, int16_t last_distance) {
    if (current_distance > last_distance) {
        return KEY_DIRECTION_DOWN;
    } else if (current_distance < last_distance) {
        return KEY_DIRECTION_UP;
    } else {
        return KEY_DIRECTION_NONE;
    }
}

void reset_rapid_trigger_extremums(uint8_t key, int16_t current_distance) {
    key_rapid_trigger_states[key].max_bottom_distance = current_distance;
    key_rapid_trigger_states[key].min_top_distance = current_distance;
}

void press_key(uint8_t key) {
    if (key_states[key] == RELEASED) {
        usb_hid_key_press(KC_A + key); // Example: map key index to HID code (KC_A, KC_B, etc.)
        key_states[key] = PRESSED;
        return;
    }
}

void release_key(uint8_t key) {
    if (key_states[key] == PRESSED) {
        usb_hid_key_release(KC_A + key);
        key_states[key] = RELEASED;
    }
}

void handle_rapid_trigger(uint8_t key, int16_t current_distance, key_trigger_settings_t *settings) {
    // Get rapid trigger data
    key_rapid_trigger_data_t *rt_data = &key_rapid_trigger_states[key];
    
    // Get distances
    int16_t max_bottom_distance = rt_data->max_bottom_distance;
    int16_t min_top_distance = rt_data->min_top_distance;
    int16_t last_distance = rt_data->last_distance;
    
    // Get key state and direction
    key_state_e state = key_states[key];
    key_direction_e key_direction = get_key_direction(current_distance, last_distance);
    
    switch (key_direction) {
        // Key is moving down
        case KEY_DIRECTION_DOWN:
            
            if (state == PRESSED) {
                // If pressed and going down, update max bottom distance
                if (current_distance > max_bottom_distance) {
                    rt_data->max_bottom_distance = current_distance;
                }
            } else {
                // If released and going down, check for rapid press trigger
                int16_t distance_from_min_top = current_distance - min_top_distance;
                int16_t press_sensitivity = settings->rapid_trigger_press_sensitivity;

                if (distance_from_min_top >= press_sensitivity) {
                    // Trigger rapid press
                    press_key(key);

                    // Reset extremums
                    reset_rapid_trigger_extremums(key, current_distance);
                }
            }
        
        break;
        
        // Key is moving up
        case KEY_DIRECTION_UP:
            
            if (state == RELEASED) {
                // If released and going up, update min top distance
                if (current_distance < min_top_distance) {
                    rt_data->min_top_distance = current_distance;
                }
            } else {
                // If pressed and going up, check for rapid release trigger
                int16_t release_sensitivity = settings->rapid_trigger_release_sensitivity;
                int16_t distance_from_max_bottom = max_bottom_distance - current_distance;

                if (distance_from_max_bottom >= release_sensitivity) {
                    // Trigger rapid release
                    release_key(key);

                    // Reset extremums
                    reset_rapid_trigger_extremums(key, current_distance);
                }
            }
            
            break;

        // No movement
        case KEY_DIRECTION_NONE:
            return;
    }
    
    // Update last distance
    rt_data->last_distance = current_distance;
}

void handle_trigger(uint8_t key) {
    // Get current distance
    int16_t current_distance = analog_read_distance_value(key);

    // Get trigger settings
    key_trigger_settings_t *settings = &key_trigger_settings[key];

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
    // Initialize key trigger settings with defaults
    for (int i = 0; i < NUM_KEYS; i++) {
        // key_trigger_settings[i].actuation_point = DEFAULT_ACTUATION_POINT;
        key_trigger_settings[i].is_rapid_trigger_enabled = false;
        
        key_trigger_settings[i].actuation_point = 200;
        key_trigger_settings[i].is_rapid_trigger_enabled = true;

        key_trigger_settings[i].use_rapid_trigger_press_sensitivity = false;
        key_trigger_settings[i].rapid_trigger_press_sensitivity = DEFAULT_RAPID_TRIGGER_SENSITIVITY;
        key_trigger_settings[i].rapid_trigger_release_sensitivity = DEFAULT_RAPID_TRIGGER_SENSITIVITY;
    }

    // Initialize rapid trigger states
    for (int i = 0; i < NUM_KEYS; i++) {
        key_rapid_trigger_states[i].last_distance = 0;
        key_rapid_trigger_states[i].max_bottom_distance = 0;
        key_rapid_trigger_states[i].min_top_distance = 0;
    }

    // Initialize key states
    for (int i = 0; i < NUM_KEYS; i++) {
        key_states[i] = RELEASED;
    }
}

void trigger_task() {
    for (int key = 0; key < NUM_KEYS; key++) {
        // Handle trigger logic
        handle_trigger(key);
    }
}