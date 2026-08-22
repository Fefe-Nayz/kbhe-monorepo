#ifndef TRIGGER_TRANSITION_FILTER_H_
#define TRIGGER_TRANSITION_FILTER_H_

#include <stdbool.h>
#include <stdint.h>

typedef struct {
  uint32_t candidate_since_ms;
  uint8_t candidate_state;
  bool candidate_active;
} trigger_transition_filter_t;

static inline void
trigger_transition_filter_reset(trigger_transition_filter_t *filter,
                                uint8_t current_state) {
  filter->candidate_since_ms = 0u;
  filter->candidate_state = current_state;
  filter->candidate_active = false;
}

static inline void
trigger_transition_filter_cancel(trigger_transition_filter_t *filter) {
  filter->candidate_active = false;
}

/* Return true only after the same candidate state has remained requested for
 * the complete interval. Unsigned subtraction intentionally handles timer
 * wraparound. */
static inline bool trigger_transition_filter_is_stable(
    trigger_transition_filter_t *filter, uint8_t desired_state,
    bool enabled, uint8_t duration_ms, uint32_t now_ms) {
  if (!enabled || duration_ms == 0u) {
    return true;
  }

  if (!filter->candidate_active ||
      filter->candidate_state != desired_state) {
    filter->candidate_active = true;
    filter->candidate_state = desired_state;
    filter->candidate_since_ms = now_ms;
    return false;
  }

  return (uint32_t)(now_ms - filter->candidate_since_ms) >= duration_ms;
}

#endif /* TRIGGER_TRANSITION_FILTER_H_ */
