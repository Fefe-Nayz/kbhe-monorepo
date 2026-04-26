#include "hid/mouse_hid.h"

#include "tusb.h"
#include "usb_descriptors.h"

#define MOUSE_HID_PULSE_QUEUE_CAPACITY 32u

typedef struct {
  uint8_t buttons;
  int8_t x;
  int8_t y;
  int8_t vertical;
  int8_t horizontal;
} mouse_hid_report_t;

static mouse_hid_report_t pulse_queue[MOUSE_HID_PULSE_QUEUE_CAPACITY];
static uint8_t pulse_head = 0u;
static uint8_t pulse_tail = 0u;
static uint8_t current_buttons = 0u;
static uint8_t last_sent_buttons = 0u;
static uint8_t state_report_buttons_in_flight = 0u;
static bool report_in_flight = false;
static bool state_report_in_flight = false;

static inline bool mouse_hid_pulse_queue_is_empty(void) {
  return pulse_head == pulse_tail;
}

static bool mouse_hid_pulse_queue_push(mouse_hid_report_t report) {
  uint8_t next_tail =
      (uint8_t)((pulse_tail + 1u) % MOUSE_HID_PULSE_QUEUE_CAPACITY);
  if (next_tail == pulse_head) {
    return false;
  }

  pulse_queue[pulse_tail] = report;
  pulse_tail = next_tail;
  return true;
}

static bool mouse_hid_pulse_queue_pop(mouse_hid_report_t *report) {
  if (report == NULL || mouse_hid_pulse_queue_is_empty()) {
    return false;
  }

  *report = pulse_queue[pulse_head];
  pulse_head = (uint8_t)((pulse_head + 1u) % MOUSE_HID_PULSE_QUEUE_CAPACITY);
  return true;
}

static bool mouse_hid_send_report(mouse_hid_report_t report) {
  return tud_hid_n_mouse_report(HID_ITF_MOUSE, 0, report.buttons, report.x,
                                report.y, report.vertical, report.horizontal);
}

static void mouse_hid_pump_queue(void) {
  mouse_hid_report_t report = {0};

  if (report_in_flight || !mouse_hid_is_ready()) {
    return;
  }

  if (mouse_hid_pulse_queue_pop(&report)) {
    if (mouse_hid_send_report(report)) {
      report_in_flight = true;
      state_report_in_flight = false;
    } else {
      pulse_head =
          (uint8_t)((pulse_head + MOUSE_HID_PULSE_QUEUE_CAPACITY - 1u) %
                    MOUSE_HID_PULSE_QUEUE_CAPACITY);
      pulse_queue[pulse_head] = report;
    }
    return;
  }

  if (current_buttons != last_sent_buttons) {
    report.buttons = current_buttons;
    if (mouse_hid_send_report(report)) {
      report_in_flight = true;
      state_report_in_flight = true;
      state_report_buttons_in_flight = current_buttons;
    }
  }
}

void mouse_hid_init(void) {
  pulse_head = 0u;
  pulse_tail = 0u;
  current_buttons = 0u;
  last_sent_buttons = 0u;
  state_report_buttons_in_flight = 0u;
  report_in_flight = false;
  state_report_in_flight = false;
}

bool mouse_hid_is_ready(void) {
  return tud_mounted() && tud_hid_n_ready(HID_ITF_MOUSE);
}

void mouse_hid_button_press(uint8_t button_mask) {
  uint8_t next_buttons = (uint8_t)(current_buttons | button_mask);
  if (next_buttons == current_buttons) {
    return;
  }

  current_buttons = next_buttons;
  mouse_hid_pump_queue();
}

void mouse_hid_button_release(uint8_t button_mask) {
  uint8_t next_buttons = (uint8_t)(current_buttons & (uint8_t)(~button_mask));
  if (next_buttons == current_buttons) {
    return;
  }

  current_buttons = next_buttons;
  mouse_hid_pump_queue();
}

bool mouse_hid_scroll(int8_t vertical, int8_t horizontal) {
  mouse_hid_report_t pulse = {0};
  mouse_hid_report_t release = {0};

  if ((vertical == 0) && (horizontal == 0)) {
    return false;
  }

  if (!tud_mounted()) {
    return false;
  }

  pulse.buttons = current_buttons;
  pulse.vertical = vertical;
  pulse.horizontal = horizontal;
  release.buttons = current_buttons;

  if (!mouse_hid_pulse_queue_push(pulse) || !mouse_hid_pulse_queue_push(release)) {
    return false;
  }

  mouse_hid_pump_queue();
  return true;
}

void mouse_hid_release_all(void) {
  pulse_head = 0u;
  pulse_tail = 0u;
  current_buttons = 0u;
  mouse_hid_pump_queue();
}

void mouse_hid_task(void) {
  mouse_hid_pump_queue();
}

void mouse_hid_on_report_complete(void) {
  report_in_flight = false;
  if (state_report_in_flight) {
    last_sent_buttons = state_report_buttons_in_flight;
    state_report_in_flight = false;
  }
  mouse_hid_pump_queue();
}
