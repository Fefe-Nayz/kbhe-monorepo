/*
 * offset.c
 * ADC offset calibration for hall effect sensors
 * Uses values from settings for per-key calibration
 */

#include "settings.h"
#include <stddef.h>

// Calculated offsets (computed from settings calibration)
static int OFFSET[6] = {0, 0, 0, 0, 0, 0};

// Initialize offsets from settings calibration values
void offsetInit(void) {
  const settings_calibration_t *cal = settings_get_calibration();
  if (cal == NULL) {
    return;
  }

  for (int i = 0; i < 6; ++i) {
    OFFSET[i] = cal->key_zero_values[i] - cal->lut_zero_value;
  }
}

// Get corrected ADC value with offset applied
int getCorrectedValue(int sensor_index, int voltage) {
  if (sensor_index < 0 || sensor_index >= 6) {
    return voltage;
  }
  return voltage - OFFSET[sensor_index];
}

// Recalculate offsets (call after settings calibration is updated)
void offsetRecalculate(void) { offsetInit(); }