#ifndef ROTARY_TAP_SCHEDULER_H_
#define ROTARY_TAP_SCHEDULER_H_

#include <stdbool.h>
#include <stdint.h>

#define ROTARY_TAP_SCHEDULER_HOLD_MS 2u
#define ROTARY_TAP_SCHEDULER_GAP_MS 1u
#define ROTARY_TAP_SCHEDULER_CAPACITY 16u

typedef struct {
  uint16_t keycode;
  uint8_t repeats;
} rotary_tap_scheduler_entry_t;

typedef struct {
  rotary_tap_scheduler_entry_t entries[ROTARY_TAP_SCHEDULER_CAPACITY];
  uint8_t head;
  uint8_t tail;
  uint8_t count;
  uint16_t active_keycode;
  uint32_t release_ms;
  uint32_t next_press_ms;
  bool active;
  bool gap_active;
} rotary_tap_scheduler_t;

typedef enum {
  ROTARY_TAP_EVENT_NONE = 0,
  ROTARY_TAP_EVENT_PRESS,
  ROTARY_TAP_EVENT_RELEASE,
} rotary_tap_event_t;

_Static_assert((ROTARY_TAP_SCHEDULER_CAPACITY &
                (ROTARY_TAP_SCHEDULER_CAPACITY - 1u)) == 0u,
               "rotary tap scheduler capacity must be a power of two");

static inline void
rotary_tap_scheduler_init(rotary_tap_scheduler_t *scheduler) {
  if (scheduler != 0) {
    *scheduler = (rotary_tap_scheduler_t){0};
  }
}

static inline bool
rotary_tap_scheduler_enqueue(rotary_tap_scheduler_t *scheduler,
                             uint16_t keycode) {
  uint8_t last_index = 0u;

  if (scheduler == 0 || keycode == 0u) {
    return false;
  }
  if (scheduler->count > 0u) {
    last_index = (uint8_t)((scheduler->tail - 1u) &
                           (ROTARY_TAP_SCHEDULER_CAPACITY - 1u));
    if (scheduler->entries[last_index].keycode == keycode &&
        scheduler->entries[last_index].repeats < UINT8_MAX) {
      scheduler->entries[last_index].repeats++;
      return true;
    }
  }
  if (scheduler->count >= ROTARY_TAP_SCHEDULER_CAPACITY) {
    return false;
  }

  scheduler->entries[scheduler->tail].keycode = keycode;
  scheduler->entries[scheduler->tail].repeats = 1u;
  scheduler->tail =
      (uint8_t)((scheduler->tail + 1u) &
                (ROTARY_TAP_SCHEDULER_CAPACITY - 1u));
  scheduler->count++;
  return true;
}

static inline bool rotary_tap_scheduler_deadline_reached(uint32_t now_ms,
                                                          uint32_t deadline) {
  return (int32_t)(now_ms - deadline) >= 0;
}

static inline rotary_tap_event_t
rotary_tap_scheduler_step(rotary_tap_scheduler_t *scheduler, uint32_t now_ms,
                          uint16_t *keycode_out) {
  if (scheduler == 0 || keycode_out == 0) {
    return ROTARY_TAP_EVENT_NONE;
  }
  *keycode_out = 0u;

  if (scheduler->active) {
    if (!rotary_tap_scheduler_deadline_reached(now_ms,
                                                scheduler->release_ms)) {
      return ROTARY_TAP_EVENT_NONE;
    }
    *keycode_out = scheduler->active_keycode;
    scheduler->active = false;
    scheduler->active_keycode = 0u;
    scheduler->gap_active = true;
    scheduler->next_press_ms = now_ms + ROTARY_TAP_SCHEDULER_GAP_MS;
    return ROTARY_TAP_EVENT_RELEASE;
  }

  if (scheduler->gap_active) {
    if (!rotary_tap_scheduler_deadline_reached(now_ms,
                                                scheduler->next_press_ms)) {
      return ROTARY_TAP_EVENT_NONE;
    }
    scheduler->gap_active = false;
  }
  if (scheduler->count == 0u) {
    return ROTARY_TAP_EVENT_NONE;
  }

  scheduler->active_keycode = scheduler->entries[scheduler->head].keycode;
  scheduler->entries[scheduler->head].repeats--;
  if (scheduler->entries[scheduler->head].repeats == 0u) {
    scheduler->head =
        (uint8_t)((scheduler->head + 1u) &
                  (ROTARY_TAP_SCHEDULER_CAPACITY - 1u));
    scheduler->count--;
  }
  scheduler->active = true;
  scheduler->release_ms = now_ms + ROTARY_TAP_SCHEDULER_HOLD_MS;
  *keycode_out = scheduler->active_keycode;
  return ROTARY_TAP_EVENT_PRESS;
}

#endif /* ROTARY_TAP_SCHEDULER_H_ */
