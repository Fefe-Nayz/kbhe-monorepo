#include <stdbool.h>
#include <stdint.h>
#include "trigger/trigger.h"
#include "analog/calibration.h"
#include "board_config.h"
#include "analog/analog.h"
#include "hid/keyboard_hid.h"
#include "hid/keyboard_nkro_hid.h"
#include "hid/mouse_hid.h"
#include "led_matrix.h"
#include "layout/keycodes.h"
#include "trigger/socd.h"
#include "layout/layout.h"
#include <string.h>


static key_trigger_settings_t key_trigger_settings[NUM_KEYS];

static key_rapid_trigger_data_t key_rapid_trigger_states[NUM_KEYS];
static key_behavior_runtime_t key_behavior_states[NUM_KEYS];

static key_state_e key_states[NUM_KEYS];
static bool keyboard_blocked_for_calibration = false;

static inline bool is_below_actuation_point(int16_t distance, uint16_t actuation_point) {
    return distance >= actuation_point;
}

static inline void reset_rapid_trigger_extremums(uint8_t key, int16_t current_distance) {
    key_rapid_trigger_states[key].max_bottom_distance = current_distance;
    key_rapid_trigger_states[key].min_top_distance = current_distance;
}

static void trigger_release_pending_actions(void) {
    for (uint8_t key = 0; key < NUM_KEYS; key++) {
        if (key_behavior_states[key].pending_release_keycode != KC_NO) {
            layout_release_action_for_key(key,
                                          key_behavior_states[key].pending_release_keycode);
            key_behavior_states[key].pending_release_keycode = KC_NO;
        }
    }
}

static void trigger_release_active_action(uint8_t key) {
    if (key_behavior_states[key].active_keycode != KC_NO) {
        layout_release_action_for_key(key, key_behavior_states[key].active_keycode);
        key_behavior_states[key].active_keycode = KC_NO;
    }
}

static void trigger_tap_action(uint8_t key, uint16_t keycode) {
    if (keycode == KC_NO) {
        return;
    }

    layout_press_action_for_key(key, keycode);
    key_behavior_states[key].pending_release_keycode = keycode;
}

static uint8_t trigger_dynamic_zone_for_distance(
    const key_trigger_settings_t *settings, int16_t current_distance) {
    uint8_t zone_count = settings->dynamic_zone_count;

    if (zone_count == 0u || zone_count > SETTINGS_DYNAMIC_ZONE_COUNT) {
        zone_count = 1u;
    }

    for (uint8_t i = 0u; i < zone_count; i++) {
        uint16_t end_um =
            (uint16_t)settings->dynamic_zones[i].end_mm_tenths * 100u;
        if ((uint16_t)current_distance <= end_um) {
            return i;
        }
    }

    return (uint8_t)(zone_count - 1u);
}

static void trigger_behavior_on_press(uint8_t key, int16_t current_distance,
                                      uint32_t now_ms) {
    key_behavior_runtime_t *runtime = &key_behavior_states[key];
    const key_trigger_settings_t *settings = &key_trigger_settings[key];

    runtime->press_start_ms = now_ms;

    switch ((key_behavior_mode_t)settings->behavior_mode) {
    case KEY_BEHAVIOR_TAP_HOLD:
        runtime->tap_hold_pending = true;
        runtime->tap_hold_secondary_active = false;
        break;

    case KEY_BEHAVIOR_TOGGLE:
        runtime->toggle_pending = true;
        runtime->toggle_hold_active = false;
        break;

    case KEY_BEHAVIOR_DYNAMIC: {
        uint8_t zone =
            trigger_dynamic_zone_for_distance(settings, current_distance);
        runtime->active_dynamic_zone = zone;
        runtime->active_keycode = settings->dynamic_zones[zone].hid_keycode;
        if (runtime->active_keycode != KC_NO) {
            layout_press_action_for_key(key, runtime->active_keycode);
        }
        break;
    }

    case KEY_BEHAVIOR_NORMAL:
    default:
        runtime->active_keycode = settings->primary_keycode;
        if (runtime->active_keycode != KC_NO) {
            layout_press_action_for_key(key, runtime->active_keycode);
        }
        break;
    }
}

static void trigger_behavior_on_update(uint8_t key, int16_t current_distance,
                                       uint32_t now_ms) {
    key_behavior_runtime_t *runtime = &key_behavior_states[key];
    const key_trigger_settings_t *settings = &key_trigger_settings[key];
    uint32_t elapsed_ms = now_ms - runtime->press_start_ms;

    switch ((key_behavior_mode_t)settings->behavior_mode) {
    case KEY_BEHAVIOR_TAP_HOLD:
        if (runtime->tap_hold_pending &&
            elapsed_ms >= settings->hold_threshold_ms) {
            runtime->tap_hold_pending = false;
            runtime->tap_hold_secondary_active = true;
            runtime->active_keycode = settings->secondary_keycode;
            if (runtime->active_keycode != KC_NO) {
                layout_press_action_for_key(key, runtime->active_keycode);
            }
        }
        break;

    case KEY_BEHAVIOR_TOGGLE:
        if (runtime->toggle_pending && !runtime->toggle_latched &&
            elapsed_ms >= settings->hold_threshold_ms) {
            runtime->toggle_pending = false;
            runtime->toggle_hold_active = true;
            runtime->active_keycode = settings->primary_keycode;
            if (runtime->active_keycode != KC_NO) {
                layout_press_action_for_key(key, runtime->active_keycode);
            }
        }
        break;

    case KEY_BEHAVIOR_DYNAMIC: {
        uint8_t zone =
            trigger_dynamic_zone_for_distance(settings, current_distance);
        if (zone != runtime->active_dynamic_zone) {
            trigger_release_active_action(key);
            runtime->active_dynamic_zone = zone;
            runtime->active_keycode = settings->dynamic_zones[zone].hid_keycode;
            if (runtime->active_keycode != KC_NO) {
                layout_press_action_for_key(key, runtime->active_keycode);
            }
        }
        break;
    }

    case KEY_BEHAVIOR_NORMAL:
    default:
        break;
    }
}

static void trigger_behavior_on_release(uint8_t key) {
    key_behavior_runtime_t *runtime = &key_behavior_states[key];
    const key_trigger_settings_t *settings = &key_trigger_settings[key];

    switch ((key_behavior_mode_t)settings->behavior_mode) {
    case KEY_BEHAVIOR_TAP_HOLD:
        if (runtime->tap_hold_secondary_active) {
            trigger_release_active_action(key);
        } else if (runtime->tap_hold_pending) {
            trigger_tap_action(key, settings->primary_keycode);
        }
        runtime->tap_hold_pending = false;
        runtime->tap_hold_secondary_active = false;
        break;

    case KEY_BEHAVIOR_TOGGLE:
        if (runtime->toggle_hold_active) {
            trigger_release_active_action(key);
        } else if (runtime->toggle_pending) {
            if (runtime->toggle_latched) {
                layout_release_action_for_key(key, settings->primary_keycode);
                runtime->toggle_latched = false;
                runtime->active_keycode = KC_NO;
            } else {
                layout_press_action_for_key(key, settings->primary_keycode);
                runtime->toggle_latched = true;
                runtime->active_keycode = settings->primary_keycode;
            }
        }
        runtime->toggle_pending = false;
        runtime->toggle_hold_active = false;
        break;

    case KEY_BEHAVIOR_DYNAMIC:
        trigger_release_active_action(key);
        runtime->active_dynamic_zone = 0u;
        break;

    case KEY_BEHAVIOR_NORMAL:
    default:
        trigger_release_active_action(key);
        break;
    }
}

static void trigger_reset_runtime_state(bool release_keyboard_reports) {
    if (release_keyboard_reports) {
        keyboard_hid_release_all();
        keyboard_hid_reset_state();
        keyboard_nkro_hid_release_all();
        mouse_hid_release_all();
    }

    layout_reset_state();
    socd_load_settings();

    for (uint8_t key = 0; key < NUM_KEYS; key++) {
        int16_t current_distance = analog_read_distance_value(key);

        key_states[key] = RELEASED;
        key_rapid_trigger_states[key].last_distance = current_distance;
        key_rapid_trigger_states[key].continuous_armed = false;
        reset_rapid_trigger_extremums(key, current_distance);
        memset(&key_behavior_states[key], 0, sizeof(key_behavior_states[key]));
        key_behavior_states[key].pending_release_keycode = KC_NO;
        key_behavior_states[key].active_keycode = KC_NO;
    }
}

static inline void press_key(uint8_t key, int16_t current_distance,
                             uint32_t now_ms) {
    if (key_states[key] == RELEASED) {
        key_states[key] = PRESSED;
        trigger_behavior_on_press(key, current_distance, now_ms);
        led_matrix_key_event(key, true);
        socd_on_press(key);
    }
}

static inline void release_key(uint8_t key) {
    if (key_states[key] == PRESSED) {
        trigger_behavior_on_release(key);
        led_matrix_key_event(key, false);
        key_states[key] = RELEASED;
        socd_on_release(key);
    }
}

static inline void handle_rapid_trigger(uint8_t key, int16_t current_distance,
                                        const key_trigger_settings_t *settings,
                                        uint32_t now_ms) {
    key_rapid_trigger_data_t *rt_data = &key_rapid_trigger_states[key];

    int16_t delta = current_distance - rt_data->last_distance;

    if (delta == 0) {
        rt_data->last_distance = current_distance;
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
                press_key(key, current_distance, now_ms);
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

static inline void handle_trigger(uint8_t key, uint32_t now_ms) {
    int16_t current_distance = analog_read_distance_value(key);
    const key_trigger_settings_t *settings = &key_trigger_settings[key];
    uint16_t actuation_point = settings->actuation_point;
    key_rapid_trigger_data_t *rt_data = &key_rapid_trigger_states[key];

    if (!settings->is_rapid_trigger_enabled) {
        if (!is_below_actuation_point(current_distance, actuation_point)) {
            release_key(key);
        } else {
            press_key(key, current_distance, now_ms);
        }
    } else if (!settings->continuous_rapid_trigger) {
        if (!is_below_actuation_point(current_distance, actuation_point)) {
            release_key(key);
        } else {
            handle_rapid_trigger(key, current_distance, settings, now_ms);
        }
    } else {
        if (!rt_data->continuous_armed) {
            if (!is_below_actuation_point(current_distance, actuation_point)) {
                rt_data->last_distance = current_distance;
                if (current_distance < rt_data->min_top_distance) {
                    rt_data->min_top_distance = current_distance;
                }
                return;
            }
        }

        handle_rapid_trigger(key, current_distance, settings, now_ms);
        if (key_states[key] == PRESSED) {
            rt_data->continuous_armed = true;
        }
        if (current_distance <= 0) {
            if (key_states[key] == PRESSED) {
                release_key(key);
            }
            if (key_states[key] == RELEASED) {
                rt_data->continuous_armed = false;
                reset_rapid_trigger_extremums(key, current_distance);
                rt_data->last_distance = current_distance;
            }
        }
    }

    if (key_states[key] == PRESSED) {
        trigger_behavior_on_update(key, current_distance, now_ms);
    }
}

inline key_state_e trigger_get_key_state(uint8_t key) {
    if (key >= NUM_KEYS) {
        return RELEASED;
    }
    return key_states[key];
}

static uint16_t mm_tenths_to_um(uint8_t value) {
    return (uint16_t)value * 100u;
}

static uint16_t mm_hundredths_to_um(uint8_t value) {
    return (uint16_t)value * 10u;
}

void trigger_apply_key_settings(uint8_t key, const settings_key_t *settings) {
    if (key >= NUM_KEYS || settings == NULL) {
        return;
    }

    key_trigger_settings_t *runtime = &key_trigger_settings[key];
    runtime->primary_keycode = settings->hid_keycode;
    runtime->is_rapid_trigger_enabled = settings->rapid_trigger_enabled ? true : false;
    runtime->continuous_rapid_trigger =
        settings_key_is_continuous_rapid_trigger_enabled(settings);
    runtime->actuation_point = runtime->is_rapid_trigger_enabled
                                   ? mm_tenths_to_um(settings->rapid_trigger_activation)
                                   : mm_tenths_to_um(settings->actuation_point_mm);
    runtime->use_rapid_trigger_press_sensitivity =
        settings->rapid_trigger_press != settings->rapid_trigger_release;
    runtime->rapid_trigger_press_sensitivity =
        mm_hundredths_to_um(settings->rapid_trigger_press);
    runtime->rapid_trigger_release_sensitivity =
        mm_hundredths_to_um(settings->rapid_trigger_release);
    runtime->behavior_mode = (key_behavior_mode_t)settings->advanced.behavior_mode;
    runtime->hold_threshold_ms =
        (uint16_t)settings->advanced.hold_threshold_10ms * 10u;
    runtime->secondary_keycode = settings->advanced.secondary_hid_keycode;
    runtime->dynamic_zone_count = settings->advanced.dynamic_zone_count;
    memcpy(runtime->dynamic_zones, settings->advanced.dynamic_zones,
           sizeof(runtime->dynamic_zones));
}

void trigger_reload_settings(void) {
    for (uint8_t i = 0; i < NUM_KEYS; i++) {
        const settings_key_t *settings = settings_get_key(i);
        if (settings != NULL) {
            trigger_apply_key_settings(i, settings);
        }
    }

    socd_load_settings();
}

uint16_t trigger_get_distance_01mm(uint8_t key) {
    if (key >= NUM_KEYS) {
        return 0;
    }

    int16_t um = analog_read_distance_value(key);
    if (um < 0) {
        um = 0;
    }

    return (uint16_t)((um + 5) / 10);
}

void trigger_init() {
    for (int i = 0; i < NUM_KEYS; i++) {
        key_trigger_settings[i].primary_keycode = KC_NO;
        key_trigger_settings[i].actuation_point = DEFAULT_ACTUATION_POINT;
        key_trigger_settings[i].is_rapid_trigger_enabled = false;
        key_trigger_settings[i].use_rapid_trigger_press_sensitivity = false;
        key_trigger_settings[i].continuous_rapid_trigger = false;
        key_trigger_settings[i].rapid_trigger_press_sensitivity = DEFAULT_RAPID_TRIGGER_SENSITIVITY;
        key_trigger_settings[i].rapid_trigger_release_sensitivity = DEFAULT_RAPID_TRIGGER_SENSITIVITY;
        key_trigger_settings[i].behavior_mode = KEY_BEHAVIOR_NORMAL;
        key_trigger_settings[i].hold_threshold_ms = 200u;
        key_trigger_settings[i].secondary_keycode = KC_NO;
        key_trigger_settings[i].dynamic_zone_count = 1u;
        memset(key_trigger_settings[i].dynamic_zones, 0,
               sizeof(key_trigger_settings[i].dynamic_zones));
        key_trigger_settings[i].dynamic_zones[0].end_mm_tenths = 40u;

        // Initialize rapid trigger states
        key_rapid_trigger_states[i].last_distance = 0;
        key_rapid_trigger_states[i].max_bottom_distance = 0;
        key_rapid_trigger_states[i].min_top_distance = 0;
        key_rapid_trigger_states[i].continuous_armed = false;

        memset(&key_behavior_states[i], 0, sizeof(key_behavior_states[i]));
        key_behavior_states[i].pending_release_keycode = KC_NO;
        key_behavior_states[i].active_keycode = KC_NO;

        // Initialize key states
        key_states[i] = RELEASED;
    }
}

void trigger_task() {
    uint32_t now_ms = HAL_GetTick();

    if (calibration_guided_is_active()) {
        if (!keyboard_blocked_for_calibration) {
            trigger_reset_runtime_state(true);
            keyboard_blocked_for_calibration = true;
        }
        return;
    }

    if (keyboard_blocked_for_calibration) {
        trigger_reset_runtime_state(false);
        keyboard_blocked_for_calibration = false;
    }

    trigger_release_pending_actions();

    for (uint8_t key = 0; key < NUM_KEYS; key++) {
        handle_trigger(key, now_ms);
    }
}
