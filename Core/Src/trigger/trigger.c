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
static bool trigger_non_tap_hold_press_event = false;
static uint8_t trigger_active_layer_cache = 0xFFu;

#define TRIGGER_DEFERRED_QUEUE_SIZE 32u
_Static_assert((TRIGGER_DEFERRED_QUEUE_SIZE &
                (TRIGGER_DEFERRED_QUEUE_SIZE - 1u)) == 0u,
               "TRIGGER_DEFERRED_QUEUE_SIZE must be a power of two");

typedef enum {
    TRIGGER_DEFERRED_ACTION_NONE = 0,
    TRIGGER_DEFERRED_ACTION_PRESS,
    TRIGGER_DEFERRED_ACTION_RELEASE,
} trigger_deferred_action_type_t;

typedef struct {
    uint8_t type;
    uint8_t key;
    uint16_t keycode;
    uint8_t ticks;
} trigger_deferred_action_t;

typedef enum {
    TRIGGER_DKS_ACTION_HOLD = 0,
    TRIGGER_DKS_ACTION_PRESS = 1,
    TRIGGER_DKS_ACTION_RELEASE = 2,
    TRIGGER_DKS_ACTION_TAP = 3,
} trigger_dks_action_t;

typedef enum {
    TRIGGER_DKS_PHASE_PRESS = 0,
    TRIGGER_DKS_PHASE_BOTTOM_OUT = 1,
    TRIGGER_DKS_PHASE_RELEASE_FROM_BOTTOM_OUT = 2,
    TRIGGER_DKS_PHASE_RELEASE = 3,
} trigger_dks_phase_t;

static trigger_deferred_action_t trigger_deferred_queue[TRIGGER_DEFERRED_QUEUE_SIZE];
static uint8_t trigger_deferred_head = 0u;
static uint8_t trigger_deferred_size = 0u;

static inline bool is_below_actuation_point(int16_t distance, uint16_t actuation_point) {
    return distance >= actuation_point;
}

static inline bool is_above_release_point(int16_t distance, uint16_t release_point) {
    return distance >= release_point;
}

static inline void reset_rapid_trigger_extremums(uint8_t key, int16_t current_distance) {
    key_rapid_trigger_states[key].max_bottom_distance = current_distance;
    key_rapid_trigger_states[key].min_top_distance = current_distance;
}

static uint8_t trigger_deferred_ticks_from_setting(void) {
    uint8_t tick_rate = settings_get_advanced_tick_rate();
    if (tick_rate <= SETTINGS_ADVANCED_TICK_RATE_MIN) {
        return 0u;
    }

    return (uint8_t)(tick_rate - 1u);
}

static void trigger_deferred_clear(void) {
    trigger_deferred_head = 0u;
    trigger_deferred_size = 0u;
}

static bool trigger_deferred_push(uint8_t type, uint8_t key, uint16_t keycode,
                                  uint8_t ticks) {
    uint8_t tail = 0u;

    if (type == (uint8_t)TRIGGER_DEFERRED_ACTION_NONE || keycode == KC_NO) {
        return true;
    }

    if (trigger_deferred_size >= TRIGGER_DEFERRED_QUEUE_SIZE) {
        return false;
    }

    tail = (uint8_t)((trigger_deferred_head + trigger_deferred_size) &
                     (TRIGGER_DEFERRED_QUEUE_SIZE - 1u));
    trigger_deferred_queue[tail].type = type;
    trigger_deferred_queue[tail].key = key;
    trigger_deferred_queue[tail].keycode = keycode;
    trigger_deferred_queue[tail].ticks = ticks;
    trigger_deferred_size++;
    return true;
}

static void trigger_deferred_cancel_key(uint8_t key) {
    trigger_deferred_action_t compacted[TRIGGER_DEFERRED_QUEUE_SIZE] = {0};
    uint8_t kept = 0u;

    for (uint8_t i = 0; i < trigger_deferred_size; i++) {
        uint8_t idx = (uint8_t)((trigger_deferred_head + i) &
                                (TRIGGER_DEFERRED_QUEUE_SIZE - 1u));
        if (trigger_deferred_queue[idx].key == key) {
            continue;
        }
        compacted[kept++] = trigger_deferred_queue[idx];
    }

    memcpy(trigger_deferred_queue, compacted, sizeof(trigger_deferred_queue));
    trigger_deferred_head = 0u;
    trigger_deferred_size = kept;
}

static void trigger_deferred_execute(const trigger_deferred_action_t *action) {
    if (action == NULL || action->keycode == KC_NO) {
        return;
    }

    if (action->type == (uint8_t)TRIGGER_DEFERRED_ACTION_PRESS) {
        layout_press_action_for_key(action->key, action->keycode);
    } else if (action->type == (uint8_t)TRIGGER_DEFERRED_ACTION_RELEASE) {
        layout_release_action_for_key(action->key, action->keycode);
    }
}

static void trigger_process_deferred_actions(void) {
    while (trigger_deferred_size > 0u) {
        trigger_deferred_action_t action =
            trigger_deferred_queue[trigger_deferred_head];

        if (action.ticks > 0u) {
            trigger_deferred_queue[trigger_deferred_head].ticks--;
            break;
        }

        trigger_deferred_head =
            (uint8_t)((trigger_deferred_head + 1u) &
                      (TRIGGER_DEFERRED_QUEUE_SIZE - 1u));
        trigger_deferred_size--;

        trigger_deferred_execute(&action);
    }
}

static void trigger_release_active_action(uint8_t key) {
    if (key_behavior_states[key].active_keycode != KC_NO) {
        layout_release_action_for_key(key, key_behavior_states[key].active_keycode);
        key_behavior_states[key].active_keycode = KC_NO;
    }
}

static uint16_t trigger_resolve_primary_action_keycode(uint8_t key,
                                                       const key_trigger_settings_t *settings) {
    uint16_t keycode = layout_get_active_keycode(key);

    if (keycode == KC_TRANSPARENT) {
        return settings->primary_keycode;
    }

    return keycode;
}

static void trigger_tap_action(uint8_t key, uint16_t keycode) {
    if (keycode == KC_NO) {
        return;
    }

    layout_press_action_for_key(key, keycode);
    (void)trigger_deferred_push((uint8_t)TRIGGER_DEFERRED_ACTION_RELEASE, key,
                                keycode, trigger_deferred_ticks_from_setting());
}

static uint8_t trigger_dks_action_from_bitmap(uint8_t bitmap, uint8_t phase) {
    if (phase > (uint8_t)TRIGGER_DKS_PHASE_RELEASE) {
        return (uint8_t)TRIGGER_DKS_ACTION_HOLD;
    }

    return (uint8_t)((bitmap >> (phase * 2u)) & 0x03u);
}

static bool trigger_dks_is_bottomed_out(const key_trigger_settings_t *settings,
                                        int16_t current_distance) {
    uint16_t threshold_um =
        (uint16_t)settings->dynamic_bottom_out_point_tenths * 100u;

    if (threshold_um == 0u) {
        threshold_um =
            (uint16_t)SETTINGS_DKS_BOTTOM_OUT_POINT_DEFAULT_TENTHS * 100u;
    }

    return current_distance >= (int16_t)threshold_um;
}

static void trigger_dks_process_phase(uint8_t key, uint8_t phase) {
    key_behavior_runtime_t *runtime = &key_behavior_states[key];
    const key_trigger_settings_t *settings = &key_trigger_settings[key];

    for (uint8_t i = 0u; i < SETTINGS_DYNAMIC_ZONE_COUNT; i++) {
        uint16_t keycode = settings->dynamic_zones[i].hid_keycode;
        uint8_t action = trigger_dks_action_from_bitmap(
            settings->dynamic_zones[i].end_mm_tenths, phase);

        if (keycode == KC_NO || action == (uint8_t)TRIGGER_DKS_ACTION_HOLD) {
            continue;
        }

        if (runtime->dks_binding_pressed[i]) {
            layout_release_action_for_key(key, keycode);
            runtime->dks_binding_pressed[i] = false;
        }

        if (action == (uint8_t)TRIGGER_DKS_ACTION_PRESS) {
            if (trigger_deferred_push((uint8_t)TRIGGER_DEFERRED_ACTION_PRESS, key,
                                      keycode,
                                      trigger_deferred_ticks_from_setting())) {
                runtime->dks_binding_pressed[i] = true;
            }
        } else if (action == (uint8_t)TRIGGER_DKS_ACTION_TAP) {
            trigger_tap_action(key, keycode);
        }
    }
}

static void trigger_activate_tap_hold_hold_action(uint8_t key) {
    key_behavior_runtime_t *runtime = &key_behavior_states[key];
    const key_trigger_settings_t *settings = &key_trigger_settings[key];
    uint16_t primary_keycode = KC_NO;

    if (!runtime->tap_hold_pending) {
        return;
    }

    runtime->tap_hold_pending = false;
    runtime->tap_hold_secondary_active = false;
    runtime->tap_hold_uppercase_active = false;

    if (settings->tap_hold_uppercase_hold) {
        primary_keycode = trigger_resolve_primary_action_keycode(key, settings);
        runtime->active_keycode = primary_keycode;
        if (primary_keycode != KC_NO) {
            layout_press_action_for_key(key, KC_LEFT_SHIFT);
            layout_press_action_for_key(key, primary_keycode);
            runtime->tap_hold_uppercase_active = true;
        }
        return;
    }

    runtime->tap_hold_secondary_active = true;
    runtime->active_keycode = settings->secondary_keycode;
    if (runtime->active_keycode != KC_NO) {
        layout_press_action_for_key(key, runtime->active_keycode);
    }
}

static void trigger_apply_hold_on_other_key_press(void) {
    for (uint8_t key = 0u; key < NUM_KEYS; key++) {
        key_behavior_runtime_t *runtime = &key_behavior_states[key];
        const key_trigger_settings_t *settings = &key_trigger_settings[key];

        if (key_states[key] != PRESSED) {
            continue;
        }
        if (settings->behavior_mode != KEY_BEHAVIOR_TAP_HOLD) {
            continue;
        }
        if (!settings->tap_hold_hold_on_other_key_press ||
            !runtime->tap_hold_pending) {
            continue;
        }

        trigger_activate_tap_hold_hold_action(key);
    }
}

static void trigger_behavior_on_press(uint8_t key, int16_t current_distance,
                                      uint32_t now_ms) {
    key_behavior_runtime_t *runtime = &key_behavior_states[key];
    const key_trigger_settings_t *settings = &key_trigger_settings[key];
    bool was_bottomed_out = false;
    bool is_bottomed_out = false;

    runtime->press_start_ms = now_ms;

    switch ((key_behavior_mode_t)settings->behavior_mode) {
    case KEY_BEHAVIOR_TAP_HOLD:
        runtime->tap_hold_pending = true;
        runtime->tap_hold_secondary_active = false;
        runtime->tap_hold_uppercase_active = false;
        break;

    case KEY_BEHAVIOR_TOGGLE:
        runtime->toggle_pending = true;
        runtime->toggle_hold_active = false;
        break;

    case KEY_BEHAVIOR_DYNAMIC: {
        memset(runtime->dks_binding_pressed, 0, sizeof(runtime->dks_binding_pressed));
        was_bottomed_out = runtime->dks_is_bottomed_out;
        is_bottomed_out = trigger_dks_is_bottomed_out(settings, current_distance);
        runtime->dks_is_bottomed_out = is_bottomed_out;

        if (is_bottomed_out && !was_bottomed_out) {
            trigger_dks_process_phase(key, (uint8_t)TRIGGER_DKS_PHASE_BOTTOM_OUT);
        } else {
            trigger_dks_process_phase(key, (uint8_t)TRIGGER_DKS_PHASE_PRESS);
        }
        break;
    }

    case KEY_BEHAVIOR_NORMAL:
    default:
        runtime->active_keycode =
            trigger_resolve_primary_action_keycode(key, settings);
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
    bool is_bottomed_out = false;

    switch ((key_behavior_mode_t)settings->behavior_mode) {
    case KEY_BEHAVIOR_TAP_HOLD:
        if (runtime->tap_hold_pending &&
            elapsed_ms >= settings->hold_threshold_ms) {
            trigger_activate_tap_hold_hold_action(key);
        }
        break;

    case KEY_BEHAVIOR_TOGGLE:
        if (runtime->toggle_pending && !runtime->toggle_latched &&
            elapsed_ms >= settings->hold_threshold_ms) {
            runtime->toggle_pending = false;
            runtime->toggle_hold_active = true;
            runtime->active_keycode =
                trigger_resolve_primary_action_keycode(key, settings);
            if (runtime->active_keycode != KC_NO) {
                layout_press_action_for_key(key, runtime->active_keycode);
            }
        }
        break;

    case KEY_BEHAVIOR_DYNAMIC: {
        is_bottomed_out = trigger_dks_is_bottomed_out(settings, current_distance);

        if (!runtime->dks_is_bottomed_out && is_bottomed_out) {
            runtime->dks_is_bottomed_out = true;
            trigger_dks_process_phase(key, (uint8_t)TRIGGER_DKS_PHASE_BOTTOM_OUT);
        } else if (runtime->dks_is_bottomed_out && !is_bottomed_out) {
            runtime->dks_is_bottomed_out = false;
            trigger_dks_process_phase(
                key, (uint8_t)TRIGGER_DKS_PHASE_RELEASE_FROM_BOTTOM_OUT);
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
        if (runtime->tap_hold_uppercase_active) {
            trigger_release_active_action(key);
            layout_release_action_for_key(key, KC_LEFT_SHIFT);
        } else if (runtime->tap_hold_secondary_active) {
            trigger_release_active_action(key);
        } else if (runtime->tap_hold_pending) {
            trigger_tap_action(key,
                               trigger_resolve_primary_action_keycode(key, settings));
        }
        runtime->tap_hold_pending = false;
        runtime->tap_hold_secondary_active = false;
        runtime->tap_hold_uppercase_active = false;
        break;

    case KEY_BEHAVIOR_TOGGLE:
        if (runtime->toggle_hold_active) {
            trigger_release_active_action(key);
        } else if (runtime->toggle_pending) {
            uint16_t primary_keycode =
                trigger_resolve_primary_action_keycode(key, settings);
            if (runtime->toggle_latched) {
                layout_release_action_for_key(key, primary_keycode);
                runtime->toggle_latched = false;
                runtime->active_keycode = KC_NO;
            } else {
                layout_press_action_for_key(key, primary_keycode);
                runtime->toggle_latched = true;
                runtime->active_keycode = primary_keycode;
            }
        }
        runtime->toggle_pending = false;
        runtime->toggle_hold_active = false;
        break;

    case KEY_BEHAVIOR_DYNAMIC:
        trigger_deferred_cancel_key(key);
        trigger_dks_process_phase(key, (uint8_t)TRIGGER_DKS_PHASE_RELEASE);
        /* Force-release any bindings whose RELEASE-phase action was HOLD.
         * Without this, a binding configured to "hold" through the release
         * phase would stay pressed forever after the physical key is released. */
        for (uint8_t i = 0u; i < SETTINGS_DYNAMIC_ZONE_COUNT; i++) {
            if (runtime->dks_binding_pressed[i]) {
                layout_release_action_for_key(key,
                    settings->dynamic_zones[i].hid_keycode);
                runtime->dks_binding_pressed[i] = false;
            }
        }
        runtime->dks_is_bottomed_out = false;
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
    trigger_deferred_clear();

    for (uint8_t key = 0; key < NUM_KEYS; key++) {
        int16_t current_distance = analog_read_distance_value(key);

        key_states[key] = RELEASED;
        key_rapid_trigger_states[key].last_distance = current_distance;
        key_rapid_trigger_states[key].continuous_armed = false;
        reset_rapid_trigger_extremums(key, current_distance);
        memset(&key_behavior_states[key], 0, sizeof(key_behavior_states[key]));
        key_behavior_states[key].active_keycode = KC_NO;
    }

    trigger_non_tap_hold_press_event = false;
}

static inline void press_key(uint8_t key, int16_t current_distance,
                             uint32_t now_ms) {
    if (key_states[key] == RELEASED) {
        key_states[key] = PRESSED;
        trigger_behavior_on_press(key, current_distance, now_ms);
        if (key_trigger_settings[key].behavior_mode != KEY_BEHAVIOR_TAP_HOLD) {
            trigger_non_tap_hold_press_event = true;
        }
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
    uint16_t release_point = settings->release_point;
    key_rapid_trigger_data_t *rt_data = &key_rapid_trigger_states[key];

    if (settings->behavior_mode == KEY_BEHAVIOR_DYNAMIC) {
        if (key_states[key] == RELEASED) {
            if (is_below_actuation_point(current_distance, actuation_point)) {
                press_key(key, current_distance, now_ms);
            }
        } else {
            if (!is_above_release_point(current_distance, release_point)) {
                release_key(key);
            }
        }
    } else if (!settings->is_rapid_trigger_enabled) {
        if (key_states[key] == RELEASED) {
            if (is_below_actuation_point(current_distance, actuation_point)) {
                press_key(key, current_distance, now_ms);
            }
        } else {
            if (!is_above_release_point(current_distance, release_point)) {
                release_key(key);
            }
        }
    } else if (!settings->continuous_rapid_trigger) {
        if (key_states[key] == PRESSED &&
            !is_above_release_point(current_distance, release_point)) {
            release_key(key);
            reset_rapid_trigger_extremums(key, current_distance);
            rt_data->last_distance = current_distance;
        } else if (key_states[key] == RELEASED &&
                   !is_below_actuation_point(current_distance, actuation_point)) {
            rt_data->last_distance = current_distance;
            if (current_distance < rt_data->min_top_distance) {
                rt_data->min_top_distance = current_distance;
            }
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

            rt_data->continuous_armed = true;
        }

        if (key_states[key] == PRESSED &&
            !is_above_release_point(current_distance, release_point)) {
            release_key(key);
            reset_rapid_trigger_extremums(key, current_distance);
            rt_data->last_distance = current_distance;
        } else {
            handle_rapid_trigger(key, current_distance, settings, now_ms);
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
    runtime->actuation_point = mm_tenths_to_um(settings->actuation_point_mm);
    runtime->release_point = mm_tenths_to_um(settings->release_point_mm);
    if (runtime->release_point > runtime->actuation_point) {
        runtime->release_point = runtime->actuation_point;
    }
    runtime->rapid_trigger_press_sensitivity =
        mm_hundredths_to_um(settings->rapid_trigger_press);
    runtime->rapid_trigger_release_sensitivity =
        mm_hundredths_to_um(settings->rapid_trigger_release);
    runtime->behavior_mode = (key_behavior_mode_t)settings->advanced.behavior_mode;
    runtime->hold_threshold_ms =
        (uint16_t)settings->advanced.hold_threshold_10ms * 10u;
    runtime->secondary_keycode = settings->advanced.secondary_hid_keycode;
    runtime->tap_hold_hold_on_other_key_press =
        settings_key_is_tap_hold_hold_on_other_key_press(settings);
    runtime->tap_hold_uppercase_hold =
        settings_key_is_tap_hold_uppercase_hold(settings);
    runtime->dynamic_bottom_out_point_tenths = settings->advanced.dks_bottom_out_point_tenths;
    memcpy(runtime->dynamic_zones, settings->advanced.dynamic_zones,
           sizeof(runtime->dynamic_zones));
}

static uint8_t trigger_runtime_active_layer(void) {
    uint8_t layer = layout_get_active_layer_top();
    if (layer >= SETTINGS_LAYER_COUNT) {
        return 0u;
    }

    return layer;
}

static void trigger_reload_settings_for_layer(uint8_t layer) {
    settings_key_t settings = {0};

    for (uint8_t i = 0; i < NUM_KEYS; i++) {
        if (settings_get_key_for_layer(i, layer, &settings)) {
            trigger_apply_key_settings(i, &settings);
        }
    }

    socd_load_settings();
}

void trigger_reload_settings(void) {
    trigger_active_layer_cache = trigger_runtime_active_layer();
    trigger_reload_settings_for_layer(trigger_active_layer_cache);
}

static void trigger_refresh_layer_runtime_settings(void) {
    uint8_t layer = trigger_runtime_active_layer();
    if (layer == trigger_active_layer_cache) {
        return;
    }

    trigger_active_layer_cache = layer;
    trigger_reload_settings_for_layer(layer);
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
    trigger_deferred_clear();
    trigger_active_layer_cache = 0xFFu;

    for (int i = 0; i < NUM_KEYS; i++) {
        key_trigger_settings[i].primary_keycode = KC_NO;
        key_trigger_settings[i].actuation_point = DEFAULT_ACTUATION_POINT;
        key_trigger_settings[i].release_point = DEFAULT_ACTUATION_POINT;
        key_trigger_settings[i].is_rapid_trigger_enabled = false;
        key_trigger_settings[i].continuous_rapid_trigger = false;
        key_trigger_settings[i].rapid_trigger_press_sensitivity = DEFAULT_RAPID_TRIGGER_SENSITIVITY;
        key_trigger_settings[i].rapid_trigger_release_sensitivity = DEFAULT_RAPID_TRIGGER_SENSITIVITY;
        key_trigger_settings[i].behavior_mode = KEY_BEHAVIOR_NORMAL;
        key_trigger_settings[i].hold_threshold_ms = 200u;
        key_trigger_settings[i].secondary_keycode = KC_NO;
        key_trigger_settings[i].tap_hold_hold_on_other_key_press = false;
        key_trigger_settings[i].tap_hold_uppercase_hold = false;
        key_trigger_settings[i].dynamic_bottom_out_point_tenths =
            SETTINGS_DKS_BOTTOM_OUT_POINT_DEFAULT_TENTHS;
        memset(key_trigger_settings[i].dynamic_zones, 0,
               sizeof(key_trigger_settings[i].dynamic_zones));
        key_trigger_settings[i].dynamic_zones[0].end_mm_tenths = 0x81u;

        // Initialize rapid trigger states
        key_rapid_trigger_states[i].last_distance = 0;
        key_rapid_trigger_states[i].max_bottom_distance = 0;
        key_rapid_trigger_states[i].min_top_distance = 0;
        key_rapid_trigger_states[i].continuous_armed = false;

        memset(&key_behavior_states[i], 0, sizeof(key_behavior_states[i]));
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

    trigger_refresh_layer_runtime_settings();

    trigger_process_deferred_actions();

    for (uint8_t key = 0; key < NUM_KEYS; key++) {
        handle_trigger(key, now_ms);
    }

    if (trigger_non_tap_hold_press_event) {
        trigger_apply_hold_on_other_key_press();
        trigger_non_tap_hold_press_event = false;
    }
}
