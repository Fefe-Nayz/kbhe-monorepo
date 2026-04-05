#include "analog/analog.h"
#include "analog/multiplexer.h"
#include <stdbool.h>
#include <stdint.h>

static AnalogConfig_t analog_config;

static uint16_t adc_buffer[ADC_BUFFER_LENGTH] __attribute__((aligned(ADC_BUFFER_LENGTH * 2)));

static uint16_t raw_values[NUM_ANALOG_INPUTS];

static uint8_t current_mux_channel = 0;

static bool is_scan_complete = false;

/*
 * Mapping from logical key index (0-81) to physical key index (0-87)
 * For example logical key index 0 corresponds to physical key index 43
 */
// static const uint8_t logical_key_index_to_physical_index[NUM_KEYS] = {
//     43, 10, 21, 32, 33, 0, 11, 22,
//     34, 1, 12, 23, 35, 24, 66, 88,
//     77, 55, 56, 78, 67, 45, 57, 79,
//     68, 58, 80, 69, 47, 42, 9, 20,
//     31, 40, 7, 18, 29, 27, 4, 15,
//     26, 36, 48, 25, 65, 87, 76, 54,
//     63, 85, 74, 52, 60, 82, 71, 49,
//     59, 70, 41, 8, 19, 30, 53, 39,
//     6, 17, 29, 51, 72, 83, 61, 38,
//     64, 86, 75, 62, 84, 73, 50, 27,
//     16, 5
// };

static const uint8_t LOGICAL_KEY_INDEX_TO_PHYSICAL_INDEX[NUM_KEYS] = {
    0, 8, 16, 24, 32, 40
};

void analog_init(AnalogConfig_t* config) {
    // Copy the configuration to the static variable
    analog_config = *config;

    // Initialize adc buffer to 0
    for (uint8_t i = 0; i < NUM_MUX; i++) {
        adc_buffer[i] = 0;
    }

    // Initialize raw values to 0
    for (uint8_t i = 0; i < NUM_ANALOG_INPUTS; i++) {
        raw_values[i] = 0;
    }
}

/*
 * Read the raw value for a key
 */
uint16_t analog_read_raw_value(uint8_t key) {
    // Check if key is within valid range (0 to NUM_KEYS - 1)
    if (key >= NUM_KEYS) {
        return 0;
    }

    // Get the physical key index from the logical key
    uint8_t physical_key_index = LOGICAL_KEY_INDEX_TO_PHYSICAL_INDEX[key];

    // Return the raw value for the corresponding physical key index
    return raw_values[physical_key_index];
}

/*
 * Read the filtered value for a key
 */
uint16_t analog_read_filtered_value(uint8_t key) {
    // Check if key is within valid range (0 to NUM_KEYS - 1)
    if (key >= NUM_KEYS) {
        return 0;
    }

    // Get the physical key index from the logical key
    uint8_t physical_key_index = LOGICAL_KEY_INDEX_TO_PHYSICAL_INDEX[key];

    // Return the filtered value for the corresponding physical key index
    return filtered_values[physical_key_index];
}

/*
 * ADC conversion complete callback
 * This function is called when the ADC conversion is completed
 */
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc) {
    // Check if the callback is for the correct ADC instance
    if (hadc->Instance != analog_config.hadc->Instance) {
        return;
    }

    // Store ADC buffer values into raw values
    for (uint8_t i = 0; i < NUM_MUX; i++) {
        uint8_t physical_index = i + (current_mux_channel * NUM_MUX);
        raw_values[physical_index] = adc_buffer[i];
    }

    // Increment MUX channel
    current_mux_channel++;

    // Reset to channel 0 if we've reached the end of the channels
    if (current_mux_channel >= NUM_MUX_CHANNELS) {
        current_mux_channel = 0;
        is_scan_complete = true;
    }

    // Select the next MUX channel
    multiplexer_select_mux_channel(current_mux_channel);

    // Trigger timer to restart ADC conversion after MUX settling time
    if (!is_scan_complete) {
        TIM4->SR = ~TIM_SR_UIF;
        TIM4->CNT = 0;
        TIM4->CR1 |= TIM_CR1_CEN;
    }
}

bool analog_is_scan_complete() {
    return is_scan_complete;
}

void analog_set_scan_complete(bool complete) {
    is_scan_complete = complete;
}

uint16_t* analog_get_adc_buffer_ptr(void) {
    return adc_buffer;
}