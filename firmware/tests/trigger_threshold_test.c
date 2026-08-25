#include "trigger/thresholds.h"

#include <assert.h>
#include <stdio.h>

static void test_sanitize_preserves_equal_thresholds(void) {
  assert(trigger_threshold_sanitize_release_tenths(12u, 12u) == 12u);
  assert(trigger_threshold_sanitize_release_um(1200u, 1200u) == 1200u);

  assert(trigger_threshold_sanitize_release_tenths(12u, 13u) == 12u);
  assert(trigger_threshold_sanitize_release_um(1200u, 1300u) == 1200u);
  assert(trigger_threshold_sanitize_release_tenths(12u, 11u) == 11u);
}

static void test_equal_boundary_has_one_stable_owner(void) {
  const uint16_t threshold = 1200u;
  bool pressed = false;

  if (trigger_threshold_should_press(1200, threshold)) {
    pressed = true;
  }
  assert(pressed);

  /* The same sample on the next scan must not immediately release the key. */
  if (trigger_threshold_should_release(1200, threshold, threshold)) {
    pressed = false;
  }
  assert(pressed);

  assert(!trigger_threshold_should_release(1201, threshold, threshold));
  assert(trigger_threshold_should_release(1199, threshold, threshold));
}

static void test_separated_thresholds_keep_inclusive_release(void) {
  assert(trigger_threshold_should_press(1200, 1200u));
  assert(!trigger_threshold_should_press(1199, 1200u));
  assert(trigger_threshold_should_release(1100, 1200u, 1100u));
  assert(!trigger_threshold_should_release(1101, 1200u, 1100u));

  /* A configured 0.0 mm release remains reachable at calibrated rest. */
  assert(trigger_threshold_should_release(0, 100u, 0u));
}

int main(void) {
  test_sanitize_preserves_equal_thresholds();
  test_equal_boundary_has_one_stable_owner();
  test_separated_thresholds_keep_inclusive_release();
  puts("trigger_threshold_test: ok");
  return 0;
}
