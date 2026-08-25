#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

#include "realtime_scan_policy.h"
#include "tusb_config.h"

static void test_completed_adc_scan_has_first_service_priority(void) {
  assert(kbhe_realtime_first_service(true) == KBHE_REALTIME_SERVICE_ADC);
  assert(kbhe_realtime_first_service(false) == KBHE_REALTIME_SERVICE_USB);
}

static void test_adc_completion_after_usb_preempts_best_effort(void) {
  assert(kbhe_realtime_after_usb(true) == KBHE_REALTIME_SERVICE_ADC);
  assert(kbhe_realtime_after_usb(false) == KBHE_REALTIME_SERVICE_BEST_EFFORT);
}

static void test_realtime_budget_and_tinyusb_backlog_are_explicit(void) {
  _Static_assert(KBHE_SCAN_DEADLINE_US == 125u,
                 "8 kHz requires a 125 us scan deadline");
  _Static_assert(CFG_TUD_TASK_QUEUE_SZ == 16u,
                 "TinyUSB event backlog characterization changed");
}

int main(void) {
  test_completed_adc_scan_has_first_service_priority();
  test_adc_completion_after_usb_preempts_best_effort();
  test_realtime_budget_and_tinyusb_backlog_are_explicit();
  puts("realtime_scan_policy_test: ok");
  return 0;
}
