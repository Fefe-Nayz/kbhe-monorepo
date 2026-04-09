#include "analog/calibration.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "analog/analog.h"
#include "analog/lut.h"
#include "led_matrix.h"
#include "settings.h"

#define CALIBRATION_GUIDED_ZERO_DURATION_MS 2500u
#define CALIBRATION_GUIDED_DONE_DURATION_MS 1500u
#define CALIBRATION_GUIDED_PRESS_THRESHOLD_ADC 24u
#define CALIBRATION_GUIDED_RELEASE_THRESHOLD_ADC 12u

static int16_t zero_offset[NUM_KEYS];
static uint16_t max_distance_um[NUM_KEYS];
static uint8_t guided_led_frame[LED_MATRIX_DATA_SIZE];

typedef struct {
    bool active;
    calibration_guided_phase_t phase;
    uint32_t phase_start_ms;
    uint32_t last_tick_ms;
    uint16_t sample_count;
    uint8_t current_key;
    bool current_key_pressed;
    uint16_t current_peak_raw;
    uint32_t zero_accumulator[NUM_KEYS];
} guided_calibration_state_t;

static guided_calibration_state_t guided_state;

static inline uint16_t calibration_default_max_raw(void) {
    return (uint16_t)(LUT_BASE_VOLTAGE + LUT_SIZE - 1);
}

static inline uint16_t calibration_apply_zero_offset(uint8_t key, uint16_t adc_value) {
    if (key >= NUM_KEYS) {
        return adc_value;
    }

    int32_t adjusted = (int32_t)adc_value - (int32_t)zero_offset[key];
    if (adjusted <= 0) {
        return 0u;
    }
    if (adjusted > 0xFFFF) {
        return 0xFFFFu;
    }
    return (uint16_t)adjusted;
}

static void calibration_recompute_key_max_distance(uint8_t key, uint16_t max_raw_value) {
    if (key >= NUM_KEYS) {
        return;
    }

    uint16_t calibrated_max = calibration_apply_zero_offset(key, max_raw_value);
    uint16_t distance_um = (uint16_t)getDistanceFromVoltage(calibrated_max);
    if (distance_um == 0u) {
        distance_um = (uint16_t)getDistanceFromVoltage(calibration_default_max_raw());
    }
    if (distance_um == 0u) {
        distance_um = 1u;
    }
    max_distance_um[key] = distance_um;
}

static void calibration_restore_runtime_led_state(void) {
    uint8_t effect_params[LED_EFFECT_PARAM_COUNT] = {0};
    uint8_t r = 0u;
    uint8_t g = 0u;
    uint8_t b = 0u;

    settings_get_led_effect_color(&r, &g, &b);
    settings_get_led_effect_params(settings_get_led_effect_mode(), effect_params);
    led_matrix_set_effect((led_effect_mode_t)settings_get_led_effect_mode());
    led_matrix_set_effect_speed(settings_get_led_effect_speed());
    led_matrix_set_effect_color(r, g, b);
    led_matrix_set_effect_params(effect_params);
    led_matrix_set_fps_limit(settings_get_led_fps_limit());
    led_matrix_set_enabled(settings_is_led_enabled());
}

static void calibration_take_led_control(void) {
    led_matrix_set_enabled(true);
    led_matrix_set_effect(LED_EFFECT_THIRD_PARTY);
    memset(guided_led_frame, 0, sizeof(guided_led_frame));
    led_matrix_set_live_frame(guided_led_frame);
}

static uint8_t calibration_pulse_value(uint32_t now_ms, uint16_t period_ms,
                                       uint8_t min_value, uint8_t max_value) {
    uint32_t phase = (period_ms == 0u) ? 0u : (now_ms % period_ms);
    uint32_t half_period = (period_ms > 1u) ? (period_ms / 2u) : 1u;
    uint32_t ramp = (phase <= half_period) ? phase : (period_ms - phase);
    uint32_t span = (max_value > min_value) ? (uint32_t)(max_value - min_value) : 0u;
    return (uint8_t)(min_value + ((ramp * span) / half_period));
}

static void calibration_render_all(uint8_t r, uint8_t g, uint8_t b) {
    for (uint8_t key = 0; key < NUM_KEYS; ++key) {
        guided_led_frame[key * 3 + 0] = r;
        guided_led_frame[key * 3 + 1] = g;
        guided_led_frame[key * 3 + 2] = b;
    }
    led_matrix_set_live_frame(guided_led_frame);
}

static void calibration_render_single_key(uint8_t key_index, uint8_t r, uint8_t g, uint8_t b) {
    memset(guided_led_frame, 0, sizeof(guided_led_frame));
    if (key_index < NUM_KEYS) {
        guided_led_frame[key_index * 3 + 0] = r;
        guided_led_frame[key_index * 3 + 1] = g;
        guided_led_frame[key_index * 3 + 2] = b;
    }
    led_matrix_set_live_frame(guided_led_frame);
}

static void calibration_finish_session(bool success) {
    if (success) {
        guided_state.phase = CALIBRATION_GUIDED_COMPLETE;
    } else {
        guided_state.phase = CALIBRATION_GUIDED_ERROR;
    }
    guided_state.phase_start_ms = 0u;
    guided_state.sample_count = 0u;
}

void calibration_init(void) {
    for (uint8_t i = 0; i < NUM_KEYS; i++) {
        zero_offset[i] = 0;
        max_distance_um[i] = (uint16_t)getDistanceFromVoltage(calibration_default_max_raw());
        if (max_distance_um[i] == 0u) {
            max_distance_um[i] = 1u;
        }
    }

    memset(&guided_state, 0, sizeof(guided_state));
}

void calibration_load_settings(void) {
    const settings_calibration_t *cal = settings_get_calibration();
    uint16_t default_max_raw = calibration_default_max_raw();

    if (cal == NULL) {
        return;
    }

    for (uint8_t i = 0; i < NUM_KEYS; ++i) {
        zero_offset[i] = (int16_t)(cal->key_zero_values[i] - cal->lut_zero_value);
        uint16_t max_raw_value = (cal->key_max_values[i] > 0)
                                     ? (uint16_t)cal->key_max_values[i]
                                     : default_max_raw;
        if (max_raw_value <= (uint16_t)cal->key_zero_values[i]) {
            max_raw_value = default_max_raw;
        }
        calibration_recompute_key_max_distance(i, max_raw_value);
    }
}

void calibration_key_set_zero_offset(uint8_t key, uint16_t zero_value) {
    if (key >= NUM_KEYS) {
        return;
    }

    zero_offset[key] = (int16_t)(zero_value - LUT_ZERO_VALUE);
    calibration_recompute_key_max_distance(key, settings_get_calibration()->key_max_values[key]);
}

void calibration_key_set_max_value(uint8_t key, uint16_t max_value) {
    if (key >= NUM_KEYS) {
        return;
    }

    calibration_recompute_key_max_distance(key, max_value);
}

uint16_t calibration_get_calibrated_value(uint8_t key, uint16_t adc_value) {
    return calibration_apply_zero_offset(key, adc_value);
}

uint16_t calibration_get_max_distance_um(uint8_t key) {
    if (key >= NUM_KEYS) {
        return 1u;
    }
    return max_distance_um[key] == 0u ? 1u : max_distance_um[key];
}

uint8_t calibration_get_normalized_distance(uint8_t key, int16_t distance_um) {
    uint32_t distance = (distance_um <= 0) ? 0u : (uint32_t)distance_um;
    uint32_t max_distance = calibration_get_max_distance_um(key);
    if (max_distance == 0u) {
        return 0u;
    }
    if (distance >= max_distance) {
        return 255u;
    }
    return (uint8_t)((distance * 255u) / max_distance);
}

bool calibration_guided_start(void) {
    if (guided_state.active) {
        return false;
    }

    memset(&guided_state, 0, sizeof(guided_state));
    guided_state.active = true;
    guided_state.phase = CALIBRATION_GUIDED_ZERO_AVERAGE;
    calibration_take_led_control();
    return true;
}

void calibration_guided_abort(void) {
    if (!guided_state.active) {
        return;
    }

    memset(&guided_state, 0, sizeof(guided_state));
    calibration_restore_runtime_led_state();
}

void calibration_guided_tick(uint32_t now_ms) {
    if (!guided_state.active) {
        return;
    }

    guided_state.last_tick_ms = now_ms;

    if (guided_state.phase_start_ms == 0u) {
        guided_state.phase_start_ms = now_ms;
    }

    switch (guided_state.phase) {
    case CALIBRATION_GUIDED_ZERO_AVERAGE: {
        uint8_t pulse = calibration_pulse_value(now_ms, 900u, 24u, 180u);
        calibration_render_all(pulse, 0u, 0u);
        if ((now_ms - guided_state.phase_start_ms) >= CALIBRATION_GUIDED_ZERO_DURATION_MS &&
            guided_state.sample_count > 0u) {
            settings_calibration_t calibration = *settings_get_calibration();
            for (uint8_t key = 0; key < NUM_KEYS; key++) {
                calibration.key_zero_values[key] =
                    (int16_t)(guided_state.zero_accumulator[key] / guided_state.sample_count);
                if (calibration.key_max_values[key] <= calibration.key_zero_values[key]) {
                    calibration.key_max_values[key] = (int16_t)calibration_default_max_raw();
                }
            }
            settings_set_calibration(&calibration);
            calibration_load_settings();

            guided_state.phase = CALIBRATION_GUIDED_WAIT_MAX_PRESS;
            guided_state.phase_start_ms = now_ms;
            guided_state.current_key = 0u;
            guided_state.current_key_pressed = false;
            guided_state.current_peak_raw = 0u;
        }
        break;
    }
    case CALIBRATION_GUIDED_WAIT_MAX_PRESS:
        calibration_render_single_key(guided_state.current_key, 200u, 0u, 0u);
        break;
    case CALIBRATION_GUIDED_TRACK_MAX_PRESS:
        calibration_render_single_key(guided_state.current_key, 255u, 64u, 64u);
        break;
    case CALIBRATION_GUIDED_COMPLETE: {
        uint8_t pulse = calibration_pulse_value(now_ms, 700u, 32u, 200u);
        calibration_render_all(0u, pulse, 0u);
        if ((now_ms - guided_state.phase_start_ms) >= CALIBRATION_GUIDED_DONE_DURATION_MS) {
            memset(&guided_state, 0, sizeof(guided_state));
            calibration_restore_runtime_led_state();
        }
        break;
    }
    case CALIBRATION_GUIDED_ERROR: {
        uint8_t pulse = calibration_pulse_value(now_ms, 700u, 24u, 180u);
        calibration_render_all(pulse, 48u, 0u);
        if ((now_ms - guided_state.phase_start_ms) >= CALIBRATION_GUIDED_DONE_DURATION_MS) {
            memset(&guided_state, 0, sizeof(guided_state));
            calibration_restore_runtime_led_state();
        }
        break;
    }
    default:
        break;
    }
}

void calibration_guided_on_scan(uint32_t now_ms) {
    if (!guided_state.active) {
        return;
    }

    guided_state.last_tick_ms = now_ms;

    if (guided_state.phase_start_ms == 0u) {
        guided_state.phase_start_ms = now_ms;
    }

    if (guided_state.phase == CALIBRATION_GUIDED_ZERO_AVERAGE) {
        guided_state.sample_count++;
        for (uint8_t key = 0; key < NUM_KEYS; key++) {
            guided_state.zero_accumulator[key] += analog_read_raw_value(key);
        }
        return;
    }

    if (guided_state.phase != CALIBRATION_GUIDED_WAIT_MAX_PRESS &&
        guided_state.phase != CALIBRATION_GUIDED_TRACK_MAX_PRESS) {
        return;
    }

    settings_calibration_t calibration = *settings_get_calibration();
    uint8_t key = guided_state.current_key;
    if (key >= NUM_KEYS) {
        calibration_finish_session(false);
        return;
    }

    uint16_t raw_value = analog_read_raw_value(key);
    uint16_t zero_value = (uint16_t)calibration.key_zero_values[key];

    if (guided_state.phase == CALIBRATION_GUIDED_WAIT_MAX_PRESS) {
        if (raw_value > (uint16_t)(zero_value + CALIBRATION_GUIDED_PRESS_THRESHOLD_ADC)) {
            guided_state.phase = CALIBRATION_GUIDED_TRACK_MAX_PRESS;
            guided_state.current_key_pressed = true;
            guided_state.current_peak_raw = raw_value;
            guided_state.phase_start_ms = now_ms;
        }
        return;
    }

    if (raw_value > guided_state.current_peak_raw) {
        guided_state.current_peak_raw = raw_value;
    }

    if (raw_value <= (uint16_t)(zero_value + CALIBRATION_GUIDED_RELEASE_THRESHOLD_ADC)) {
        uint16_t captured_max = guided_state.current_peak_raw;
        if (captured_max <= zero_value) {
            captured_max = (uint16_t)(zero_value + CALIBRATION_GUIDED_PRESS_THRESHOLD_ADC);
        }
        calibration.key_max_values[key] = (int16_t)captured_max;
        if (!settings_set_calibration(&calibration)) {
            calibration_finish_session(false);
            return;
        }
        calibration_load_settings();

        guided_state.current_key++;
        guided_state.current_key_pressed = false;
        guided_state.current_peak_raw = 0u;
        guided_state.phase_start_ms = now_ms;

        if (guided_state.current_key >= NUM_KEYS) {
            bool saved = settings_save();
            calibration_finish_session(saved);
            guided_state.phase_start_ms = now_ms;
        } else {
            guided_state.phase = CALIBRATION_GUIDED_WAIT_MAX_PRESS;
        }
    }
}

void calibration_guided_get_status(calibration_guided_status_t *status) {
    uint32_t elapsed_ms = 0u;

    if (status == NULL) {
        return;
    }

    memset(status, 0, sizeof(*status));
    status->active = guided_state.active ? 1u : 0u;
    status->phase = (uint8_t)guided_state.phase;
    status->current_key = guided_state.current_key;
    status->sample_count = guided_state.sample_count;

    if (!guided_state.active) {
        return;
    }

    if (guided_state.last_tick_ms >= guided_state.phase_start_ms) {
        elapsed_ms = guided_state.last_tick_ms - guided_state.phase_start_ms;
    }
    status->phase_elapsed_ms =
        (uint16_t)((elapsed_ms > 0xFFFFu) ? 0xFFFFu : elapsed_ms);

    if (guided_state.phase == CALIBRATION_GUIDED_ZERO_AVERAGE) {
        uint32_t clamped = (elapsed_ms > CALIBRATION_GUIDED_ZERO_DURATION_MS)
                               ? CALIBRATION_GUIDED_ZERO_DURATION_MS
                               : elapsed_ms;
        status->progress_percent =
            (uint8_t)((clamped * 100u) / CALIBRATION_GUIDED_ZERO_DURATION_MS);
    } else if (guided_state.phase == CALIBRATION_GUIDED_WAIT_MAX_PRESS ||
               guided_state.phase == CALIBRATION_GUIDED_TRACK_MAX_PRESS) {
        uint32_t completed = (uint32_t)guided_state.current_key * 100u;
        if (guided_state.phase == CALIBRATION_GUIDED_TRACK_MAX_PRESS) {
            completed += 50u;
        }
        status->progress_percent = (uint8_t)(completed / NUM_KEYS);
    } else if (guided_state.phase == CALIBRATION_GUIDED_COMPLETE) {
        status->progress_percent = 100u;
    } else if (guided_state.phase == CALIBRATION_GUIDED_ERROR) {
        status->progress_percent = 100u;
    }
}
