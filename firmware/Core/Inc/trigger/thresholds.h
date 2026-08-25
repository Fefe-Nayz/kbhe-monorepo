#ifndef TRIGGER_THRESHOLDS_H_
#define TRIGGER_THRESHOLDS_H_

#include <stdbool.h>
#include <stdint.h>

/* Release cannot be deeper than actuation: that would make the two state
 * regions overlap. Equality is valid and intentionally preserved. */
static inline uint8_t trigger_threshold_sanitize_release_tenths(
    uint8_t actuation_point, uint8_t release_point) {
  return release_point > actuation_point ? actuation_point : release_point;
}

static inline uint16_t trigger_threshold_sanitize_release_um(
    uint16_t actuation_point, uint16_t release_point) {
  return release_point > actuation_point ? actuation_point : release_point;
}

static inline bool trigger_threshold_should_press(int16_t distance,
                                                  uint16_t actuation_point) {
  return distance >= (int16_t)actuation_point;
}

static inline bool trigger_threshold_should_release(int16_t distance,
                                                    uint16_t actuation_point,
                                                    uint16_t release_point) {
  /* With equal thresholds, assign the exact boundary to PRESSED. Otherwise a
   * sample exactly on the boundary would press and release on alternating
   * scans. Separated thresholds retain the existing inclusive release edge,
   * including a reachable 0.0 mm release point at calibrated rest. */
  if (release_point == actuation_point) {
    return distance < (int16_t)release_point;
  }

  return distance <= (int16_t)release_point;
}

#endif /* TRIGGER_THRESHOLDS_H_ */
