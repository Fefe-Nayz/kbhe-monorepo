#include "hid/consumer_hid.h"

#include "tusb.h"
#include "usb_descriptors.h"

#define CONSUMER_HID_QUEUE_CAPACITY 128u

static uint16_t report_queue[CONSUMER_HID_QUEUE_CAPACITY];
static uint8_t queue_head = 0u;
static uint8_t queue_tail = 0u;
static bool report_in_flight = false;

static inline bool consumer_hid_queue_is_empty(void) {
  return queue_head == queue_tail;
}

static inline uint8_t consumer_hid_queue_free_slots(void) {
  if (queue_tail >= queue_head) {
    return (uint8_t)(CONSUMER_HID_QUEUE_CAPACITY - (queue_tail - queue_head) -
                     1u);
  }

  return (uint8_t)(queue_head - queue_tail - 1u);
}

static bool consumer_hid_queue_push(uint16_t usage_code) {
  uint8_t next_tail =
      (uint8_t)((queue_tail + 1u) % CONSUMER_HID_QUEUE_CAPACITY);
  if (next_tail == queue_head) {
    return false;
  }

  report_queue[queue_tail] = usage_code;
  queue_tail = next_tail;
  return true;
}

static bool consumer_hid_queue_pop(uint16_t *usage_code) {
  if (usage_code == NULL || consumer_hid_queue_is_empty()) {
    return false;
  }

  *usage_code = report_queue[queue_head];
  queue_head = (uint8_t)((queue_head + 1u) % CONSUMER_HID_QUEUE_CAPACITY);
  return true;
}

void consumer_hid_init(void) {
  queue_head = 0u;
  queue_tail = 0u;
  report_in_flight = false;
}

bool consumer_hid_is_ready(void) {
  return tud_mounted() && tud_hid_n_ready(HID_ITF_CONSUMER);
}

static bool consumer_hid_send_report(uint16_t usage_code) {
  return tud_hid_n_report(HID_ITF_CONSUMER, 0, &usage_code,
                          sizeof(usage_code));
}

static void consumer_hid_pump_queue(void) {
  uint16_t usage_code = 0u;

  if (report_in_flight || !consumer_hid_is_ready()) {
    return;
  }

  if (!consumer_hid_queue_pop(&usage_code)) {
    return;
  }

  if (consumer_hid_send_report(usage_code)) {
    report_in_flight = true;
  } else {
    queue_head =
        (uint8_t)((queue_head + CONSUMER_HID_QUEUE_CAPACITY - 1u) %
                  CONSUMER_HID_QUEUE_CAPACITY);
    report_queue[queue_head] = usage_code;
  }
}

bool consumer_hid_send_usage(uint16_t usage_code) {
  if (usage_code == 0u) {
    return false;
  }

  if (!tud_mounted()) {
    return false;
  }

  if (consumer_hid_queue_free_slots() < 2u) {
    return false;
  }

  if (!consumer_hid_queue_push(usage_code) || !consumer_hid_queue_push(0u)) {
    return false;
  }

  consumer_hid_pump_queue();
  return true;
}

bool consumer_hid_volume_up(void) {
  return consumer_hid_send_usage(HID_USAGE_CONSUMER_VOLUME_INCREMENT);
}

bool consumer_hid_volume_down(void) {
  return consumer_hid_send_usage(HID_USAGE_CONSUMER_VOLUME_DECREMENT);
}

bool consumer_hid_mute(void) {
#ifdef HID_USAGE_CONSUMER_MUTE
  return consumer_hid_send_usage(HID_USAGE_CONSUMER_MUTE);
#else
  return consumer_hid_send_usage(0x00E2u);
#endif
}

bool consumer_hid_play_pause(void) {
  return consumer_hid_send_usage(HID_USAGE_CONSUMER_PLAY_PAUSE);
}

void consumer_hid_task(void) {
  consumer_hid_pump_queue();
}

void consumer_hid_on_report_complete(void) {
  report_in_flight = false;
  consumer_hid_pump_queue();
}
