#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "hid/consumer_hid.h"
#include "hid/keyboard_hid.h"
#include "hid/keyboard_nkro_hid.h"
#include "hid/mouse_hid.h"
#include "hid/raw_hid.h"
#include "tusb.h"
#include "usb_descriptors.h"

#define TEST_REPORT_CAPACITY 512u
#define TEST_HID_INSTANCE_COUNT 6u

typedef struct {
  uint8_t instance;
  uint16_t len;
  uint8_t data[NKRO_REPORT_SIZE];
} captured_report_t;

static bool test_mounted = true;
static bool test_ready[TEST_HID_INSTANCE_COUNT];
static bool test_in_flight[TEST_HID_INSTANCE_COUNT];
static bool test_fail_next_submit[TEST_HID_INSTANCE_COUNT];
static uint32_t test_tick_ms = 0u;
static captured_report_t captured_reports[TEST_REPORT_CAPACITY];
static uint16_t captured_report_count = 0u;

static bool capture_report(uint8_t instance, const void *report, uint16_t len) {
  captured_report_t *captured = NULL;

  assert(instance < TEST_HID_INSTANCE_COUNT);
  assert(report != NULL);
  assert(len <= sizeof(captured_reports[0].data));

  if (test_fail_next_submit[instance]) {
    test_fail_next_submit[instance] = false;
    return false;
  }
  if (!test_mounted || !test_ready[instance] ||
      captured_report_count >= TEST_REPORT_CAPACITY) {
    return false;
  }

  captured = &captured_reports[captured_report_count++];
  memset(captured, 0, sizeof(*captured));
  captured->instance = instance;
  captured->len = len;
  memcpy(captured->data, report, len);
  test_ready[instance] = false;
  test_in_flight[instance] = true;
  return true;
}

bool tud_mounted(void) { return test_mounted; }

bool tud_hid_n_ready(uint8_t instance) {
  assert(instance < TEST_HID_INSTANCE_COUNT);
  return test_mounted && test_ready[instance] && !test_in_flight[instance];
}

uint8_t tud_hid_n_get_protocol(uint8_t instance) {
  (void)instance;
  return HID_PROTOCOL_REPORT;
}

bool tud_hid_n_keyboard_report(uint8_t instance, uint8_t report_id,
                               uint8_t modifier,
                               const uint8_t keycodes[6]) {
  hid_keyboard_report_t report = {0};
  (void)report_id;
  report.modifier = modifier;
  memcpy(report.keycode, keycodes, sizeof(report.keycode));
  return capture_report(instance, &report, sizeof(report));
}

bool tud_hid_n_report(uint8_t instance, uint8_t report_id,
                      const void *report, uint16_t len) {
  (void)report_id;
  return capture_report(instance, report, len);
}

uint32_t HAL_GetTick(void) { return test_tick_ms; }

void layout_refresh_output_routes(void) {}
void led_indicator_set_state(uint8_t led_state) { (void)led_state; }
void led_matrix_set_usb_suspend_state(bool suspended) { (void)suspended; }
void led_matrix_update(void) {}
bool settings_is_led_usb_suspend_rgb_off_enabled(void) { return false; }
void raw_hid_on_receive(const uint8_t *data, uint16_t len) {
  (void)data;
  (void)len;
}
void raw_hid_on_report_complete(void) {}
void consumer_hid_on_report_complete(void) {}
void consumer_hid_on_umount(void) {}
void mouse_hid_on_report_complete(void) {}
void mouse_hid_on_umount(void) {}

static void complete_report(uint8_t instance) {
  const captured_report_t *report = NULL;

  assert(instance < TEST_HID_INSTANCE_COUNT);
  assert(test_in_flight[instance]);
  assert(captured_report_count > 0u);
  report = &captured_reports[captured_report_count - 1u];
  assert(report->instance == instance);

  test_in_flight[instance] = false;
  test_ready[instance] = true;
  tud_hid_report_complete_cb(instance, report->data, report->len);
}

static void fail_report(uint8_t instance) {
  const captured_report_t *report = NULL;

  assert(instance < TEST_HID_INSTANCE_COUNT);
  assert(test_in_flight[instance]);
  assert(captured_report_count > 0u);
  report = &captured_reports[captured_report_count - 1u];
  assert(report->instance == instance);

  test_in_flight[instance] = false;
  test_ready[instance] = true;
  tud_hid_report_failed_cb(instance, HID_REPORT_TYPE_INPUT, report->data, 0u);
}

static void drain_reports(uint8_t instance) {
  uint16_t completed = 0u;
  while (test_in_flight[instance]) {
    assert(completed++ < 256u);
    complete_report(instance);
  }
}

static bool report_6kro_contains(const captured_report_t *report,
                                 uint8_t keycode) {
  const hid_keyboard_report_t *keyboard =
      (const hid_keyboard_report_t *)report->data;
  assert(report->instance == HID_ITF_KEYBOARD);
  assert(report->len == sizeof(*keyboard));
  for (uint8_t i = 0u; i < 6u; i++) {
    if (keyboard->keycode[i] == keycode) {
      return true;
    }
  }
  return false;
}

static bool report_6kro_is_neutral(const captured_report_t *report) {
  static const hid_keyboard_report_t neutral = {0};
  assert(report->len == sizeof(neutral));
  return memcmp(report->data, &neutral, sizeof(neutral)) == 0;
}

static uint8_t report_6kro_modifier(const captured_report_t *report) {
  const hid_keyboard_report_t *keyboard =
      (const hid_keyboard_report_t *)report->data;
  assert(report->len == sizeof(*keyboard));
  return keyboard->modifier;
}

static bool report_nkro_contains(const captured_report_t *report,
                                 uint8_t keycode) {
  const nkro_keyboard_report_t *keyboard =
      (const nkro_keyboard_report_t *)report->data;
  assert(report->instance == HID_ITF_NKRO);
  assert(report->len == sizeof(*keyboard));
  assert(keycode < 128u);
  return (keyboard->keys[keycode / 8u] & (uint8_t)(1u << (keycode % 8u))) !=
         0u;
}

static bool report_nkro_is_neutral(const captured_report_t *report) {
  static const nkro_keyboard_report_t neutral = {0};
  assert(report->len == sizeof(neutral));
  return memcmp(report->data, &neutral, sizeof(neutral)) == 0;
}

static void test_6kro_busy_press_release(void) {
  uint16_t start = captured_report_count;
  test_ready[HID_ITF_KEYBOARD] = false;

  keyboard_hid_key_press(HID_KEY_A);
  keyboard_hid_task();
  keyboard_hid_key_release(HID_KEY_A);
  keyboard_hid_task();
  assert(captured_report_count == start);

  test_ready[HID_ITF_KEYBOARD] = true;
  keyboard_hid_task();
  assert(captured_report_count == (uint16_t)(start + 1u));
  assert(report_6kro_contains(&captured_reports[start], HID_KEY_A));

  complete_report(HID_ITF_KEYBOARD);
  assert(captured_report_count == (uint16_t)(start + 2u));
  assert(report_6kro_is_neutral(&captured_reports[start + 1u]));
  complete_report(HID_ITF_KEYBOARD);
}

static void test_6kro_batches_same_scan_mutations(void) {
  uint16_t start = captured_report_count;
  test_ready[HID_ITF_KEYBOARD] = true;

  keyboard_hid_key_press(HID_KEY_A);
  keyboard_hid_key_press(HID_KEY_CONTROL_LEFT);
  keyboard_hid_task();
  assert(captured_report_count == (uint16_t)(start + 1u));
  assert(report_6kro_contains(&captured_reports[start], HID_KEY_A));
  assert(report_6kro_modifier(&captured_reports[start]) == 0x01u);
  complete_report(HID_ITF_KEYBOARD);

  keyboard_hid_key_release(HID_KEY_A);
  keyboard_hid_key_release(HID_KEY_CONTROL_LEFT);
  keyboard_hid_task();
  assert(captured_report_count == (uint16_t)(start + 2u));
  assert(report_6kro_is_neutral(&captured_reports[start + 1u]));
  complete_report(HID_ITF_KEYBOARD);
}

static void test_6kro_submit_failure_retry(void) {
  uint16_t start = captured_report_count;
  test_ready[HID_ITF_KEYBOARD] = true;
  test_fail_next_submit[HID_ITF_KEYBOARD] = true;

  keyboard_hid_key_press(HID_KEY_B);
  keyboard_hid_task();
  assert(captured_report_count == start);
  keyboard_hid_task();
  assert(captured_report_count == (uint16_t)(start + 1u));
  assert(report_6kro_contains(&captured_reports[start], HID_KEY_B));
  complete_report(HID_ITF_KEYBOARD);

  keyboard_hid_key_release(HID_KEY_B);
  keyboard_hid_task();
  assert(report_6kro_is_neutral(&captured_reports[captured_report_count - 1u]));
  complete_report(HID_ITF_KEYBOARD);
}

static void test_6kro_transfer_failure_retry(void) {
  uint16_t start = captured_report_count;
  uint32_t failed_before = keyboard_hid_get_transfer_failed_count();
  test_ready[HID_ITF_KEYBOARD] = true;

  keyboard_hid_key_press(HID_KEY_F);
  keyboard_hid_task();
  assert(captured_report_count == (uint16_t)(start + 1u));
  fail_report(HID_ITF_KEYBOARD);
  assert(captured_report_count == (uint16_t)(start + 2u));
  assert(report_6kro_contains(&captured_reports[start], HID_KEY_F));
  assert(report_6kro_contains(&captured_reports[start + 1u], HID_KEY_F));
  assert(keyboard_hid_get_transfer_failed_count() == failed_before + 1u);
  complete_report(HID_ITF_KEYBOARD);

  keyboard_hid_key_release(HID_KEY_F);
  keyboard_hid_task();
  complete_report(HID_ITF_KEYBOARD);
}

static void test_6kro_overflow_finishes_with_desired_resync(void) {
  uint32_t overflow_before = keyboard_hid_get_queue_overflow_count();
  test_ready[HID_ITF_KEYBOARD] = false;

  for (uint16_t i = 0u; i < 70u; i++) {
    keyboard_hid_key_press(HID_KEY_A);
    keyboard_hid_task();
    keyboard_hid_key_release(HID_KEY_A);
    keyboard_hid_task();
  }

  assert(keyboard_hid_get_queue_high_watermark() == 128u);
  assert(keyboard_hid_get_queue_overflow_count() > overflow_before);
  test_ready[HID_ITF_KEYBOARD] = true;
  keyboard_hid_task();
  drain_reports(HID_ITF_KEYBOARD);
  assert(report_6kro_is_neutral(
      &captured_reports[captured_report_count - 1u]));
}

static void test_6kro_unmount_resyncs_only_desired(void) {
  uint16_t start = 0u;
  test_ready[HID_ITF_KEYBOARD] = false;
  keyboard_hid_key_press(HID_KEY_C);
  keyboard_hid_task();
  keyboard_hid_key_release(HID_KEY_C);
  keyboard_hid_task();
  keyboard_hid_key_press(HID_KEY_D);
  keyboard_hid_task();

  test_mounted = false;
  tud_umount_cb();
  keyboard_hid_key_release(HID_KEY_D);
  keyboard_hid_key_press(HID_KEY_E);

  start = captured_report_count;
  test_mounted = true;
  test_ready[HID_ITF_KEYBOARD] = true;
  keyboard_hid_task();
  assert(captured_report_count == (uint16_t)(start + 1u));
  assert(report_6kro_contains(&captured_reports[start], HID_KEY_E));
  assert(!report_6kro_contains(&captured_reports[start], HID_KEY_C));
  assert(!report_6kro_contains(&captured_reports[start], HID_KEY_D));
  complete_report(HID_ITF_KEYBOARD);

  keyboard_hid_key_release(HID_KEY_E);
  keyboard_hid_task();
  complete_report(HID_ITF_KEYBOARD);
}

static void test_nkro_busy_press_release(void) {
  uint16_t start = captured_report_count;
  test_ready[HID_ITF_NKRO] = false;

  keyboard_nkro_hid_key_press(HID_KEY_A);
  keyboard_nkro_hid_task();
  keyboard_nkro_hid_key_release(HID_KEY_A);
  keyboard_nkro_hid_task();
  assert(captured_report_count == start);

  test_ready[HID_ITF_NKRO] = true;
  keyboard_nkro_hid_task();
  assert(captured_report_count == (uint16_t)(start + 1u));
  assert(report_nkro_contains(&captured_reports[start], HID_KEY_A));

  complete_report(HID_ITF_NKRO);
  assert(captured_report_count == (uint16_t)(start + 2u));
  assert(report_nkro_is_neutral(&captured_reports[start + 1u]));
  complete_report(HID_ITF_NKRO);
}

static void test_nkro_batches_same_scan_mutations(void) {
  uint16_t start = captured_report_count;
  const nkro_keyboard_report_t *report = NULL;
  test_ready[HID_ITF_NKRO] = true;

  keyboard_nkro_hid_key_press(HID_KEY_A);
  keyboard_nkro_hid_key_press(HID_KEY_CONTROL_LEFT);
  keyboard_nkro_hid_task();
  assert(captured_report_count == (uint16_t)(start + 1u));
  assert(report_nkro_contains(&captured_reports[start], HID_KEY_A));
  report = (const nkro_keyboard_report_t *)captured_reports[start].data;
  assert(report->modifier == 0x01u);
  complete_report(HID_ITF_NKRO);

  keyboard_nkro_hid_key_release(HID_KEY_A);
  keyboard_nkro_hid_key_release(HID_KEY_CONTROL_LEFT);
  keyboard_nkro_hid_task();
  assert(captured_report_count == (uint16_t)(start + 2u));
  assert(report_nkro_is_neutral(&captured_reports[start + 1u]));
  complete_report(HID_ITF_NKRO);
}

static void test_nkro_submit_failure_retry(void) {
  uint16_t start = captured_report_count;
  test_ready[HID_ITF_NKRO] = true;
  test_fail_next_submit[HID_ITF_NKRO] = true;

  keyboard_nkro_hid_key_press(HID_KEY_B);
  keyboard_nkro_hid_task();
  assert(captured_report_count == start);
  keyboard_nkro_hid_task();
  assert(captured_report_count == (uint16_t)(start + 1u));
  assert(report_nkro_contains(&captured_reports[start], HID_KEY_B));
  complete_report(HID_ITF_NKRO);

  keyboard_nkro_hid_key_release(HID_KEY_B);
  keyboard_nkro_hid_task();
  assert(report_nkro_is_neutral(&captured_reports[captured_report_count - 1u]));
  complete_report(HID_ITF_NKRO);
}

static void test_nkro_transfer_failure_retry(void) {
  uint16_t start = captured_report_count;
  uint32_t failed_before = keyboard_nkro_hid_get_transfer_failed_count();
  test_ready[HID_ITF_NKRO] = true;

  keyboard_nkro_hid_key_press(HID_KEY_F);
  keyboard_nkro_hid_task();
  assert(captured_report_count == (uint16_t)(start + 1u));
  fail_report(HID_ITF_NKRO);
  assert(captured_report_count == (uint16_t)(start + 2u));
  assert(report_nkro_contains(&captured_reports[start], HID_KEY_F));
  assert(report_nkro_contains(&captured_reports[start + 1u], HID_KEY_F));
  assert(keyboard_nkro_hid_get_transfer_failed_count() == failed_before + 1u);
  complete_report(HID_ITF_NKRO);

  keyboard_nkro_hid_key_release(HID_KEY_F);
  keyboard_nkro_hid_task();
  complete_report(HID_ITF_NKRO);
}

static void test_nkro_overflow_finishes_with_desired_resync(void) {
  uint32_t overflow_before = keyboard_nkro_hid_get_queue_overflow_count();
  test_ready[HID_ITF_NKRO] = false;

  for (uint16_t i = 0u; i < 70u; i++) {
    keyboard_nkro_hid_key_press(HID_KEY_A);
    keyboard_nkro_hid_task();
    keyboard_nkro_hid_key_release(HID_KEY_A);
    keyboard_nkro_hid_task();
  }

  assert(keyboard_nkro_hid_get_queue_high_watermark() == 128u);
  assert(keyboard_nkro_hid_get_queue_overflow_count() > overflow_before);
  test_ready[HID_ITF_NKRO] = true;
  keyboard_nkro_hid_task();
  drain_reports(HID_ITF_NKRO);
  assert(report_nkro_is_neutral(
      &captured_reports[captured_report_count - 1u]));
}

static void test_nkro_unmount_resyncs_only_desired(void) {
  uint16_t start = 0u;
  test_ready[HID_ITF_NKRO] = false;
  keyboard_nkro_hid_key_press(HID_KEY_C);
  keyboard_nkro_hid_task();
  keyboard_nkro_hid_key_release(HID_KEY_C);
  keyboard_nkro_hid_task();
  keyboard_nkro_hid_key_press(HID_KEY_D);
  keyboard_nkro_hid_task();

  test_mounted = false;
  tud_umount_cb();
  keyboard_nkro_hid_key_release(HID_KEY_D);
  keyboard_nkro_hid_key_press(HID_KEY_E);

  start = captured_report_count;
  test_mounted = true;
  test_ready[HID_ITF_NKRO] = true;
  keyboard_nkro_hid_task();
  assert(captured_report_count == (uint16_t)(start + 1u));
  assert(report_nkro_contains(&captured_reports[start], HID_KEY_E));
  assert(!report_nkro_contains(&captured_reports[start], HID_KEY_C));
  assert(!report_nkro_contains(&captured_reports[start], HID_KEY_D));
  complete_report(HID_ITF_NKRO);

  keyboard_nkro_hid_key_release(HID_KEY_E);
  keyboard_nkro_hid_task();
  complete_report(HID_ITF_NKRO);
}

static void test_nkro_fallback_drops_failed_non_neutral_head(void) {
  uint16_t start = captured_report_count;
  test_ready[HID_ITF_NKRO] = true;
  test_tick_ms += NKRO_ACTIVE_STALL_TIMEOUT_MS + 1u;

  keyboard_nkro_hid_key_press(HID_KEY_G);
  keyboard_nkro_hid_task();
  assert(captured_report_count == (uint16_t)(start + 1u));
  assert(report_nkro_contains(&captured_reports[start], HID_KEY_G));

  test_tick_ms += NKRO_ACTIVE_STALL_TIMEOUT_MS + 1u;
  keyboard_nkro_hid_task();
  assert(keyboard_nkro_hid_is_runtime_fallback_active());
  keyboard_nkro_hid_release_all();

  fail_report(HID_ITF_NKRO);
  assert(captured_report_count == (uint16_t)(start + 2u));
  assert(report_nkro_is_neutral(&captured_reports[start + 1u]));
  complete_report(HID_ITF_NKRO);
}

int main(void) {
  memset(test_ready, 0, sizeof(test_ready));
  memset(test_in_flight, 0, sizeof(test_in_flight));
  memset(test_fail_next_submit, 0, sizeof(test_fail_next_submit));

  test_6kro_busy_press_release();
  test_6kro_batches_same_scan_mutations();
  test_6kro_submit_failure_retry();
  test_6kro_transfer_failure_retry();
  test_6kro_overflow_finishes_with_desired_resync();
  test_6kro_unmount_resyncs_only_desired();
  test_nkro_busy_press_release();
  test_nkro_batches_same_scan_mutations();
  test_nkro_submit_failure_retry();
  test_nkro_transfer_failure_retry();
  test_nkro_overflow_finishes_with_desired_resync();
  test_nkro_unmount_resyncs_only_desired();
  test_nkro_fallback_drops_failed_non_neutral_head();
  return 0;
}
