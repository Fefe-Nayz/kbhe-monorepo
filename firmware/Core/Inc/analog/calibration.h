#include <stdbool.h>
#include <stdint.h>
#include <stdbool.h>
#include "board_config.h"

// #define LUT_ZERO_VALUE 2117
#define LUT_ZERO_VALUE 2180

typedef enum {
    CALIBRATION_GUIDED_IDLE = 0,
    CALIBRATION_GUIDED_ZERO_AVERAGE = 1,
    CALIBRATION_GUIDED_WAIT_MAX_PRESS = 2,
    CALIBRATION_GUIDED_TRACK_MAX_PRESS = 3,
    CALIBRATION_GUIDED_COMPLETE = 4,
    CALIBRATION_GUIDED_ABORTED = 5,
    CALIBRATION_GUIDED_ERROR = 6,
} calibration_guided_phase_t;

typedef struct {
    uint8_t active;
    uint8_t phase;
    uint8_t current_key;
    uint8_t progress_percent;
    uint16_t sample_count;
    uint16_t phase_elapsed_ms;
    uint32_t last_tick_ms;
} calibration_guided_status_t;

void calibration_init(void);

void calibration_load_settings(void);

void calibration_key_set_zero_offset(uint8_t key, uint16_t zero_value);

void calibration_key_set_max_value(uint8_t key, uint16_t max_value);

uint16_t calibration_get_calibrated_value(uint8_t key, uint16_t adc_value);

uint16_t calibration_get_max_distance_um(uint8_t key);

uint8_t calibration_get_normalized_distance(uint8_t key, int16_t distance_um);

bool calibration_guided_start(void);

void calibration_guided_abort(void);

bool calibration_guided_is_active(void);

void calibration_guided_tick(uint32_t now_ms);

void calibration_guided_on_scan(uint32_t now_ms);

void calibration_guided_get_status(calibration_guided_status_t *status);
