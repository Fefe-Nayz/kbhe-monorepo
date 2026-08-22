#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "trigger/socd.h"
#include "board_config.h"
#include "hid/keyboard_hid.h"
#include "analog/analog.h"
#include "layout/layout.h"
#include "trigger/trigger.h"
#include "stm32f7xx_hal.h"

#define SOCD_WINNER_NONE 0xFFu
#define SOCD_FULLY_PRESSED_CANDIDATE_NONE 0xFFu
#define SOCD_DISTANCE_HYSTERESIS_UM 100u
#define SOCD_STABILITY_MIN_MS 1u

static socd_key_settings_t socd_key_settings[NUM_KEYS];

static bool socd_override_states[NUM_KEYS];
static uint8_t socd_pair_winner[NUM_KEYS];
static uint8_t socd_pair_candidate[NUM_KEYS];
static uint32_t socd_pair_candidate_since_ms[NUM_KEYS];
static bool socd_pair_fully_pressed[NUM_KEYS];
static uint8_t socd_pair_fully_pressed_candidate[NUM_KEYS];
static uint32_t socd_pair_fully_pressed_candidate_since_ms[NUM_KEYS];
/* Record every physical press, even while the key is not currently paired.
 * Layer/profile reloads can then arbitrate a newly-created LAST_INPUT pair
 * without leaving both already-held outputs active. Zero means unseen. */
static uint16_t socd_press_sequence[NUM_KEYS];
static uint16_t next_socd_press_sequence = 1u;

static void socd_reset_key_config(uint8_t key) {
    socd_override_states[key] = false;
    socd_key_settings[key].resolution_mode = SETTINGS_SOCD_RESOLUTION_LAST_INPUT_WINS;
    socd_key_settings[key].is_socd_enabled = false;
    socd_key_settings[key].fully_pressed_enabled = false;
    socd_key_settings[key].fully_pressed_point_um =
        (uint16_t)SETTINGS_SOCD_FULLY_PRESSED_POINT_DEFAULT_TENTHS * 100u;
    socd_key_settings[key].linked_key = key;
    socd_pair_winner[key] = SOCD_WINNER_NONE;
    socd_pair_candidate[key] = SOCD_WINNER_NONE;
    socd_pair_candidate_since_ms[key] = 0u;
    socd_pair_fully_pressed[key] = false;
    socd_pair_fully_pressed_candidate[key] =
        SOCD_FULLY_PRESSED_CANDIDATE_NONE;
    socd_pair_fully_pressed_candidate_since_ms[key] = 0u;
}

static uint8_t socd_stability_duration_ms(void) {
    bool guard_enabled = false;
    uint8_t guard_ms = 0u;

    trigger_get_chatter_guard(&guard_enabled, &guard_ms);
    if (!guard_enabled || guard_ms < SOCD_STABILITY_MIN_MS) {
        return SOCD_STABILITY_MIN_MS;
    }
    return guard_ms;
}

static bool socd_update_fully_pressed_state(uint8_t pair_key,
                                            bool desired_state,
                                            uint32_t now_ms) {
    uint8_t desired_candidate = desired_state ? 1u : 0u;

    if (desired_state == socd_pair_fully_pressed[pair_key]) {
        socd_pair_fully_pressed_candidate[pair_key] =
            SOCD_FULLY_PRESSED_CANDIDATE_NONE;
        return false;
    }

    if (socd_pair_fully_pressed_candidate[pair_key] != desired_candidate) {
        socd_pair_fully_pressed_candidate[pair_key] = desired_candidate;
        socd_pair_fully_pressed_candidate_since_ms[pair_key] = now_ms;
        return false;
    }

    if ((uint32_t)(now_ms -
                   socd_pair_fully_pressed_candidate_since_ms[pair_key]) <
        socd_stability_duration_ms()) {
        return false;
    }

    socd_pair_fully_pressed[pair_key] = desired_state;
    socd_pair_fully_pressed_candidate[pair_key] =
        SOCD_FULLY_PRESSED_CANDIDATE_NONE;
    return true;
}

/*
 * Most pressed wins resolution logic
 */

static void socd_apply_winner(uint8_t key1, uint8_t key2, uint8_t winner) {
    uint8_t loser = winner == key1 ? key2 : key1;

    trigger_socd_set_key_output(loser, false);
    socd_override_states[loser] = true;
    trigger_socd_set_key_output(winner, true);
    socd_override_states[winner] = false;
    socd_pair_winner[key1] = winner;
}

static bool socd_sequence_is_newer(uint16_t candidate, uint16_t reference) {
    if (candidate == 0u) {
        return false;
    }
    if (reference == 0u) {
        return true;
    }
    return (int16_t)(candidate - reference) > 0;
}

static void socd_record_press(uint8_t key) {
    socd_press_sequence[key] = next_socd_press_sequence++;
    if (next_socd_press_sequence == 0u) {
        next_socd_press_sequence = 1u;
    }
}

static void handle_most_pressed_wins(uint8_t key1, uint8_t key2,
                                     uint32_t now_ms) {
    int16_t key1_distance = analog_read_travel_distance_value(key1);
    int16_t key2_distance = analog_read_travel_distance_value(key2);
    uint8_t current = socd_pair_winner[key1];
    uint8_t desired = current;

    if (current != key1 && current != key2) {
        desired = key1_distance >= key2_distance ? key1 : key2;
        socd_apply_winner(key1, key2, desired);
        socd_pair_candidate[key1] = SOCD_WINNER_NONE;
        return;
    }

    /* A previous release intentionally leaves the remaining key as winner.
     * When its partner is pressed again both normal outputs are initially
     * active, so re-assert the existing winner before applying hysteresis. */
    socd_apply_winner(key1, key2, current);

    if (current == key1 &&
        (int32_t)key2_distance >=
            (int32_t)key1_distance + (int32_t)SOCD_DISTANCE_HYSTERESIS_UM) {
        desired = key2;
    } else if (current == key2 &&
               (int32_t)key1_distance >=
                   (int32_t)key2_distance +
                       (int32_t)SOCD_DISTANCE_HYSTERESIS_UM) {
        desired = key1;
    }

    if (desired == current) {
        socd_pair_candidate[key1] = SOCD_WINNER_NONE;
        return;
    }

    if (socd_pair_candidate[key1] != desired) {
        socd_pair_candidate[key1] = desired;
        socd_pair_candidate_since_ms[key1] = now_ms;
        return;
    }

    if ((uint32_t)(now_ms - socd_pair_candidate_since_ms[key1]) <
        socd_stability_duration_ms()) {
        return;
    }

    socd_apply_winner(key1, key2, desired);
    socd_pair_candidate[key1] = SOCD_WINNER_NONE;
}

static void socd_reconcile_held_pair(uint8_t key1, uint8_t key2,
                                     uint32_t now_ms) {
    socd_resolution_e resolution = socd_key_settings[key1].resolution_mode;
    uint8_t winner = SOCD_WINNER_NONE;

    if (trigger_get_key_state(key1) != PRESSED ||
        trigger_get_key_state(key2) != PRESSED) {
        return;
    }

    switch (resolution) {
    case SETTINGS_SOCD_RESOLUTION_LAST_INPUT_WINS:
        winner = socd_sequence_is_newer(socd_press_sequence[key1],
                                        socd_press_sequence[key2])
                     ? key1
                     : key2;
        socd_apply_winner(key1, key2, winner);
        break;
    case SETTINGS_SOCD_RESOLUTION_MOST_PRESSED_WINS:
        handle_most_pressed_wins(key1, key2, now_ms);
        break;
    case SETTINGS_SOCD_RESOLUTION_ABSOLUTE_PRIORITY_KEY1:
        socd_apply_winner(key1, key2, key1);
        break;
    case SETTINGS_SOCD_RESOLUTION_ABSOLUTE_PRIORITY_KEY2:
        socd_apply_winner(key1, key2, key2);
        break;
    case SETTINGS_SOCD_RESOLUTION_NEUTRAL:
        trigger_socd_set_key_output(key1, false);
        trigger_socd_set_key_output(key2, false);
        socd_override_states[key1] = true;
        socd_override_states[key2] = true;
        socd_pair_winner[key1] = SOCD_WINNER_NONE;
        socd_pair_candidate[key1] = SOCD_WINNER_NONE;
        break;
    default:
        break;
    }
}

/*
 * Last input wins resolution logic
 */

void socd_on_press(uint8_t key) {
    socd_key_settings_t *settings = NULL;

    if (key >= NUM_KEYS) {
        return;
    }

    socd_record_press(key);
    settings = &socd_key_settings[key];

    if (!settings->is_socd_enabled) {
        return;
    }

    uint8_t linked_key = settings->linked_key;
    /* A released key may carry an old loser marker from an earlier pair
     * sequence. Its newly emitted press starts unsuppressed. */
    socd_override_states[key] = false;

    switch (settings->resolution_mode) {
    case SETTINGS_SOCD_RESOLUTION_LAST_INPUT_WINS:
        if (trigger_get_key_state(linked_key) == PRESSED) {
            trigger_socd_set_key_output(linked_key, false);
            socd_override_states[linked_key] = true;
        }
        break;

    case SETTINGS_SOCD_RESOLUTION_ABSOLUTE_PRIORITY_KEY1:
        if (trigger_get_key_state(linked_key) != PRESSED) {
            break;
        }
        if (key < linked_key) {
            trigger_socd_set_key_output(linked_key, false);
            socd_override_states[linked_key] = true;
        } else {
            trigger_socd_set_key_output(key, false);
            socd_override_states[key] = true;
        }
        break;

    case SETTINGS_SOCD_RESOLUTION_ABSOLUTE_PRIORITY_KEY2:
        if (trigger_get_key_state(linked_key) != PRESSED) {
            break;
        }
        if (key < linked_key) {
            trigger_socd_set_key_output(key, false);
            socd_override_states[key] = true;
        } else {
            trigger_socd_set_key_output(linked_key, false);
            socd_override_states[linked_key] = true;
        }
        break;

    case SETTINGS_SOCD_RESOLUTION_NEUTRAL:
        if (trigger_get_key_state(linked_key) != PRESSED) {
            break;
        }
        trigger_socd_set_key_output(key, false);
        trigger_socd_set_key_output(linked_key, false);
        socd_override_states[key] = true;
        socd_override_states[linked_key] = true;
        break;

    default:
        break;
    }
}

void socd_on_release(uint8_t key) {
    socd_key_settings_t *settings = NULL;

    if (key >= NUM_KEYS) {
        return;
    }

    settings = &socd_key_settings[key];

    if (!settings->is_socd_enabled) {
        return;
    }

    uint8_t linked_key = settings->linked_key;

    if (settings->resolution_mode == SETTINGS_SOCD_RESOLUTION_LAST_INPUT_WINS ||
        settings->resolution_mode == SETTINGS_SOCD_RESOLUTION_MOST_PRESSED_WINS ||
        settings->resolution_mode == SETTINGS_SOCD_RESOLUTION_ABSOLUTE_PRIORITY_KEY1 ||
        settings->resolution_mode == SETTINGS_SOCD_RESOLUTION_ABSOLUTE_PRIORITY_KEY2 ||
        settings->resolution_mode == SETTINGS_SOCD_RESOLUTION_NEUTRAL) {
        key_state_e linked_key_state = trigger_get_key_state(linked_key);

        /* The releasing key is no longer physical-pressed, so its override
         * state must always be cleared.  Leaving it set would be a stale
         * truth that prevents correct re-evaluation on the next press. */
        socd_override_states[key] = false;

        if (linked_key_state == PRESSED && socd_override_states[linked_key]) {
            trigger_socd_set_key_output(linked_key, true);
            socd_override_states[linked_key] = false;
        }

        if (key < linked_key) {
            socd_pair_winner[key] =
                linked_key_state == PRESSED ? linked_key : SOCD_WINNER_NONE;
            socd_pair_candidate[key] = SOCD_WINNER_NONE;
            socd_pair_fully_pressed[key] = false;
            socd_pair_fully_pressed_candidate[key] =
                SOCD_FULLY_PRESSED_CANDIDATE_NONE;
        } else {
            socd_pair_winner[linked_key] =
                linked_key_state == PRESSED ? linked_key : SOCD_WINNER_NONE;
            socd_pair_candidate[linked_key] = SOCD_WINNER_NONE;
            socd_pair_fully_pressed[linked_key] = false;
            socd_pair_fully_pressed_candidate[linked_key] =
                SOCD_FULLY_PRESSED_CANDIDATE_NONE;
        }
    }
}

void socd_init(void) {
    next_socd_press_sequence = 1u;
    for (uint8_t i = 0; i < NUM_KEYS; i++) {
        socd_press_sequence[i] = 0u;
        socd_reset_key_config(i);
    }

    socd_load_settings();
}

void socd_load_settings(void) {
    uint8_t active_layer = layout_get_active_layer_top();
    uint32_t now_ms = HAL_GetTick();

    if (active_layer >= SETTINGS_LAYER_COUNT) {
        active_layer = 0u;
    }

    /* Restore outputs suppressed by the old configuration before replacing
     * it.  Otherwise a layer/profile reload can forget the override while the
     * host key remains released. */
    for (uint8_t i = 0u; i < NUM_KEYS; i++) {
        if (socd_override_states[i] && trigger_get_key_state(i) == PRESSED) {
            trigger_socd_set_key_output(i, true);
        }
        socd_reset_key_config(i);
    }

    for (uint8_t i = 0; i < NUM_KEYS; i++) {
        settings_key_t key_settings = {0};
        settings_key_t partner_settings = {0};
        uint8_t linked_key = SETTINGS_SOCD_PAIR_NONE;

        if (!settings_get_key_for_layer(i, active_layer, &key_settings)) {
            continue;
        }

        linked_key = key_settings.socd_pair;
        if (linked_key == SETTINGS_SOCD_PAIR_NONE || linked_key >= NUM_KEYS ||
            linked_key <= i ||
            !settings_get_key_for_layer(linked_key, active_layer,
                                        &partner_settings) ||
            partner_settings.socd_pair != i) {
            continue;
        }

        socd_key_settings[i].resolution_mode = (socd_resolution_e)
            settings_key_get_socd_resolution(&key_settings);
        socd_key_settings[i].fully_pressed_enabled =
            settings_key_is_socd_fully_pressed_enabled(&key_settings);
        /* Only override the reset-to-default value when a valid threshold is
         * stored.  A stored zero would make distance>=0 always true, which
         * would permanently disable SOCD resolution. */
        if (key_settings.advanced.socd_fully_pressed_point_tenths != 0u) {
          socd_key_settings[i].fully_pressed_point_um =
              (uint16_t)key_settings.advanced.socd_fully_pressed_point_tenths *
              100u;
        }
        socd_key_settings[i].is_socd_enabled = true;
        socd_key_settings[i].linked_key = linked_key;
        socd_key_settings[linked_key] = socd_key_settings[i];
        socd_key_settings[linked_key].linked_key = i;
        socd_reconcile_held_pair(i, linked_key, now_ms);
    }
}

void socd_task(void) {
    uint32_t now_ms = HAL_GetTick();

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
                int16_t key1_distance =
                    analog_read_travel_distance_value(key1);
                int16_t key2_distance =
                    analog_read_travel_distance_value(key2);
                uint16_t threshold = settings->fully_pressed_point_um;
                uint16_t release_threshold =
                    threshold > SOCD_DISTANCE_HYSTERESIS_UM
                        ? (uint16_t)(threshold - SOCD_DISTANCE_HYSTERESIS_UM)
                        : 0u;
                bool was_fully_pressed = socd_pair_fully_pressed[key1];
                bool desired_fully_pressed = was_fully_pressed
                    ? key1_distance > (int16_t)release_threshold &&
                          key2_distance > (int16_t)release_threshold
                    : key1_distance >= (int16_t)threshold &&
                          key2_distance >= (int16_t)threshold;

                (void)socd_update_fully_pressed_state(
                    key1, desired_fully_pressed, now_ms);

                if (socd_pair_fully_pressed[key1]) {
                    trigger_socd_set_key_output(key1, true);
                    trigger_socd_set_key_output(key2, true);
                    socd_override_states[key1] = false;
                    socd_override_states[key2] = false;
                    socd_pair_fully_pressed[key1] = true;
                    socd_pair_candidate[key1] = SOCD_WINNER_NONE;
                    continue;
                }

                if (was_fully_pressed) {
                    /* Both outputs were restored in fully-pressed mode. Force
                     * a fresh arbitration on the stable exit; retaining the
                     * old winner would otherwise leave both outputs active
                     * until the 0.1 mm winner hysteresis was crossed. */
                    socd_pair_winner[key1] = SOCD_WINNER_NONE;
                    socd_pair_candidate[key1] = SOCD_WINNER_NONE;
                }
            }

            handle_most_pressed_wins(key1, key2, now_ms);
        }
    }
}
