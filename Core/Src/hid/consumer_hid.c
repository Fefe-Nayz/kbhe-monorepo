#include "hid/consumer_hid.h"

#include "tusb.h"
#include "usb_descriptors.h"

static uint16_t pending_release_usage = 0u;

void consumer_hid_init(void) {
  pending_release_usage = 0u;
}

bool consumer_hid_is_ready(void) {
  return tud_mounted() && tud_hid_n_ready(HID_ITF_CONSUMER);
}

static bool consumer_hid_send_report(uint16_t usage_code) {
  return tud_hid_n_report(HID_ITF_CONSUMER, 0, &usage_code,
                          sizeof(usage_code));
}

bool consumer_hid_send_usage(uint16_t usage_code) {
  if (usage_code == 0u) {
    return false;
  }

  if (!consumer_hid_is_ready()) {
    return false;
  }

  if (pending_release_usage != 0u) {
    uint16_t release = 0u;
    if (!consumer_hid_send_report(release)) {
      return false;
    }
    pending_release_usage = 0u;
  }

  if (!consumer_hid_send_report(usage_code)) {
    return false;
  }

  pending_release_usage = usage_code;
  return true;
}

bool consumer_hid_volume_up(void) {
  return consumer_hid_send_usage(HID_USAGE_CONSUMER_VOLUME_INCREMENT);
}

bool consumer_hid_volume_down(void) {
  return consumer_hid_send_usage(HID_USAGE_CONSUMER_VOLUME_DECREMENT);
}

bool consumer_hid_play_pause(void) {
  return consumer_hid_send_usage(HID_USAGE_CONSUMER_PLAY_PAUSE);
}

void consumer_hid_task(void) {
  if (pending_release_usage == 0u) {
    return;
  }

  if (!consumer_hid_is_ready()) {
    return;
  }

  uint16_t release = 0u;
  if (consumer_hid_send_report(release)) {
    pending_release_usage = 0u;
  }
}

void consumer_hid_on_report_complete(void) {
  // The release is driven from consumer_hid_task() to keep the logic simple.
}
