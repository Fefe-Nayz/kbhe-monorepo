#include "hid/gamepad_hid.h"

#include "layout/layout.h"
#include "settings.h"
#include "trigger/trigger.h"
#include "tusb.h"
#include "usb_descriptors.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define GAMEPAD_HID_REPORT_SIZE 14u
#define GAMEPAD_HID_BUTTON_BYTE 10u

static settings_t test_settings;
static gamepad_api_mode_t api_mode;
static bool mounted;
static bool ready;
static uint8_t sent_reports[16][GAMEPAD_HID_REPORT_SIZE];
static uint8_t sent_count;

const settings_t *settings_get(void) { return &test_settings; }
gamepad_api_mode_t settings_get_gamepad_api_mode(void) { return api_mode; }
uint8_t settings_gamepad_apply_curve(uint16_t distance_01mm) {
  if (distance_01mm >= GAMEPAD_CURVE_MAX_DISTANCE_01MM) {
    return 255u;
  }
  return (uint8_t)(((uint32_t)distance_01mm * 255u) /
                   GAMEPAD_CURVE_MAX_DISTANCE_01MM);
}
bool settings_is_key_curve_enabled(uint8_t key_index) {
  (void)key_index;
  return false;
}
uint8_t settings_apply_curve(uint8_t key_index, uint8_t input) {
  (void)key_index;
  return input;
}

uint8_t layout_get_active_layer_top(void) { return 0u; }
key_state_e trigger_get_key_state(uint8_t key) {
  (void)key;
  return RELEASED;
}
uint16_t trigger_get_distance_01mm(uint8_t key) {
  (void)key;
  return 0u;
}

bool tud_mounted(void) { return mounted; }
bool tud_hid_n_ready(uint8_t instance) {
  assert(instance == HID_ITF_GAMEPAD);
  return ready;
}
bool tud_hid_n_report(uint8_t instance, uint8_t report_id,
                      const void *report, uint16_t len) {
  assert(instance == HID_ITF_GAMEPAD);
  assert(report_id == 0u);
  assert(report != NULL);
  assert(len == GAMEPAD_HID_REPORT_SIZE);
  assert(sent_count < (uint8_t)(sizeof(sent_reports) / sizeof(sent_reports[0])));
  memcpy(sent_reports[sent_count++], report, len);
  return true;
}

bool tud_hid_n_mouse_report(uint8_t instance, uint8_t report_id,
                            uint8_t buttons, int8_t x, int8_t y,
                            int8_t vertical, int8_t horizontal) {
  (void)instance;
  (void)report_id;
  (void)buttons;
  (void)x;
  (void)y;
  (void)vertical;
  (void)horizontal;
  return false;
}

static void reset_fixture(void) {
  memset(&test_settings, 0, sizeof(test_settings));
  memset(sent_reports, 0, sizeof(sent_reports));
  api_mode = GAMEPAD_API_HID;
  mounted = true;
  ready = true;
  sent_count = 0u;
  gamepad_hid_init();
  gamepad_hid_task();
  assert(sent_count == 1u); /* Initial state is explicit after enumeration. */
  gamepad_hid_on_report_complete();
}

static void press_button_a(void) {
  gamepad_hid_custom_button_press(GAMEPAD_BUTTON_A);
  gamepad_hid_refresh_state();
  gamepad_hid_task();
}

static void test_failed_press_and_neutral_are_retried(void) {
  reset_fixture();
  press_button_a();
  assert(sent_count == 2u);
  assert((sent_reports[1][GAMEPAD_HID_BUTTON_BYTE] & 0x01u) != 0u);

  gamepad_hid_on_report_failed();
  gamepad_hid_task();
  assert(sent_count == 3u);
  assert(memcmp(sent_reports[1], sent_reports[2], GAMEPAD_HID_REPORT_SIZE) == 0);
  gamepad_hid_on_report_complete();

  gamepad_hid_custom_button_release(GAMEPAD_BUTTON_A);
  gamepad_hid_refresh_state();
  gamepad_hid_task();
  assert(sent_count == 4u);
  assert((sent_reports[3][GAMEPAD_HID_BUTTON_BYTE] & 0x01u) == 0u);
  gamepad_hid_on_report_failed();
  gamepad_hid_task();
  assert(sent_count == 5u);
  assert(memcmp(sent_reports[3], sent_reports[4], GAMEPAD_HID_REPORT_SIZE) == 0);
  gamepad_hid_on_report_complete();
}

static void test_active_unmount_resends_held_state(void) {
  reset_fixture();
  press_button_a();
  assert(sent_count == 2u);

  mounted = false;
  gamepad_hid_on_umount();
  gamepad_hid_task();
  assert(sent_count == 2u);

  mounted = true;
  gamepad_hid_task();
  assert(sent_count == 3u);
  assert((sent_reports[2][GAMEPAD_HID_BUTTON_BYTE] & 0x01u) != 0u);
  gamepad_hid_on_report_complete();
}

int main(void) {
  test_failed_press_and_neutral_are_retried();
  test_active_unmount_resends_held_state();
  puts("gamepad_hid_transport_test: ok");
  return 0;
}
