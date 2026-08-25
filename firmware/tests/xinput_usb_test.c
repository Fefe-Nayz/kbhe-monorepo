#include "../Core/Src/hid/xinput_usb.c"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define TEST_XINPUT_EP_IN 0x81u

static gamepad_report_t test_gamepad_report;
static xinput_report_t sent_reports[16];
static uint8_t sent_count;
static bool device_ready;

const gamepad_report_t *gamepad_hid_get_report(void) {
  return &test_gamepad_report;
}
gamepad_api_mode_t settings_get_gamepad_api_mode(void) {
  return GAMEPAD_API_XINPUT;
}
bool tud_ready(void) { return device_ready; }
bool usbd_edpt_busy(uint8_t rhport, uint8_t ep_addr) {
  (void)rhport;
  (void)ep_addr;
  return false;
}
bool usbd_edpt_xfer(uint8_t rhport, uint8_t ep_addr, uint8_t *buffer,
                    uint16_t total_bytes, bool in_isr) {
  (void)rhport;
  (void)in_isr;
  assert(ep_addr == TEST_XINPUT_EP_IN);
  assert(buffer != NULL);
  assert(total_bytes == sizeof(xinput_report_t));
  assert(sent_count < (uint8_t)(sizeof(sent_reports) / sizeof(sent_reports[0])));
  memcpy(&sent_reports[sent_count++], buffer, sizeof(xinput_report_t));
  return true;
}
bool usbd_open_edpt_pair(uint8_t rhport, const uint8_t *desc_ep,
                         uint8_t ep_count, uint8_t xfer_type,
                         uint8_t *ep_out, uint8_t *ep_in) {
  (void)rhport;
  (void)desc_ep;
  (void)ep_count;
  (void)xfer_type;
  *ep_out = 0x01u;
  *ep_in = TEST_XINPUT_EP_IN;
  return true;
}
const uint8_t *tu_desc_next(const uint8_t *desc) { return desc; }

static void reset_fixture(void) {
  memset(&test_gamepad_report, 0, sizeof(test_gamepad_report));
  memset(sent_reports, 0, sizeof(sent_reports));
  sent_count = 0u;
  device_ready = true;
  xinput_usb_init();
  s_ep_in = TEST_XINPUT_EP_IN;
  xinput_usb_task();
  assert(sent_count == 1u);
  assert(xinput_driver_xfer_cb(0u, TEST_XINPUT_EP_IN, XFER_RESULT_SUCCESS,
                               sizeof(xinput_report_t)));
}

static void test_failed_press_and_release_are_retried(void) {
  reset_fixture();
  test_gamepad_report.buttons = KBHE_GAMEPAD_BUTTON_BIT(KBHE_BUTTON_A);
  xinput_usb_task();
  assert(sent_count == 2u);
  assert((sent_reports[1].buttons & XINPUT_BUTTON_A) != 0u);

  assert(xinput_driver_xfer_cb(0u, TEST_XINPUT_EP_IN, XFER_RESULT_FAILED, 0u));
  xinput_usb_task();
  assert(sent_count == 3u);
  assert(memcmp(&sent_reports[1], &sent_reports[2], sizeof(xinput_report_t)) ==
         0);
  assert(xinput_driver_xfer_cb(0u, TEST_XINPUT_EP_IN, XFER_RESULT_SUCCESS,
                               sizeof(xinput_report_t)));

  test_gamepad_report.buttons = 0u;
  xinput_usb_task();
  assert(sent_count == 4u);
  assert(sent_reports[3].buttons == 0u);
  assert(xinput_driver_xfer_cb(0u, TEST_XINPUT_EP_IN, XFER_RESULT_FAILED, 0u));
  xinput_usb_task();
  assert(sent_count == 5u);
  assert(sent_reports[4].buttons == 0u);
}

static void test_reset_resends_held_state(void) {
  reset_fixture();
  test_gamepad_report.buttons = KBHE_GAMEPAD_BUTTON_BIT(KBHE_BUTTON_B);
  xinput_usb_task();
  assert(sent_count == 2u);
  assert(xinput_driver_xfer_cb(0u, TEST_XINPUT_EP_IN, XFER_RESULT_SUCCESS,
                               sizeof(xinput_report_t)));

  xinput_driver_reset(0u);
  s_ep_in = TEST_XINPUT_EP_IN;
  xinput_usb_task();
  assert(sent_count == 3u);
  assert((sent_reports[2].buttons & XINPUT_BUTTON_B) != 0u);
}

int main(void) {
  test_failed_press_and_release_are_retried();
  test_reset_resends_held_state();
  puts("xinput_usb_test: ok");
  return 0;
}
