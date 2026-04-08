#include "analog/filter.h"
#include "board_config.h"

static uint32_t values_q[NUM_KEYS];
static uint16_t alpha_lut_q[BYPASS_THRESHOLD + 1];
static bool is_initialized = false;
static bool alpha_lut_ready = false;

static void filter_prepare_alpha_lut(void) {
    for (uint16_t i = 0; i <= BYPASS_THRESHOLD; i++) {
        if (i <= NOISE_BAND) {
            alpha_lut_q[i] = ALPHA_MIN_Q;
            continue;
        }

        uint32_t step = i - NOISE_BAND;
        uint32_t alpha = ALPHA_MIN_Q +
                         ((step * (ALPHA_MAX_Q - ALPHA_MIN_Q) + (ALPHA_RAMP_DENOM / 2U)) /
                          ALPHA_RAMP_DENOM);
        if (alpha > ALPHA_MAX_Q) {
            alpha = ALPHA_MAX_Q;
        }
        alpha_lut_q[i] = (uint16_t)alpha;
    }

    alpha_lut_ready = true;
}

void filter_init(void) {
    for (uint8_t i = 0; i < NUM_KEYS; i++) {
        values_q[i] = 0;
    }

    filter_prepare_alpha_lut();
}

uint16_t filter_compute_next_filtered_value(uint8_t key, uint16_t raw_value) {
    if (!alpha_lut_ready) {
        filter_prepare_alpha_lut();
    }

    if (!is_initialized) {
        values_q[key] = ((uint32_t)raw_value << FILTER_Q_SHIFT);
        return raw_value;
    }

    uint32_t previous_filtered_q = values_q[key];
    uint32_t raw_q = ((uint32_t)raw_value << FILTER_Q_SHIFT);
    int32_t delta_q = (int32_t)raw_q - (int32_t)previous_filtered_q;
    uint32_t abs_delta_q = (uint32_t)(delta_q < 0 ? -delta_q : delta_q);

    if (abs_delta_q >= ((uint32_t)BYPASS_THRESHOLD << FILTER_Q_SHIFT)) {
        values_q[key] = raw_q;
        return raw_value;
    }

    uint16_t error_index = (uint16_t)(abs_delta_q >> FILTER_Q_SHIFT);
    uint16_t alpha_q = alpha_lut_q[error_index];

    int32_t correction_q = (alpha_q * delta_q) >> FILTER_Q_SHIFT;
    int32_t filtered_q = (int32_t)previous_filtered_q + correction_q;

    if (filtered_q < 0) {
        filtered_q = 0;
    }

    values_q[key] = (uint32_t)filtered_q;

    return (uint16_t)(values_q[key] >> FILTER_Q_SHIFT);
}

bool filter_is_initialized(void) {
    return is_initialized;
}

void filter_set_initialized(bool initialized) {
    is_initialized = initialized;
}