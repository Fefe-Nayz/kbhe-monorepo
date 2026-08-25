#include "hid/mouse_hid.h"

#include "tusb.h"
#include "usb_descriptors.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
  uint8_t buttons;
  int8_t x;
  int8_t y;
  int8_t vertical;
  int8_t horizontal;
} captured_mouse_report_t;

static bool mounted;
static bool ready;
static captured_mouse_report_t sent_reports[32];
static uint8_t sent_count;

bool tud_mounted(void) { return mounted; }

bool tud_hid_n_ready(uint8_t instance) {
  assert(instance == HID_ITF_MOUSE);
  return ready;
}

bool tud_hid_n_mouse_report(uint8_t instance, uint8_t report_id,
                            uint8_t buttons, int8_t x, int8_t y,
                            int8_t vertical, int8_t horizontal) {
  assert(instance == HID_ITF_MOUSE);
  assert(report_id == 0u);
  assert(sent_count < (uint8_t)(sizeof(sent_reports) / sizeof(sent_reports[0])));
  sent_reports[sent_count++] = (captured_mouse_report_t){
      .buttons = buttons,
      .x = x,
      .y = y,
      .vertical = vertical,
      .horizontal = horizontal,
  };
  return true;
}

bool tud_hid_n_report(uint8_t instance, uint8_t report_id,
                      const void *report, uint16_t len) {
  (void)instance;
  (void)report_id;
  (void)report;
  (void)len;
  return false;
}

static void reset_fixture(void) {
  mounted = true;
  ready = true;
  sent_count = 0u;
  memset(sent_reports, 0, sizeof(sent_reports));
  mouse_hid_init();
}

static void test_failed_button_and_neutral_are_retried(void) {
  reset_fixture();
  mouse_hid_button_press(MOUSE_HID_BUTTON_LEFT);
  assert(sent_count == 1u && sent_reports[0].buttons == MOUSE_HID_BUTTON_LEFT);
  mouse_hid_on_report_failed();
  assert(sent_count == 2u && sent_reports[1].buttons == MOUSE_HID_BUTTON_LEFT);
  mouse_hid_on_report_complete();

  mouse_hid_button_release(MOUSE_HID_BUTTON_LEFT);
  assert(sent_count == 3u && sent_reports[2].buttons == 0u);
  mouse_hid_on_report_failed();
  assert(sent_count == 4u && sent_reports[3].buttons == 0u);
  mouse_hid_on_report_complete();
}

static void test_failed_scroll_pulse_and_neutral_are_retried(void) {
  reset_fixture();
  assert(mouse_hid_scroll(1, 0));
  assert(sent_count == 1u && sent_reports[0].vertical == 1);
  mouse_hid_on_report_failed();
  assert(sent_count == 2u && sent_reports[1].vertical == 1);
  mouse_hid_on_report_complete();
  assert(sent_count == 3u && sent_reports[2].vertical == 0);
  mouse_hid_on_report_failed();
  assert(sent_count == 4u && sent_reports[3].vertical == 0);
  mouse_hid_on_report_complete();
}

static void test_active_unmount_resynchronizes_held_button(void) {
  reset_fixture();
  mouse_hid_button_press(MOUSE_HID_BUTTON_RIGHT);
  assert(sent_count == 1u);

  mounted = false;
  mouse_hid_on_umount();
  mouse_hid_task();
  assert(sent_count == 1u);

  mounted = true;
  mouse_hid_task();
  assert(sent_count == 2u);
  assert(sent_reports[1].buttons == MOUSE_HID_BUTTON_RIGHT);
  mouse_hid_on_report_complete();
}

int main(void) {
  test_failed_button_and_neutral_are_retried();
  test_failed_scroll_pulse_and_neutral_are_retried();
  test_active_unmount_resynchronizes_held_button();
  puts("mouse_hid_test: ok");
  return 0;
}
