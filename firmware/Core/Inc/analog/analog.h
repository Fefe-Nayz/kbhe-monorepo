#include <stdbool.h>
#include <stdint.h>
#include "main.h"
#include "board_config.h"

// Analog configuration structure
typedef struct {
    // ADC
    ADC_HandleTypeDef* hadc;
} AnalogConfig_t;

typedef struct {
    uint16_t raw_us;
    uint16_t filter_us;
    uint16_t calibration_us;
    uint16_t lut_us;
    uint16_t store_us;
    uint16_t key_min_us;
    uint16_t key_max_us;
    uint16_t key_avg_us;
    uint16_t nonzero_keys;
    uint16_t key_max_index;
} analog_task_monitor_t;

void analog_init(AnalogConfig_t* config);

void analog_task();

void analog_set_multiplexer_channel(uint8_t channel);

bool analog_is_scan_complete(void);

void analog_set_scan_complete(bool complete);

/** Atomically consume an ADC/DMA fault reported by the HAL callback. */
bool analog_take_scan_fault(void);

/** Reset the ISR scan cursor and publication state after DMA has stopped. */
void analog_reset_scan_state(void);

uint16_t analog_read_raw_value(uint8_t key);

uint16_t analog_read_filtered_value(uint8_t key);

uint16_t analog_read_calibrated_value(uint8_t key);

int16_t analog_read_distance_value(uint8_t key);

/**
 * Return the calibrated logical travel used by trigger/gamepad code.
 *
 * The sensor LUT does not span the keyboard's full configured 4.00 mm
 * travel.  This accessor maps each key's zero..calibrated-maximum range to
 * the canonical logical travel, so thresholds at the end of travel remain
 * reachable and per-key maximum calibration actually affects input.
 */
int16_t analog_read_travel_distance_value(uint8_t key);

uint8_t analog_read_normalized_value(uint8_t key);

uint16_t* analog_get_adc_buffer_ptr(void);

void analog_get_task_monitor(analog_task_monitor_t* monitor);
