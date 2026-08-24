#include <stdbool.h>
#include <stdint.h>
#include "action_engine.h"
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
#include "trigger/transition_filter.h"
#include "layout/layout.h"
#include <string.h>


static key_trigger_settings_t key_trigger_settings[NUM_KEYS];
/* Settings are frozen when the physical press is accepted.  Layer/profile
 * changes while a key is held must never change how that press is updated or
 * released. */
static key_trigger_settings_t key_pressed_settings[NUM_KEYS];

static key_rapid_trigger_data_t key_rapid_trigger_states[NUM_KEYS];
static key_behavior_runtime_t key_behavior_states[NUM_KEYS];
typedef trigger_transition_filter_t key_transition_guard_t;
static key_transition_guard_t key_transition_guards[NUM_KEYS];

static key_state_e key_states[NUM_KEYS];
static bool keyboard_blocked_for_calibration = false;
static uint8_t trigger_active_layer_cache = 0xFFu;
static uint32_t trigger_last_input_transition_ms = 0u;
static uint8_t trigger_chatter_guard_enabled =
    (uint8_t)(SETTINGS_DEFAULT_TRIGGER_CHATTER_GUARD_ENABLED ? 1u : 0u);
static uint8_t trigger_chatter_guard_duration_ms =
    (uint8_t)SETTINGS_DEFAULT_TRIGGER_CHATTER_GUARD_MS;

#define TRIGGER_DEFERRED_QUEUE_SIZE 64u
#define TRIGGER_DKS_BOTTOM_HYSTERESIS_UM 100u
#define TRIGGER_DKS_STABILITY_MIN_MS 1u
#define TRIGGER_CONTINUOUS_RT_REST_BAND_UM 100u

typedef enum {
    TRIGGER_DEFERRED_ACTION_NONE = 0,
    TRIGGER_DEFERRED_ACTION_PRESS,
    TRIGGER_DEFERRED_ACTION_RELEASE,
} trigger_deferred_action_type_t;

typedef struct {
    uint8_t type;
    uint8_t key;
    uint8_t binding;
    uint8_t flags;
    uint16_t keycode;
    uint16_t generation;
    uint32_t due_tick;
    bool used;
} trigger_deferred_action_t;

#define TRIGGER_DEFERRED_FLAG_COMPENSATING_RELEASE 0x01u
#define TRIGGER_DEFERRED_BINDING_NONE 0xFFu

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
static uint8_t trigger_deferred_size = 0u;
static uint32_t trigger_scheduler_tick = 0u;

static inline uint8_t trigger_chatter_guard_sanitize_duration(uint8_t duration_ms) {
    if (duration_ms > SETTINGS_TRIGGER_CHATTER_GUARD_MAX_MS) {
        return SETTINGS_TRIGGER_CHATTER_GUARD_MAX_MS;
    }

    return duration_ms;
}

static void trigger_transition_guard_reset_all(void) {
    for (uint8_t key = 0u; key < NUM_KEYS; key++) {
        trigger_transition_filter_reset(&key_transition_guards[key],
                                        (uint8_t)RELEASED);
    }
}

static inline void trigger_transition_guard_cancel(uint8_t key) {
    trigger_transition_filter_cancel(&key_transition_guards[key]);
}

static bool trigger_transition_guard_is_stable(uint8_t key,
                                               key_state_e desired_state,
                                               uint32_t now_ms) {
    return trigger_transition_filter_is_stable(
        &key_transition_guards[key], (uint8_t)desired_state,
        trigger_chatter_guard_enabled != 0u,
        trigger_chatter_guard_duration_ms, now_ms);
}

static inline bool is_below_actuation_point(int16_t distance, uint16_t actuation_point) {
    return distance >= actuation_point;
}

static inline bool is_above_release_point(int16_t distance, uint16_t release_point) {
    /* Actuation is inclusive on the downward edge; release is inclusive on
     * the upward edge. In particular, a valid 0.0 mm release point must be
     * reachable at the calibrated rest position. */
    return distance > (int16_t)release_point;
}

static inline void reset_rapid_trigger_extremums(uint8_t key, int16_t current_distance) {
    key_rapid_trigger_states[key].max_bottom_distance = current_distance;
    key_rapid_trigger_states[key].min_top_distance = current_distance;
}

static uint8_t trigger_deferred_ticks_from_setting(void) {
    uint8_t tick_rate = settings_get_advanced_tick_rate();
    if (tick_rate < SETTINGS_ADVANCED_TICK_RATE_MIN) {
        return SETTINGS_ADVANCED_TICK_RATE_MIN;
    }

    return tick_rate;
}

static void trigger_deferred_clear(void) {
    memset(trigger_deferred_queue, 0, sizeof(trigger_deferred_queue));
    trigger_deferred_size = 0u;
    trigger_scheduler_tick = 0u;
}

static bool trigger_deferred_push(uint8_t type, uint8_t key, uint8_t binding,
                                  uint16_t keycode, uint8_t ticks,
                                  uint8_t flags) {
    if (type == (uint8_t)TRIGGER_DEFERRED_ACTION_NONE || keycode == KC_NO) {
        return true;
    }

    if (trigger_deferred_size >= TRIGGER_DEFERRED_QUEUE_SIZE) {
        return false;
    }

    for (uint8_t i = 0u; i < TRIGGER_DEFERRED_QUEUE_SIZE; i++) {
        trigger_deferred_action_t *entry = &trigger_deferred_queue[i];
        if (!entry->used) {
            entry->type = type;
            entry->key = key;
            entry->binding = binding;
            entry->flags = flags;
            entry->keycode = keycode;
            entry->generation = key_behavior_states[key].dks_generation;
            entry->due_tick = trigger_scheduler_tick + (uint32_t)ticks;
            entry->used = true;
            trigger_deferred_size++;
            return true;
        }
    }

    return false;
}

static void trigger_deferred_execute(const trigger_deferred_action_t *action) {
    if (action == NULL || action->keycode == KC_NO) {
        return;
    }

    if (action->generation != key_behavior_states[action->key].dks_generation &&
        (action->flags & TRIGGER_DEFERRED_FLAG_COMPENSATING_RELEASE) == 0u) {
        return;
    }

    if (action->type == (uint8_t)TRIGGER_DEFERRED_ACTION_PRESS) {
        layout_press_action_for_key(action->key, action->keycode);
        if (action->binding < SETTINGS_DYNAMIC_ZONE_COUNT) {
            key_behavior_states[action->key]
                .dks_binding_pressed[action->binding] = true;
        }
    } else if (action->type == (uint8_t)TRIGGER_DEFERRED_ACTION_RELEASE) {
        layout_release_action_for_key(action->key, action->keycode);
        if (action->binding < SETTINGS_DYNAMIC_ZONE_COUNT) {
            key_behavior_states[action->key]
                .dks_binding_pressed[action->binding] = false;
        }
    }
}

static void trigger_deferred_cancel(uint8_t key, uint8_t binding) {
    for (uint8_t i = 0u; i < TRIGGER_DEFERRED_QUEUE_SIZE; i++) {
        trigger_deferred_action_t action = trigger_deferred_queue[i];
        if (!action.used || action.key != key ||
            (binding != TRIGGER_DEFERRED_BINDING_NONE &&
             action.binding != binding)) {
            continue;
        }

        trigger_deferred_queue[i].used = false;
        trigger_deferred_size--;
        /* A scheduled release is compensation for an output already emitted.
         * Execute it now instead of deleting it and leaving the host stuck. */
        if (action.type == (uint8_t)TRIGGER_DEFERRED_ACTION_RELEASE) {
            action.flags |= TRIGGER_DEFERRED_FLAG_COMPENSATING_RELEASE;
            trigger_deferred_execute(&action);
        }
    }
}

static void trigger_deferred_cancel_key(uint8_t key) {
    trigger_deferred_cancel(key, TRIGGER_DEFERRED_BINDING_NONE);
}

static void trigger_process_deferred_actions(void) {
    trigger_scheduler_tick++;

    for (uint8_t i = 0u; i < TRIGGER_DEFERRED_QUEUE_SIZE; i++) {
        trigger_deferred_action_t action = trigger_deferred_queue[i];
        if (!action.used ||
            (int32_t)(trigger_scheduler_tick - action.due_tick) < 0) {
            continue;
        }

        trigger_deferred_queue[i].used = false;
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
    (void)key;
    return settings->primary_keycode;
}

static void trigger_reconcile_released_toggle_latch(
    uint8_t key, key_behavior_mode_t desired_mode, uint16_t desired_keycode) {
    key_behavior_runtime_t *runtime = &key_behavior_states[key];

    if (!runtime->toggle_latched || key_states[key] != RELEASED) {
        return;
    }
    if (desired_keycode == KC_TRANSPARENT) {
        desired_keycode = layout_get_active_keycode(key);
    }
    if (desired_mode != KEY_BEHAVIOR_TOGGLE ||
        runtime->active_keycode != desired_keycode) {
        trigger_release_active_action(key);
        runtime->toggle_latched = false;
    }
}

static void trigger_tap_action(uint8_t key, uint16_t keycode) {
    if (keycode == KC_NO) {
        return;
    }

    /* Reserve the compensating release before emitting the press.  Dropping a
     * tap under extreme queue pressure is preferable to a permanently held
     * host key. */
    if (!trigger_deferred_push((uint8_t)TRIGGER_DEFERRED_ACTION_RELEASE, key,
                               TRIGGER_DEFERRED_BINDING_NONE, keycode,
                               trigger_deferred_ticks_from_setting(),
                               TRIGGER_DEFERRED_FLAG_COMPENSATING_RELEASE)) {
        return;
    }

    layout_press_action_for_key(key, keycode);
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

static bool trigger_dks_update_bottom_state(uint8_t key,
                                            const key_trigger_settings_t *settings,
                                            int16_t current_distance,
                                            uint32_t now_ms) {
    key_behavior_runtime_t *runtime = &key_behavior_states[key];
    uint16_t threshold_um =
        (uint16_t)settings->dynamic_bottom_out_point_tenths * 100u;
    bool desired = runtime->dks_is_bottomed_out;
    uint8_t stability_ms = trigger_chatter_guard_enabled
                               ? trigger_chatter_guard_duration_ms
                               : TRIGGER_DKS_STABILITY_MIN_MS;

    if (threshold_um == 0u) {
        threshold_um =
            (uint16_t)SETTINGS_DKS_BOTTOM_OUT_POINT_DEFAULT_TENTHS * 100u;
    }

    if (!runtime->dks_is_bottomed_out) {
        desired = current_distance >= (int16_t)threshold_um;
    } else {
        uint16_t release_threshold =
            threshold_um > TRIGGER_DKS_BOTTOM_HYSTERESIS_UM
                ? (uint16_t)(threshold_um - TRIGGER_DKS_BOTTOM_HYSTERESIS_UM)
                : 0u;
        desired = current_distance > (int16_t)release_threshold;
    }

    if (desired == runtime->dks_is_bottomed_out) {
        runtime->dks_candidate_active = false;
        return false;
    }

    if (!runtime->dks_candidate_active ||
        runtime->dks_candidate_bottomed_out != desired) {
        runtime->dks_candidate_active = true;
        runtime->dks_candidate_bottomed_out = desired;
        runtime->dks_candidate_since_ms = now_ms;
        return false;
    }

    if ((uint32_t)(now_ms - runtime->dks_candidate_since_ms) < stability_ms) {
        return false;
    }

    runtime->dks_candidate_active = false;
    runtime->dks_is_bottomed_out = desired;
    return true;
}

static void trigger_dks_process_phase(uint8_t key, uint8_t phase,
                                      bool immediate_press) {
    key_behavior_runtime_t *runtime = &key_behavior_states[key];
    const key_trigger_settings_t *settings = &key_pressed_settings[key];

    for (uint8_t i = 0u; i < SETTINGS_DYNAMIC_ZONE_COUNT; i++) {
        uint16_t keycode = settings->dynamic_zones[i].hid_keycode;
        uint8_t action = trigger_dks_action_from_bitmap(
            settings->dynamic_zones[i].end_mm_tenths, phase);

        if (keycode == KC_NO || action == (uint8_t)TRIGGER_DKS_ACTION_HOLD) {
            continue;
        }

        trigger_deferred_cancel(key, i);

        if (runtime->dks_binding_pressed[i]) {
            layout_release_action_for_key(key, keycode);
            runtime->dks_binding_pressed[i] = false;
        }

        if (action == (uint8_t)TRIGGER_DKS_ACTION_PRESS) {
            if (phase == (uint8_t)TRIGGER_DKS_PHASE_RELEASE) {
                trigger_tap_action(key, keycode);
            } else if (immediate_press) {
                layout_press_action_for_key(key, keycode);
                runtime->dks_binding_pressed[i] = true;
            } else {
                (void)trigger_deferred_push(
                    (uint8_t)TRIGGER_DEFERRED_ACTION_PRESS, key, i, keycode,
                    trigger_deferred_ticks_from_setting(), 0u);
            }
        } else if (action == (uint8_t)TRIGGER_DKS_ACTION_TAP) {
            trigger_tap_action(key, keycode);
        }
    }
}

static void trigger_activate_tap_hold_hold_action(uint8_t key) {
    key_behavior_runtime_t *runtime = &key_behavior_states[key];
    const key_trigger_settings_t *settings = &key_pressed_settings[key];
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

static void trigger_apply_hold_on_other_key_press(uint8_t source_key) {
    for (uint8_t key = 0u; key < NUM_KEYS; key++) {
        key_behavior_runtime_t *runtime = &key_behavior_states[key];
        const key_trigger_settings_t *settings = &key_pressed_settings[key];

        if (key == source_key || key_states[key] != PRESSED) {
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
    const key_trigger_settings_t *settings = &key_pressed_settings[key];
    bool is_bottomed_out = false;

    runtime->press_start_ms = now_ms;
    runtime->pressed_behavior_mode = settings->behavior_mode;
    runtime->socd_output_suppressed = false;

    switch (runtime->pressed_behavior_mode) {
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
        runtime->dks_generation++;
        memset(runtime->dks_binding_pressed, 0, sizeof(runtime->dks_binding_pressed));
        runtime->dks_candidate_active = false;
        runtime->dks_is_bottomed_out = false;
        is_bottomed_out =
            trigger_dks_is_bottomed_out(settings, current_distance);
        trigger_dks_process_phase(key, (uint8_t)TRIGGER_DKS_PHASE_PRESS,
                                  is_bottomed_out);
        if (is_bottomed_out) {
            runtime->dks_is_bottomed_out = true;
            trigger_dks_process_phase(
                key, (uint8_t)TRIGGER_DKS_PHASE_BOTTOM_OUT, false);
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
    const key_trigger_settings_t *settings = &key_pressed_settings[key];
    uint32_t elapsed_ms = now_ms - runtime->press_start_ms;
    bool is_bottomed_out = false;

    switch (runtime->pressed_behavior_mode) {
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
        is_bottomed_out = runtime->dks_is_bottomed_out;
        if (!trigger_dks_update_bottom_state(key, settings, current_distance,
                                             now_ms)) {
            break;
        }

        if (!is_bottomed_out && runtime->dks_is_bottomed_out) {
            trigger_dks_process_phase(
                key, (uint8_t)TRIGGER_DKS_PHASE_BOTTOM_OUT, false);
        } else if (is_bottomed_out && !runtime->dks_is_bottomed_out) {
            trigger_dks_process_phase(
                key, (uint8_t)TRIGGER_DKS_PHASE_RELEASE_FROM_BOTTOM_OUT,
                false);
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
    const key_trigger_settings_t *settings = &key_pressed_settings[key];

    switch (runtime->pressed_behavior_mode) {
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
            if (runtime->toggle_latched) {
                trigger_release_active_action(key);
                runtime->toggle_latched = false;
            } else {
                uint16_t primary_keycode =
                    trigger_resolve_primary_action_keycode(key, settings);
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
        trigger_dks_process_phase(key, (uint8_t)TRIGGER_DKS_PHASE_RELEASE,
                                  false);
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
        action_engine_cancel_all();
        keyboard_hid_release_all();
        keyboard_hid_reset_state();
        keyboard_nkro_hid_release_all();
        mouse_hid_release_all();
    }

    layout_reset_state();
    socd_load_settings();
    trigger_deferred_clear();

    for (uint8_t key = 0; key < NUM_KEYS; key++) {
        int16_t current_distance = analog_read_travel_distance_value(key);

        key_states[key] = RELEASED;
        key_rapid_trigger_states[key].last_distance = current_distance;
        key_rapid_trigger_states[key].continuous_armed = false;
        reset_rapid_trigger_extremums(key, current_distance);
        memset(&key_behavior_states[key], 0, sizeof(key_behavior_states[key]));
        key_behavior_states[key].active_keycode = KC_NO;
        trigger_transition_filter_reset(&key_transition_guards[key],
                                        (uint8_t)RELEASED);
    }
}

static void trigger_freeze_press_settings(uint8_t key) {
    uint16_t resolved_keycode = layout_get_active_keycode(key);

    key_pressed_settings[key] = key_trigger_settings[key];
    if (resolved_keycode != KC_TRANSPARENT) {
        key_pressed_settings[key].primary_keycode = resolved_keycode;
    }
}

static inline bool trigger_commit_press(uint8_t key, int16_t current_distance,
                                        uint32_t now_ms) {
    if (key_states[key] != RELEASED) {
        return false;
    }

    /* Any physical press, including another tap-hold key, resolves older
     * hold-on-other pending keys before this press becomes pending itself. */
    trigger_apply_hold_on_other_key_press(key);
    trigger_freeze_press_settings(key);
    key_states[key] = PRESSED;
    trigger_last_input_transition_ms = now_ms;
    trigger_behavior_on_press(key, current_distance, now_ms);
    led_matrix_key_event(key, true);
    socd_on_press(key);
    trigger_transition_guard_cancel(key);

    return true;
}

static inline bool trigger_commit_release(uint8_t key, uint32_t now_ms) {
    if (key_states[key] != PRESSED) {
        return false;
    }

    trigger_behavior_on_release(key);
    led_matrix_key_event(key, false);
    key_states[key] = RELEASED;
    trigger_last_input_transition_ms = now_ms;
    socd_on_release(key);
    key_behavior_states[key].socd_output_suppressed = false;
    trigger_reconcile_released_toggle_latch(
        key, key_trigger_settings[key].behavior_mode,
        key_trigger_settings[key].primary_keycode);
    trigger_transition_guard_cancel(key);

    return true;
}

static bool trigger_request_state(uint8_t key, key_state_e desired_state,
                                  int16_t current_distance,
                                  uint32_t now_ms) {
    if (desired_state == key_states[key]) {
        trigger_transition_guard_cancel(key);
        return false;
    }

    if (!trigger_transition_guard_is_stable(key, desired_state, now_ms)) {
        return false;
    }

    if (desired_state == PRESSED) {
        return trigger_commit_press(key, current_distance, now_ms);
    }

    return trigger_commit_release(key, now_ms);
}

static inline void handle_rapid_trigger(uint8_t key, int16_t current_distance,
                                        const key_trigger_settings_t *settings,
                                        uint32_t now_ms) {
    key_rapid_trigger_data_t *rt_data = &key_rapid_trigger_states[key];

    key_state_e state = key_states[key];

    if (state == RELEASED) {
        int16_t distance_from_min_top = 0;
        int16_t press_sensitivity =
            (int16_t)settings->rapid_trigger_press_sensitivity;

        if (current_distance < rt_data->min_top_distance) {
            rt_data->min_top_distance = current_distance;
        }

        distance_from_min_top = current_distance - rt_data->min_top_distance;
        if (distance_from_min_top >= press_sensitivity) {
            if (trigger_request_state(key, PRESSED, current_distance, now_ms)) {
                reset_rapid_trigger_extremums(key, current_distance);
            }
        } else {
            trigger_transition_guard_cancel(key);
        }
    } else {
        int16_t release_sensitivity =
            (int16_t)settings->rapid_trigger_release_sensitivity;
        int16_t distance_from_max_bottom = 0;

        if (current_distance > rt_data->max_bottom_distance) {
            rt_data->max_bottom_distance = current_distance;
        }

        distance_from_max_bottom = rt_data->max_bottom_distance - current_distance;
        if (distance_from_max_bottom >= release_sensitivity) {
            if (trigger_request_state(key, RELEASED, current_distance, now_ms)) {
                reset_rapid_trigger_extremums(key, current_distance);
            }
        } else {
            trigger_transition_guard_cancel(key);
        }
    }

    rt_data->last_distance = current_distance;
}

static inline void handle_trigger(uint8_t key, uint32_t now_ms) {
    int16_t current_distance = analog_read_travel_distance_value(key);
    const key_trigger_settings_t *settings =
        key_states[key] == PRESSED ? &key_pressed_settings[key]
                                   : &key_trigger_settings[key];
    uint16_t actuation_point = settings->actuation_point;
    uint16_t release_point = settings->release_point;
    key_rapid_trigger_data_t *rt_data = &key_rapid_trigger_states[key];

    if (settings->behavior_mode == KEY_BEHAVIOR_DYNAMIC ||
        !settings->is_rapid_trigger_enabled) {
        if (key_states[key] == RELEASED) {
            if (is_below_actuation_point(current_distance, actuation_point)) {
                (void)trigger_request_state(key, PRESSED, current_distance,
                                            now_ms);
            } else {
                trigger_transition_guard_cancel(key);
            }
        } else {
            if (!is_above_release_point(current_distance, release_point)) {
                (void)trigger_request_state(key, RELEASED, current_distance,
                                            now_ms);
            } else {
                trigger_transition_guard_cancel(key);
            }
        }
    } else if (!settings->continuous_rapid_trigger) {
        if (key_states[key] == PRESSED &&
            !is_above_release_point(current_distance, release_point)) {
            if (trigger_request_state(key, RELEASED, current_distance, now_ms)) {
                reset_rapid_trigger_extremums(key, current_distance);
                rt_data->last_distance = current_distance;
            }
        } else if (key_states[key] == RELEASED &&
                   !is_below_actuation_point(current_distance, actuation_point)) {
            trigger_transition_guard_cancel(key);
            rt_data->last_distance = current_distance;
            if (current_distance < rt_data->min_top_distance) {
                rt_data->min_top_distance = current_distance;
            }
        } else {
            handle_rapid_trigger(key, current_distance, settings, now_ms);
        }
    } else {
        if (current_distance <= (int16_t)TRIGGER_CONTINUOUS_RT_REST_BAND_UM) {
            if (key_states[key] == PRESSED) {
                (void)trigger_request_state(key, RELEASED, current_distance,
                                            now_ms);
            }
            if (key_states[key] == RELEASED) {
                rt_data->continuous_armed = false;
                reset_rapid_trigger_extremums(key, current_distance);
                rt_data->last_distance = current_distance;
            }
            goto trigger_behavior_update;
        }

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
            if (trigger_request_state(key, RELEASED, current_distance, now_ms)) {
                reset_rapid_trigger_extremums(key, current_distance);
                rt_data->last_distance = current_distance;
            }
        } else {
            handle_rapid_trigger(key, current_distance, settings, now_ms);
        }

    }

trigger_behavior_update:
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

bool trigger_is_input_idle(uint32_t now_ms, uint32_t quiet_period_ms) {
    for (uint8_t key = 0u; key < NUM_KEYS; key++) {
        if (key_states[key] == PRESSED) {
            return false;
        }
    }

    return (uint32_t)(now_ms - trigger_last_input_transition_ms) >=
           quiet_period_ms;
}

void trigger_socd_set_key_output(uint8_t key, bool pressed) {
    uint16_t keycode = KC_NO;
    key_behavior_runtime_t *runtime = NULL;

    if (key >= NUM_KEYS ||
        key_states[key] != PRESSED ||
        key_behavior_states[key].pressed_behavior_mode != KEY_BEHAVIOR_NORMAL) {
        return;
    }

    runtime = &key_behavior_states[key];
    keycode = runtime->active_keycode;
    if (keycode == KC_NO) {
        return;
    }

    if (pressed && runtime->socd_output_suppressed) {
        layout_press_action_for_key(key, keycode);
        runtime->socd_output_suppressed = false;
    } else if (!pressed && !runtime->socd_output_suppressed) {
        layout_release_action_for_key(key, keycode);
        runtime->socd_output_suppressed = true;
    }
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
    uint16_t resolved_keycode = settings->hid_keycode;

    if (resolved_keycode == KC_TRANSPARENT) {
        resolved_keycode = layout_get_active_keycode(key);
    }
    trigger_reconcile_released_toggle_latch(
        key, (key_behavior_mode_t)settings->advanced.behavior_mode,
        resolved_keycode);

    runtime->primary_keycode = settings->hid_keycode;
    runtime->is_rapid_trigger_enabled = settings->rapid_trigger_enabled ? true : false;
    runtime->continuous_rapid_trigger =
        settings_key_is_continuous_rapid_trigger_enabled(settings);
    runtime->actuation_point = mm_tenths_to_um(settings->actuation_point_mm);
    runtime->release_point = mm_tenths_to_um(settings->release_point_mm);
    if (runtime->release_point +
            (SETTINGS_TRIGGER_MIN_HYSTERESIS_TENTHS * 100u) >
        runtime->actuation_point) {
        runtime->release_point =
            runtime->actuation_point >
                    (SETTINGS_TRIGGER_MIN_HYSTERESIS_TENTHS * 100u)
                ? (uint16_t)(runtime->actuation_point -
                             (SETTINGS_TRIGGER_MIN_HYSTERESIS_TENTHS * 100u))
                : 0u;
    }
    runtime->rapid_trigger_press_sensitivity =
        mm_hundredths_to_um(
            settings->rapid_trigger_press <
                    SETTINGS_RAPID_TRIGGER_MIN_HUNDREDTHS
                ? SETTINGS_RAPID_TRIGGER_MIN_HUNDREDTHS
                : settings->rapid_trigger_press);
    runtime->rapid_trigger_release_sensitivity =
        mm_hundredths_to_um(
            settings->rapid_trigger_release <
                    SETTINGS_RAPID_TRIGGER_MIN_HUNDREDTHS
                ? SETTINGS_RAPID_TRIGGER_MIN_HUNDREDTHS
                : settings->rapid_trigger_release);
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

void trigger_reload_key_settings(uint8_t key) {
        settings_key_t settings = {0};
        uint8_t layer = trigger_runtime_active_layer();

        if (key >= NUM_KEYS ||
            !settings_get_key_for_layer(key, layer, &settings)) {
            return;
        }
        /* Deliberately not touching trigger_active_layer_cache: only one key
         * was refreshed. Marking the whole layer as loaded made
         * trigger_refresh_layer_runtime_settings() skip the full reload, so
         * the other NUM_KEYS-1 keys kept the previous layer's actuation,
         * release and rapid-trigger parameters. */
        trigger_apply_key_settings(key, &settings);
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

    int16_t um = analog_read_travel_distance_value(key);
    if (um < 0) {
        um = 0;
    }
    /* The trigger consumes the extrapolated value, but this accessor feeds the
     * host readout, whose contract is 0.00..4.00 mm. Clamp here so reporting
     * stays unchanged while the trigger keeps its resolution past the
     * calibrated peak. */
    if (um > (int16_t)SETTINGS_LOGICAL_TRAVEL_UM) {
        um = (int16_t)SETTINGS_LOGICAL_TRAVEL_UM;
    }

    return (uint16_t)((um + 5) / 10);
}

bool trigger_set_chatter_guard(bool enabled, uint8_t duration_ms) {
    if (duration_ms > SETTINGS_TRIGGER_CHATTER_GUARD_MAX_MS ||
        (enabled && duration_ms == 0u)) {
        return false;
    }

    trigger_chatter_guard_enabled = enabled ? 1u : 0u;
    trigger_chatter_guard_duration_ms =
        trigger_chatter_guard_sanitize_duration(duration_ms);
    trigger_transition_guard_reset_all();

    return true;
}

void trigger_get_chatter_guard(bool *enabled, uint8_t *duration_ms) {
    if (enabled != NULL) {
        *enabled = trigger_chatter_guard_enabled != 0u;
    }

    if (duration_ms != NULL) {
        *duration_ms = trigger_chatter_guard_duration_ms;
    }
}

void trigger_init() {
    trigger_deferred_clear();
    trigger_active_layer_cache = 0xFFu;
    trigger_last_input_transition_ms = HAL_GetTick();
    trigger_chatter_guard_enabled =
        (uint8_t)(SETTINGS_DEFAULT_TRIGGER_CHATTER_GUARD_ENABLED ? 1u : 0u);
    trigger_chatter_guard_duration_ms =
        trigger_chatter_guard_sanitize_duration((uint8_t)SETTINGS_DEFAULT_TRIGGER_CHATTER_GUARD_MS);

    for (int i = 0; i < NUM_KEYS; i++) {
        key_trigger_settings[i].primary_keycode = KC_NO;
        key_trigger_settings[i].actuation_point = DEFAULT_ACTUATION_POINT;
        key_trigger_settings[i].release_point =
            DEFAULT_ACTUATION_POINT -
            (SETTINGS_TRIGGER_MIN_HYSTERESIS_TENTHS * 100u);
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
        key_pressed_settings[i] = key_trigger_settings[i];

        // Initialize key states
        key_states[i] = RELEASED;
    }

    trigger_transition_guard_reset_all();
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
        /* A key accepted earlier in this scan may have changed a layer or
         * profile. Refresh immediately so later keys freeze the matching
         * advanced behavior, not only the new primary keycode. */
        trigger_refresh_layer_runtime_settings();
    }

}
