#include "hid/consumer_hid.h"

#include "tusb.h"
#include "usb_descriptors.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static bool mounted;
static bool ready;
static uint16_t sent_usages[160];
static uint16_t sent_count;

bool tud_mounted(void) { return mounted; }

bool tud_hid_n_ready(uint8_t instance) {
  assert(instance == HID_ITF_CONSUMER);
  return ready;
}

bool tud_hid_n_report(uint8_t instance, uint8_t report_id,
                      const void *report, uint16_t len) {
  assert(instance == HID_ITF_CONSUMER);
  assert(report_id == 0u);
  assert(report != NULL && len == sizeof(uint16_t));
  memcpy(&sent_usages[sent_count++], report, sizeof(uint16_t));
  return true;
}

static void reset_fixture(void) {
  mounted = true;
  ready = false;
  sent_count = 0u;
  memset(sent_usages, 0, sizeof(sent_usages));
  consumer_hid_init();
}

static void test_busy_endpoint_buffers_sixty_four_complete_taps(void) {
  reset_fixture();
  for (uint8_t i = 0u; i < 64u; i++) {
    assert(consumer_hid_volume_up());
  }
  assert(!consumer_hid_volume_up());

  ready = true;
  consumer_hid_task();
  assert(sent_count == 1u &&
         sent_usages[0] == HID_USAGE_CONSUMER_VOLUME_INCREMENT);
  for (uint16_t i = 1u; i < 128u; i++) {
    consumer_hid_on_report_complete();
  }
  assert(sent_count == 128u);
  for (uint16_t i = 0u; i < sent_count; i += 2u) {
    assert(sent_usages[i] == HID_USAGE_CONSUMER_VOLUME_INCREMENT);
    assert(sent_usages[i + 1u] == 0u);
  }
}

static void test_unmounted_usage_is_rejected(void) {
  reset_fixture();
  mounted = false;
  assert(!consumer_hid_mute());
}

static void test_failed_usage_and_neutral_are_retried(void) {
  reset_fixture();
  ready = true;
  assert(consumer_hid_volume_up());
  assert(sent_count == 1u);
  assert(sent_usages[0] == HID_USAGE_CONSUMER_VOLUME_INCREMENT);

  consumer_hid_on_report_failed();
  assert(sent_count == 2u);
  assert(sent_usages[1] == HID_USAGE_CONSUMER_VOLUME_INCREMENT);

  consumer_hid_on_report_complete();
  assert(sent_count == 3u);
  assert(sent_usages[2] == 0u);
  consumer_hid_on_report_failed();
  assert(sent_count == 4u);
  assert(sent_usages[3] == 0u);
  consumer_hid_on_report_complete();
}

static void test_active_unmount_preserves_transaction(void) {
  reset_fixture();
  ready = true;
  assert(consumer_hid_play_pause());
  assert(sent_count == 1u);

  mounted = false;
  consumer_hid_on_umount();
  consumer_hid_task();
  assert(sent_count == 1u);

  mounted = true;
  consumer_hid_task();
  assert(sent_count == 2u);
  assert(sent_usages[1] == HID_USAGE_CONSUMER_PLAY_PAUSE);
  consumer_hid_on_report_complete();
  assert(sent_count == 3u && sent_usages[2] == 0u);
  consumer_hid_on_report_complete();
}

int main(void) {
  test_busy_endpoint_buffers_sixty_four_complete_taps();
  test_unmounted_usage_is_rejected();
  test_failed_usage_and_neutral_are_retried();
  test_active_unmount_preserves_transaction();
  puts("consumer_hid_test: ok");
  return 0;
}
