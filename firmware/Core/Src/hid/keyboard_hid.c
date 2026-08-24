/*
 * keyboard_hid.c
 * Implémentation des callbacks HID TinyUSB
 * et fonctions d'envoi de rapport clavier
 */

#include "hid/keyboard_hid.h"
#include "hid/consumer_hid.h"
#include "hid/mouse_hid.h"
#include "hid/keyboard_nkro_hid.h"
#include "led_indicator.h"
#include "led_matrix.h"
#include "settings.h"
#include "hid/raw_hid.h"
#include "tusb.h"
#include "usb_descriptors.h"
#include <string.h>

//--------------------------------------------------------------------+
// Variables internes
//--------------------------------------------------------------------+

/* Keep the complete desired set, not only the six boot-report slots.  This
 * allows a seventh held key to be promoted as soon as one of the first six is
 * released. */
#define KEYBOARD_HID_MAX_HELD_KEYS 128u
#define KEYBOARD_HID_REPORT_QUEUE_CAPACITY 129u

static uint8_t desired_modifier = 0u;
static bool desired_report_dirty = false;
static uint8_t pressed_keys[KEYBOARD_HID_MAX_HELD_KEYS] = {0};
static uint8_t num_pressed_keys = 0;
/* Multiple physical keys/macros may own the same HID usage. */
static uint8_t key_reference_counts[256] = {0};

/* Keep every observable keyboard state until TinyUSB confirms delivery. One
 * slot remains unused so 129 entries hold 128 complete snapshots. */
static hid_keyboard_report_t report_queue[KEYBOARD_HID_REPORT_QUEUE_CAPACITY];
static uint16_t report_queue_head = 0u;
static uint16_t report_queue_tail = 0u;
static bool report_in_flight = false;
static bool report_resync_required = false;
static uint32_t report_queue_overflow_count = 0u;
static uint16_t report_queue_high_watermark = 0u;
static uint32_t report_transfer_failed_count = 0u;

static inline bool keyboard_hid_is_modifier(uint8_t keycode) {
  return (keycode >= HID_KEY_CONTROL_LEFT) && (keycode <= HID_KEY_GUI_RIGHT);
}

static uint16_t keyboard_hid_queue_next(uint16_t index) {
  index++;
  return index >= KEYBOARD_HID_REPORT_QUEUE_CAPACITY ? 0u : index;
}

static bool keyboard_hid_queue_is_empty(void) {
  return report_queue_head == report_queue_tail;
}

static bool keyboard_hid_queue_is_full(void) {
  return keyboard_hid_queue_next(report_queue_tail) == report_queue_head;
}

static uint16_t keyboard_hid_queue_depth(void) {
  if (report_queue_tail >= report_queue_head) {
    return (uint16_t)(report_queue_tail - report_queue_head);
  }
  return (uint16_t)(KEYBOARD_HID_REPORT_QUEUE_CAPACITY -
                    (report_queue_head - report_queue_tail));
}

static void keyboard_hid_build_desired_report(hid_keyboard_report_t *report) {
  if (report == NULL) {
    return;
  }

  memset(report, 0, sizeof(*report));
  report->modifier = desired_modifier;
  for (uint8_t i = 0u; i < num_pressed_keys && i < 6u; i++) {
    report->keycode[i] = pressed_keys[i];
  }
}

static bool
keyboard_hid_queue_push_snapshot(const hid_keyboard_report_t *report) {
  uint16_t previous_tail = 0u;
  uint16_t depth = 0u;

  if (report == NULL) {
    return false;
  }

  if (!keyboard_hid_queue_is_empty()) {
    previous_tail = report_queue_tail == 0u
                        ? (KEYBOARD_HID_REPORT_QUEUE_CAPACITY - 1u)
                        : (uint16_t)(report_queue_tail - 1u);
    if (memcmp(&report_queue[previous_tail], report, sizeof(*report)) == 0) {
      return true;
    }
  }

  if (keyboard_hid_queue_is_full()) {
    report_queue_overflow_count++;
    report_resync_required = true;
    return false;
  }

  report_queue[report_queue_tail] = *report;
  report_queue_tail = keyboard_hid_queue_next(report_queue_tail);
  depth = keyboard_hid_queue_depth();
  if (depth > report_queue_high_watermark) {
    report_queue_high_watermark = depth;
  }
  return true;
}

static bool keyboard_hid_queue_desired_report(void) {
  hid_keyboard_report_t report = {0};

  if (!tud_mounted()) {
    report_resync_required = true;
    return true;
  }

  keyboard_hid_build_desired_report(&report);
  return keyboard_hid_queue_push_snapshot(&report);
}

static void keyboard_hid_try_queue_resync(void) {
  if (!report_resync_required || !tud_mounted() ||
      keyboard_hid_queue_is_full()) {
    return;
  }

  report_resync_required = false;
  if (!keyboard_hid_queue_desired_report()) {
    report_resync_required = true;
  }
}

/* Drop snapshots that have not reached the USB controller yet. An accepted
 * head report cannot be cancelled, so retain it and place the desired resync
 * directly behind it. */
static void keyboard_hid_discard_pending_reports(void) {
  if (report_in_flight && !keyboard_hid_queue_is_empty()) {
    report_queue_tail = keyboard_hid_queue_next(report_queue_head);
  } else {
    report_queue_head = 0u;
    report_queue_tail = 0u;
    report_in_flight = false;
  }
  report_resync_required = false;
}

static bool keyboard_hid_pump_queue(void) {
  const hid_keyboard_report_t *report = NULL;

  keyboard_hid_try_queue_resync();
  if (report_in_flight || keyboard_hid_queue_is_empty() || !tud_mounted() ||
      !tud_hid_n_ready(HID_ITF_KEYBOARD)) {
    return false;
  }

  report = &report_queue[report_queue_head];
  if (!tud_hid_n_keyboard_report(HID_ITF_KEYBOARD, 0u, report->modifier,
                                 report->keycode)) {
    return false;
  }

  report_in_flight = true;
  return true;
}

//--------------------------------------------------------------------+
// API Publique - Fonctions d'envoi de rapport clavier
//--------------------------------------------------------------------+

bool keyboard_hid_is_ready(void) {
  return tud_hid_n_ready(HID_ITF_KEYBOARD);
}

bool keyboard_hid_is_boot_protocol_active(void) {
  if (!tud_mounted()) {
    return false;
  }

  return tud_hid_n_get_protocol(HID_ITF_KEYBOARD) == HID_PROTOCOL_BOOT;
}

bool keyboard_hid_send_report(uint8_t modifier, const uint8_t keycodes[6]) {
  hid_keyboard_report_t report = {0};

  if (!tud_mounted()) {
    return false;
  }

  report.modifier = modifier;

  if (keycodes != NULL) {
    memcpy(report.keycode, keycodes, sizeof(report.keycode));
  }

  if (!keyboard_hid_queue_push_snapshot(&report)) {
    return false;
  }
  (void)keyboard_hid_pump_queue();
  return true;
}

bool keyboard_hid_press_key(uint8_t modifier, uint8_t keycode) {
  uint8_t keycodes[6] = {keycode, 0, 0, 0, 0, 0};
  return keyboard_hid_send_report(modifier, keycodes);
}

bool keyboard_hid_release_all(void) {
  bool queued = false;

  desired_modifier = 0u;
  desired_report_dirty = false;
  memset(pressed_keys, 0, sizeof(pressed_keys));
  memset(key_reference_counts, 0, sizeof(key_reference_counts));
  num_pressed_keys = 0u;
  keyboard_hid_discard_pending_reports();
  queued = keyboard_hid_queue_desired_report();
  (void)keyboard_hid_pump_queue();
  return queued;
}

void keyboard_hid_reset_state(void) {
  desired_modifier = 0u;
  desired_report_dirty = false;
  memset(pressed_keys, 0, sizeof(pressed_keys));
  memset(key_reference_counts, 0, sizeof(key_reference_counts));
  num_pressed_keys = 0u;
  keyboard_hid_discard_pending_reports();
  (void)keyboard_hid_queue_desired_report();
  (void)keyboard_hid_pump_queue();
}

void keyboard_hid_key_press(uint8_t keycode) {
  if (keycode == 0)
    return;

  if (key_reference_counts[keycode] == 0xFFu) {
    return;
  }
  key_reference_counts[keycode]++;
  if (key_reference_counts[keycode] > 1u) {
    return;
  }

  if (keyboard_hid_is_modifier(keycode)) {
    uint8_t modifier_mask = (uint8_t)(1u << (keycode - HID_KEY_CONTROL_LEFT));
    if ((desired_modifier & modifier_mask) == 0u) {
      desired_modifier |= modifier_mask;
      desired_report_dirty = true;
    }
    return;
  }

  // Track all logical keys; report generation selects the first six.
  if (num_pressed_keys < KEYBOARD_HID_MAX_HELD_KEYS) {
    pressed_keys[num_pressed_keys++] = keycode;
    desired_report_dirty = true;
  } else {
    key_reference_counts[keycode] = 0u;
  }
}

void keyboard_hid_key_release(uint8_t keycode) {
  if (keycode == 0)
    return;

  if (key_reference_counts[keycode] == 0u) {
    return;
  }
  key_reference_counts[keycode]--;
  if (key_reference_counts[keycode] > 0u) {
    return;
  }

  if (keyboard_hid_is_modifier(keycode)) {
    uint8_t modifier_mask = (uint8_t)(1u << (keycode - HID_KEY_CONTROL_LEFT));
    if ((desired_modifier & modifier_mask) != 0u) {
      desired_modifier &= (uint8_t)(~modifier_mask);
      desired_report_dirty = true;
    }
    return;
  }

  // Find and remove key
  for (uint8_t i = 0; i < num_pressed_keys; i++) {
    if (pressed_keys[i] == keycode) {
      // Shift remaining keys
      for (uint8_t j = i; j < num_pressed_keys - 1; j++) {
        pressed_keys[j] = pressed_keys[j + 1];
      }
      num_pressed_keys--;
      pressed_keys[num_pressed_keys] = 0;
      desired_report_dirty = true;
      return;
    }
  }
}

bool keyboard_hid_send_report_if_changed(void) {
  if (desired_report_dirty) {
    /* Batch every mutation from the published analog scan into one atomic HID
     * state. A full queue latches a final desired-state resync. */
    desired_report_dirty = false;
    (void)keyboard_hid_queue_desired_report();
  }
  return keyboard_hid_pump_queue();
}

uint8_t keyboard_hid_get_modifier_state(void) {
  return desired_modifier;
}

void keyboard_hid_task(void) {
  // Send pending report if any
  keyboard_hid_send_report_if_changed();
}

void keyboard_hid_on_umount(void) {
  /* An aborted transfer has no completion callback. Discard historical taps
   * and publish only the current desired state after re-enumeration. */
  report_queue_head = 0u;
  report_queue_tail = 0u;
  report_in_flight = false;
  desired_report_dirty = false;
  report_resync_required = true;
}

void keyboard_hid_on_report_complete(void) {
  if (!report_in_flight || keyboard_hid_queue_is_empty()) {
    return;
  }

  report_in_flight = false;
  report_queue_head = keyboard_hid_queue_next(report_queue_head);
  keyboard_hid_try_queue_resync();
  (void)keyboard_hid_pump_queue();
}

void keyboard_hid_on_report_failed(void) {
  if (!report_in_flight) {
    return;
  }

  report_in_flight = false;
  report_transfer_failed_count++;
  (void)keyboard_hid_pump_queue();
}

uint32_t keyboard_hid_get_queue_overflow_count(void) {
  return report_queue_overflow_count;
}

uint16_t keyboard_hid_get_queue_high_watermark(void) {
  return report_queue_high_watermark;
}

uint32_t keyboard_hid_get_transfer_failed_count(void) {
  return report_transfer_failed_count;
}

bool keyboard_hid_is_transport_idle(void) {
  if (!tud_mounted()) {
    return true;
  }
  return !desired_report_dirty && !report_in_flight &&
         keyboard_hid_queue_is_empty() && !report_resync_required;
}

//--------------------------------------------------------------------+
// TinyUSB HID Callbacks (requis par TinyUSB)
//--------------------------------------------------------------------+

// Invoked when device is mounted/configured.
void tud_mount_cb(void) { led_matrix_set_usb_suspend_state(false); }

// Invoked when device is unmounted.
void tud_umount_cb(void) {
  keyboard_hid_on_umount();
  keyboard_nkro_hid_on_umount();
  consumer_hid_on_umount();
  mouse_hid_on_umount();
  led_matrix_set_usb_suspend_state(false);
}

// Invoked when USB bus enters suspend.
void tud_suspend_cb(bool remote_wakeup_en) {
  (void)remote_wakeup_en;

  if (settings_is_led_usb_suspend_rgb_off_enabled()) {
    led_matrix_set_usb_suspend_state(true);
  }
}

// Invoked when USB bus resumes.
void tud_resume_cb(void) { led_matrix_set_usb_suspend_state(false); }

/*
 * Invoked when received GET_REPORT control request
 * Application must fill buffer report's content and return its length.
 * Return zero will cause the stack to STALL request
 */
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                               hid_report_type_t report_type, uint8_t *buffer,
                               uint16_t reqlen) {
  uint16_t response_len = 0u;
  (void)report_id;

  if ((instance == HID_ITF_KEYBOARD) &&
      (report_type == HID_REPORT_TYPE_INPUT) && buffer != NULL &&
      reqlen > 0u) {
    hid_keyboard_report_t report = {0};
    // Retourner le rapport clavier courant
    keyboard_hid_build_desired_report(&report);
    response_len = reqlen < sizeof(report)
                       ? reqlen
                       : (uint16_t)sizeof(report);
    memcpy(buffer, &report, response_len);
    return response_len;
  }

  return 0;
}

/*
 * Invoked when received SET_REPORT control request or
 * received data on OUT endpoint (Report ID = 0, Type = OUTPUT)
 *
 * Pour un clavier, cela correspond aux LEDs (Caps Lock, Num Lock, etc.)
 */
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                           hid_report_type_t report_type, uint8_t const *buffer,
                           uint16_t bufsize) {
  (void)instance;
  (void)report_id;

  switch (instance) {
  case HID_ITF_KEYBOARD: // Keyboard
    if (report_type == HID_REPORT_TYPE_OUTPUT && buffer != NULL &&
        bufsize >= 1u) {
      uint8_t led_state = buffer[0];
      // Update lock-state tracking and refresh the matrix so the Caps Lock key
      // LED can be overridden immediately.
      led_indicator_set_state(led_state);
      led_matrix_update();
    }
    break;

  case HID_ITF_RAW_HID:
    raw_hid_on_receive(buffer, bufsize);
    break;

  case HID_ITF_CONSUMER:
    break;

  default:
    // Other instances if needed
    break;
  }
}

void tud_hid_set_protocol_cb(uint8_t instance, uint8_t protocol) {
  if ((instance == HID_ITF_KEYBOARD) && (protocol == HID_PROTOCOL_BOOT)) {
    // Ensure NKRO state is flushed when host requests legacy boot protocol.
    keyboard_nkro_hid_release_all();
  }
}

/*
 * Invoked when sent REPORT successfully to host
 * Application can use this to send the next report
 */
void tud_hid_report_complete_cb(uint8_t instance, uint8_t const *report,
                                uint16_t len) {
  (void)report;
  (void)len;

  switch (instance) {
  case HID_ITF_KEYBOARD:
    keyboard_hid_on_report_complete();
    break;

  case HID_ITF_NKRO:
    keyboard_nkro_hid_on_report_complete();
    break;

  case HID_ITF_RAW_HID:
    raw_hid_on_report_complete();
    break;

  case HID_ITF_CONSUMER:
    consumer_hid_on_report_complete();
    break;

  case HID_ITF_MOUSE:
    mouse_hid_on_report_complete();
    break;

  default:
    break;
  }
}

void tud_hid_report_failed_cb(uint8_t instance,
                              hid_report_type_t report_type,
                              uint8_t const *report,
                              uint16_t xferred_bytes) {
  (void)report;
  (void)xferred_bytes;

  if (report_type != HID_REPORT_TYPE_INPUT) {
    return;
  }

  switch (instance) {
  case HID_ITF_KEYBOARD:
    keyboard_hid_on_report_failed();
    break;

  case HID_ITF_NKRO:
    keyboard_nkro_hid_on_report_failed();
    break;

  default:
    break;
  }
}
