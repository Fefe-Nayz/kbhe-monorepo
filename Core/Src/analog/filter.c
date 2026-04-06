#include "analog/filter.h"
#include "board_config.h"

static float values[NUM_KEYS];
static bool is_initialized = false;

void filter_init(void) {
    for (uint8_t i = 0; i < NUM_KEYS; i++) {
        values[i] = 0;
    }
}

uint16_t filter_compute_next_filtered_value(uint8_t key, uint16_t raw_value) {
    if (!is_initialized) {
        values[key] = raw_value;
        return raw_value;
    }

    float alpha = ALPHA_MIN;

    float previous_filtered_value = values[key];

    float error = (raw_value > previous_filtered_value) ? (raw_value - previous_filtered_value) : (previous_filtered_value - raw_value);

    if (error >= BYPASS_THRESHOLD) {
        values[key] = raw_value;
        return raw_value;
    }

    if (error > NOISE_BAND) {
        alpha = ALPHA_MIN + (error - NOISE_BAND) * ALPHA_COEFF;
    }

    float filtered_value = alpha * raw_value + (1 - alpha) * previous_filtered_value;

    values[key] = filtered_value;

    return (uint16_t) filtered_value;
}

bool filter_is_initialized(void) {
    return is_initialized;
}

void filter_set_initialized(bool initialized) {
    is_initialized = initialized;
}