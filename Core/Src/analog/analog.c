#include "analog/analog.h"
#include "analog/multiplexer.h"
#include "analog/filter.h"
#include "analog/lut.h"
#include "analog/calibration.h"
#include <stdbool.h>
#include <stdint.h>

static AnalogConfig_t analog_config;

static uint16_t adc_buffer[ADC_BUFFER_LENGTH] __attribute__((aligned(ADC_BUFFER_LENGTH * 2)));

static uint16_t raw_values[NUM_ANALOG_INPUTS];

static uint16_t filtered_values[NUM_KEYS];

static uint16_t calibrated_values[NUM_KEYS];

static uint16_t distance_values[NUM_KEYS];

static uint8_t current_mux_channel = 0;

static bool is_scan_complete = false;

static analog_task_monitor_t analog_task_monitor;

static inline bool analog_is_valid_physical_index(uint8_t index) {
    return index < NUM_ANALOG_INPUTS;
}

static inline uint32_t analog_cycles_to_us(uint32_t cycles) {
    if (SystemCoreClock == 0U) {
        return 0U;
    }

    return (uint32_t)(((uint64_t)cycles * 1000000ULL) / (uint64_t)SystemCoreClock);
}

/*
 * Mapping from logical key index (0-81) to physical key index (0-87)
 * For example logical key index 0 corresponds to physical key index 43
 */
static const uint8_t LOGICAL_KEY_INDEX_TO_PHYSICAL_INDEX[NUM_KEYS] = {
    41, 8, 19, 30, 33, 0, 11, 22,
    34, 1, 12, 23, 35, 24, 63, 85,
    74, 52, 55, 77, 66, 44, 56, 78,
    67, 57, 79, 68, 46, 40, 7, 18,
    29, 42, 9, 20, 31, 37, 4, 15,
    26, 36, 47, 25, 62, 84, 73, 51,
    64, 86, 75, 53, 59, 81, 70, 48,
    58, 69, 43, 10, 21, 32, 54, 39,
    6, 17, 28, 50, 71, 82, 60, 38,
    65, 87, 76, 61, 83, 72, 49, 27,
    16, 5
};

// static const uint8_t LOGICAL_KEY_INDEX_TO_PHYSICAL_INDEX[NUM_KEYS] = {
// 0, 8, 16, 24, 32, 40, 0, 0, 0, 0, 0, 0,
// 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
// 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
// 0, 0, 0, 0, 0, 0, 0, 0,0, 0, 0, 0, 0, 0, 0,
// 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
// 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
// 0, 0, 0, 0
// };

// static const uint8_t LOGICAL_KEY_INDEX_TO_PHYSICAL_INDEX[NUM_KEYS] = {
//     0, 8, 16, 24, 32, 40
// };

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

    // Initialize filtered values to 0
    for (uint8_t i = 0; i < NUM_KEYS; i++) {
        filtered_values[i] = 0;
        calibrated_values[i] = 0;
        distance_values[i] = 0;
    }

    analog_task_monitor.raw_us = 0;
    analog_task_monitor.filter_us = 0;
    analog_task_monitor.calibration_us = 0;
    analog_task_monitor.lut_us = 0;
    analog_task_monitor.store_us = 0;
    analog_task_monitor.key_min_us = 0;
    analog_task_monitor.key_max_us = 0;
    analog_task_monitor.key_avg_us = 0;
    analog_task_monitor.nonzero_keys = 0;
    analog_task_monitor.key_max_index = 0;
}

void analog_task() {
    const uint8_t* key_to_phys = LOGICAL_KEY_INDEX_TO_PHYSICAL_INDEX;
    uint32_t raw_cycles = 0;
    uint32_t filter_cycles = 0;
    uint32_t calibration_cycles = 0;
    uint32_t lut_cycles = 0;
    uint32_t store_cycles = 0;
    uint32_t key_sum_cycles = 0;
    uint32_t key_min_cycles = UINT32_MAX;
    uint32_t key_max_cycles = 0;
    uint8_t key_max_index = 0;
    uint16_t nonzero_keys = 0;

    for (uint8_t key = 0; key < NUM_KEYS; key++) {
        uint32_t key_start_cycles = DWT->CYCCNT;
        uint8_t physical_key_index = key_to_phys[key];

        uint32_t step_start_cycles = DWT->CYCCNT;
        uint16_t raw_value = analog_is_valid_physical_index(physical_key_index)
                                 ? raw_values[physical_key_index]
                                 : 0;
        raw_cycles += (DWT->CYCCNT - step_start_cycles);

        if (raw_value != 0) {
            nonzero_keys++;
        }

        step_start_cycles = DWT->CYCCNT;
        uint16_t filtered_value = filter_compute_next_filtered_value(key, raw_value);
        filter_cycles += (DWT->CYCCNT - step_start_cycles);

        step_start_cycles = DWT->CYCCNT;
        uint16_t calibrated_value = calibration_get_calibrated_value(key, filtered_value);
        calibration_cycles += (DWT->CYCCNT - step_start_cycles);

        step_start_cycles = DWT->CYCCNT;
        uint16_t distance_value = getDistanceFromVoltage(calibrated_value);
        lut_cycles += (DWT->CYCCNT - step_start_cycles);

        step_start_cycles = DWT->CYCCNT;
        filtered_values[key] = filtered_value;
        calibrated_values[key] = calibrated_value;
        distance_values[key] = distance_value;
        store_cycles += (DWT->CYCCNT - step_start_cycles);

        uint32_t key_cycles = DWT->CYCCNT - key_start_cycles;
        key_sum_cycles += key_cycles;

        if (key_cycles < key_min_cycles) {
            key_min_cycles = key_cycles;
        }

        if (key_cycles >= key_max_cycles) {
            key_max_cycles = key_cycles;
            key_max_index = key;
        }
    }

    analog_task_monitor.raw_us = (uint16_t)analog_cycles_to_us(raw_cycles);
    analog_task_monitor.filter_us = (uint16_t)analog_cycles_to_us(filter_cycles);
    analog_task_monitor.calibration_us = (uint16_t)analog_cycles_to_us(calibration_cycles);
    analog_task_monitor.lut_us = (uint16_t)analog_cycles_to_us(lut_cycles);
    analog_task_monitor.store_us = (uint16_t)analog_cycles_to_us(store_cycles);
    analog_task_monitor.key_min_us = (uint16_t)analog_cycles_to_us(key_min_cycles == UINT32_MAX ? 0 : key_min_cycles);
    analog_task_monitor.key_max_us = (uint16_t)analog_cycles_to_us(key_max_cycles);
    analog_task_monitor.key_avg_us = (uint16_t)analog_cycles_to_us(key_sum_cycles / NUM_KEYS);
    analog_task_monitor.nonzero_keys = nonzero_keys;
    analog_task_monitor.key_max_index = key_max_index;

    if (!filter_is_initialized()) {
        filter_set_initialized(true);
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

    if (!analog_is_valid_physical_index(physical_key_index)) {
        return 0;
    }

    // Return the raw value for the corresponding physical key index
    return raw_values[physical_key_index];
}

/*
 * Read the EMA-filtered ADC value for a key, before calibration/LUT.
 */
uint16_t analog_read_filtered_value(uint8_t key) {
    // Check if key is within valid range (0 to NUM_KEYS - 1)
    if (key >= NUM_KEYS) {
        return 0;
    }

    // Return the filtered value
    return filtered_values[key];
}

/*
 * Read the calibrated ADC value for a key, after EMA and calibration but
 * before LUT distance conversion.
 */
uint16_t analog_read_calibrated_value(uint8_t key) {
    if (key >= NUM_KEYS) {
        return 0;
    }

    return calibrated_values[key];
}

int16_t analog_read_distance_value(uint8_t key) {
    // Check if key is within valid range (0 to NUM_KEYS - 1)
    if (key >= NUM_KEYS) {
        return 0;
    }

    // Return the distance value
    return distance_values[key];
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

void analog_get_task_monitor(analog_task_monitor_t* monitor) {
    if (monitor == NULL) {
        return;
    }

    *monitor = analog_task_monitor;
}
