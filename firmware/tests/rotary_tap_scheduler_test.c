#include "rotary_tap_scheduler.h"

#include <assert.h>
#include <stdio.h>

static rotary_tap_event_t step(rotary_tap_scheduler_t *scheduler,
                               uint32_t now_ms, uint16_t expected_keycode) {
  uint16_t keycode = 0u;
  rotary_tap_event_t event =
      rotary_tap_scheduler_step(scheduler, now_ms, &keycode);
  assert(keycode == expected_keycode);
  return event;
}

static void test_tap_has_report_visible_hold_and_neutral_gap(void) {
  rotary_tap_scheduler_t scheduler;
  rotary_tap_scheduler_init(&scheduler);
  assert(rotary_tap_scheduler_enqueue(&scheduler, 0x04u));
  assert(rotary_tap_scheduler_enqueue(&scheduler, 0x05u));

  assert(step(&scheduler, 100u, 0x04u) == ROTARY_TAP_EVENT_PRESS);
  assert(step(&scheduler, 101u, 0u) == ROTARY_TAP_EVENT_NONE);
  assert(step(&scheduler, 102u, 0x04u) == ROTARY_TAP_EVENT_RELEASE);
  assert(step(&scheduler, 102u, 0u) == ROTARY_TAP_EVENT_NONE);
  assert(step(&scheduler, 103u, 0x05u) == ROTARY_TAP_EVENT_PRESS);
  assert(step(&scheduler, 105u, 0x05u) == ROTARY_TAP_EVENT_RELEASE);
}

static void test_deadlines_are_wrap_safe(void) {
  rotary_tap_scheduler_t scheduler;
  rotary_tap_scheduler_init(&scheduler);
  assert(rotary_tap_scheduler_enqueue(&scheduler, 0x06u));
  assert(step(&scheduler, UINT32_MAX, 0x06u) == ROTARY_TAP_EVENT_PRESS);
  assert(step(&scheduler, 0u, 0u) == ROTARY_TAP_EVENT_NONE);
  assert(step(&scheduler, 1u, 0x06u) == ROTARY_TAP_EVENT_RELEASE);
}

static void test_repeated_detents_are_compacted_and_bounded(void) {
  rotary_tap_scheduler_t scheduler;
  rotary_tap_scheduler_init(&scheduler);

  for (uint8_t i = 0u; i < 64u; i++) {
    assert(rotary_tap_scheduler_enqueue(&scheduler, 0x07u));
  }
  assert(scheduler.count == 1u);
  assert(scheduler.entries[scheduler.head].repeats == 64u);

  rotary_tap_scheduler_init(&scheduler);
  for (uint8_t i = 0u; i < ROTARY_TAP_SCHEDULER_CAPACITY; i++) {
    assert(rotary_tap_scheduler_enqueue(&scheduler, (uint16_t)(1u + i)));
  }
  assert(!rotary_tap_scheduler_enqueue(&scheduler, 0x100u));
}

int main(void) {
  test_tap_has_report_visible_hold_and_neutral_gap();
  test_deadlines_are_wrap_safe();
  test_repeated_detents_are_compacted_and_bounded();
  puts("rotary_tap_scheduler_test: ok");
  return 0;
}
