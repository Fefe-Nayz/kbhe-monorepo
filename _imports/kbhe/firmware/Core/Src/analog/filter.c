#include "analog/filter.h"
#include "board_config.h"
#include <stddef.h>

static uint32_t values_q[NUM_KEYS];
static uint16_t alpha_lut_q[FILTER_BYPASS_THRESHOLD + 1];
static bool is_initialized = false;
static bool alpha_lut_ready = false;
static bool filter_enabled = FILTER_DEFAULT_ENABLED != 0U;
static uint8_t filter_noise_band = FILTER_DEFAULT_NOISE_BAND;
static uint8_t filter_alpha_min_denom = FILTER_DEFAULT_ALPHA_MIN_DENOM;
static uint8_t filter_alpha_max_denom = FILTER_DEFAULT_ALPHA_MAX_DENOM;
static uint16_t filter_alpha_min_q = 0U;
static uint16_t filter_alpha_max_q = 0U;

static inline uint8_t clamp_u8(uint8_t value, uint8_t min_value,
                               uint8_t max_value) {
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static uint16_t denom_to_alpha_q(uint8_t denom) {
    denom = clamp_u8(denom, 1U, 255U);
    uint32_t alpha_q = (FILTER_Q_ONE + (uint32_t)(denom / 2U)) / (uint32_t)denom;
    if (alpha_q > FILTER_Q_ONE) {
        alpha_q = FILTER_Q_ONE;
    }
    return (uint16_t)alpha_q;
}

static void filter_prepare_alpha_lut(void) {
    if (filter_alpha_min_denom < filter_alpha_max_denom) {
        uint8_t temp = filter_alpha_min_denom;
        filter_alpha_min_denom = filter_alpha_max_denom;
        filter_alpha_max_denom = temp;
    }

    filter_alpha_min_q = denom_to_alpha_q(filter_alpha_min_denom);
    filter_alpha_max_q = denom_to_alpha_q(filter_alpha_max_denom);

    uint32_t ramp_span = (filter_noise_band > 0U) ? (uint32_t)(filter_noise_band * 7U) : 1U;
    if (ramp_span == 0U) {
        ramp_span = 1U;
    }

    for (uint16_t i = 0; i <= FILTER_BYPASS_THRESHOLD; i++) {
        if (i <= filter_noise_band) {
            alpha_lut_q[i] = filter_alpha_min_q;
            continue;
        }

        uint32_t step = i - filter_noise_band;
        uint32_t alpha =
            filter_alpha_min_q +
            ((step * (uint32_t)(filter_alpha_max_q - filter_alpha_min_q) + (ramp_span / 2U)) /
             ramp_span);
        if (alpha > filter_alpha_max_q) {
            alpha = filter_alpha_max_q;
        }
        alpha_lut_q[i] = (uint16_t)alpha;
    }

    alpha_lut_ready = true;
}

void filter_init(void) {
    filter_reset();
    filter_prepare_alpha_lut();
}

void filter_reset(void) {
    for (uint8_t i = 0; i < NUM_KEYS; i++) {
        values_q[i] = 0;
    }
    is_initialized = false;
}

uint16_t filter_compute_next_filtered_value(uint8_t key, uint16_t raw_value) {
    if (!alpha_lut_ready) {
        filter_prepare_alpha_lut();
    }

    if (!filter_enabled) {
        values_q[key] = ((uint32_t)raw_value << FILTER_Q_SHIFT);
        return raw_value;
    }

    if (!is_initialized) {
        values_q[key] = ((uint32_t)raw_value << FILTER_Q_SHIFT);
        return raw_value;
    }

    uint32_t previous_filtered_q = values_q[key];
    uint32_t raw_q = ((uint32_t)raw_value << FILTER_Q_SHIFT);
    int32_t delta_q = (int32_t)raw_q - (int32_t)previous_filtered_q;
    uint32_t abs_delta_q = (uint32_t)(delta_q < 0 ? -delta_q : delta_q);

    if (abs_delta_q >= ((uint32_t)FILTER_BYPASS_THRESHOLD << FILTER_Q_SHIFT)) {
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

bool filter_get_enabled(void) {
    return filter_enabled;
}

void filter_set_enabled(bool enabled) {
    bool new_enabled = enabled ? true : false;
    if (filter_enabled == new_enabled) {
        return;
    }

    filter_enabled = new_enabled;
    filter_reset();
}

void filter_get_params(uint8_t *noise_band, uint8_t *alpha_min_denom,
                       uint8_t *alpha_max_denom) {
    if (noise_band != NULL) {
        *noise_band = filter_noise_band;
    }
    if (alpha_min_denom != NULL) {
        *alpha_min_denom = filter_alpha_min_denom;
    }
    if (alpha_max_denom != NULL) {
        *alpha_max_denom = filter_alpha_max_denom;
    }
}

void filter_set_params(uint8_t noise_band, uint8_t alpha_min_denom,
                       uint8_t alpha_max_denom) {
    filter_noise_band = clamp_u8(noise_band, 1U, 255U);
    filter_alpha_min_denom = clamp_u8(alpha_min_denom, 1U, 255U);
    filter_alpha_max_denom = clamp_u8(alpha_max_denom, 1U, 255U);
    alpha_lut_ready = false;
    filter_prepare_alpha_lut();
}
