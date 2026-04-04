#include <stdint.h>
#include "board_config.h"

#define LUT_ZERO_VALUE 2117

void calibration_init(void);

void calibration_load_settings(void);

void calibration_key_set_zero_offset(uint8_t key, uint16_t zero_value);

uint16_t calibration_get_calibrated_value(uint8_t key, uint16_t adc_value);