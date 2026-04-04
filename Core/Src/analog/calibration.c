#include <stdint.h>
#include <stddef.h>
#include "settings.h"
#include "analog/calibration.h"

/*
 * Offset value for each key when they're not pressed
 * Offset of +5 means the key zero ADC value is +5 pts higher than the LUT zero ADC value
 */
static int16_t zero_offset[NUM_KEYS];

void calibration_init() {
    // Initialize zero offsets to 0
    for (uint8_t i = 0; i < NUM_KEYS; i++) {
        zero_offset[i] = 0;
    }

    calibration_load_settings();
}

void calibration_load_settings() {
    const settings_calibration_t *cal = settings_get_calibration();

    if (cal == NULL) {
        return;
    }

    for (int i = 0; i < 6; ++i) {
        zero_offset[i] = cal->key_zero_values[i] - cal->lut_zero_value;
    }
}

void calibration_key_set_zero_offset(uint8_t key, uint16_t zero_value) {
    if (key >= NUM_KEYS) {
        return;
    }
    
    zero_offset[key] = (int16_t)(zero_value - LUT_ZERO_VALUE);
}

uint16_t calibration_get_calibrated_value(uint8_t key, uint16_t adc_value) {
    if (key >= NUM_KEYS) {
        return adc_value;
    }
    
    int16_t offset = zero_offset[key];

    if (adc_value - offset < 0) {
        return 0;
    }
    
    return (uint16_t) (adc_value - offset);
}