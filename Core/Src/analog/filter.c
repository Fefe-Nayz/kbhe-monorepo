#include <stdint.h>
#include "analog/filter.h"

uint16_t compute_next_filtered_value(uint16_t new_raw_value, uint16_t previous_filtered_value) {
    float alpha = ALPHA_MIN;

    uint16_t error = (new_raw_value > previous_filtered_value) ? (new_raw_value - previous_filtered_value) : (previous_filtered_value - new_raw_value);

    if (error >= BYPASS_THRESHOLD) {
        return new_raw_value;
    }

    if (error > NOISE_BAND) {
        alpha = ALPHA_MIN + (error - NOISE_BAND) * ALPHA_COEFF;
    }

    uint16_t next_filtered_value = (uint16_t) (alpha * new_raw_value + (1 - alpha) * previous_filtered_value);

    return next_filtered_value;
}