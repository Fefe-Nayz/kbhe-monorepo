#include "trigger/transition_filter.h"

#include <assert.h>
#include <stdio.h>

static void test_requires_continuous_stability(void) {
  trigger_transition_filter_t filter;
  trigger_transition_filter_reset(&filter, 0u);

  assert(!trigger_transition_filter_is_stable(&filter, 1u, true, 20u, 100u));
  assert(!trigger_transition_filter_is_stable(&filter, 1u, true, 20u, 119u));
  trigger_transition_filter_cancel(&filter); /* signal returned to old state */
  assert(!trigger_transition_filter_is_stable(&filter, 1u, true, 20u, 120u));
  assert(!trigger_transition_filter_is_stable(&filter, 1u, true, 20u, 139u));
  assert(trigger_transition_filter_is_stable(&filter, 1u, true, 20u, 140u));
}

static void test_candidate_change_restarts_timer(void) {
  trigger_transition_filter_t filter;
  trigger_transition_filter_reset(&filter, 0u);
  assert(!trigger_transition_filter_is_stable(&filter, 1u, true, 5u, 10u));
  assert(!trigger_transition_filter_is_stable(&filter, 0u, true, 5u, 14u));
  assert(!trigger_transition_filter_is_stable(&filter, 0u, true, 5u, 18u));
  assert(trigger_transition_filter_is_stable(&filter, 0u, true, 5u, 19u));
}

static void test_timer_wrap_and_disabled_mode(void) {
  trigger_transition_filter_t filter;
  trigger_transition_filter_reset(&filter, 0u);
  assert(!trigger_transition_filter_is_stable(&filter, 1u, true, 4u,
                                               UINT32_MAX - 2u));
  assert(trigger_transition_filter_is_stable(&filter, 1u, true, 4u, 1u));
  trigger_transition_filter_reset(&filter, 0u);
  assert(trigger_transition_filter_is_stable(&filter, 1u, false, 20u, 0u));
}

int main(void) {
  test_requires_continuous_stability();
  test_candidate_change_restarts_timer();
  test_timer_wrap_and_disabled_mode();
  puts("transition_filter_test: ok");
  return 0;
}
