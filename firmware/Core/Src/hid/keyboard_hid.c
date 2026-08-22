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

// Rapport clavier courant
static hid_keyboard_report_t keyboard_report = {0};

/* Keep the complete desired set, not only the six boot-report slots.  This
 * allows a seventh held key to be promoted as soon as one of the first six is
 * released. */
#define KEYBOARD_HID_MAX_HELD_KEYS 128u
static uint8_t pressed_keys[KEYBOARD_HID_MAX_HELD_KEYS] = {0};
static uint8_t num_pressed_keys = 0;
/* Multiple physical keys/macros may own the same HID usage. */
static uint8_t key_reference_counts[256] = {0};

// Flag pour indiquer qu'un rapport a changé et doit être envoyé
static volatile bool report_changed = false;

// Flag pour indiquer qu'un rapport est prêt à être envoyé
static volatile bool report_pending = false;

static inline bool keyboard_hid_is_modifier(uint8_t keycode) {
  return (keycode >= HID_KEY_CONTROL_LEFT) && (keycode <= HID_KEY_GUI_RIGHT);
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
  if (!tud_hid_n_ready(HID_ITF_KEYBOARD)) {
    return false;
  }

  keyboard_report.modifier = modifier;
  keyboard_report.reserved = 0;

  if (keycodes != NULL) {
    memcpy(keyboard_report.keycode, keycodes, 6);
  } else {
    memset(keyboard_report.keycode, 0, 6);
  }

  // Use instance-specific function for keyboard (instance 0)
  return tud_hid_n_keyboard_report(
      HID_ITF_KEYBOARD, 0, keyboard_report.modifier, keyboard_report.keycode);
}

bool keyboard_hid_press_key(uint8_t modifier, uint8_t keycode) {
  uint8_t keycodes[6] = {keycode, 0, 0, 0, 0, 0};
  return keyboard_hid_send_report(modifier, keycodes);
}

bool keyboard_hid_release_all(void) {
  bool sent = false;

  /* Clear desired ownership first, then retain a pending neutral report if
   * the endpoint is momentarily busy. Discarding the state after a failed
   * immediate send can otherwise leave the host's last 6KRO report stuck. */
  memset(&keyboard_report, 0, sizeof(keyboard_report));
  memset(pressed_keys, 0, sizeof(pressed_keys));
  memset(key_reference_counts, 0, sizeof(key_reference_counts));
  num_pressed_keys = 0u;
  report_changed = true;

  if (tud_mounted() && tud_hid_n_ready(HID_ITF_KEYBOARD)) {
    sent = keyboard_hid_send_report(0u, NULL);
    if (sent) {
      report_changed = false;
    }
  }
  return sent;
}

void keyboard_hid_reset_state(void) {
  memset(&keyboard_report, 0, sizeof(keyboard_report));
  memset(pressed_keys, 0, sizeof(pressed_keys));
  memset(key_reference_counts, 0, sizeof(key_reference_counts));
  num_pressed_keys = 0;
  /* Reset is also a logical release. Keep it pending until the USB endpoint
   * accepts the neutral report. */
  report_changed = true;
  report_pending = false;
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
    if ((keyboard_report.modifier & modifier_mask) == 0u) {
      keyboard_report.modifier |= modifier_mask;
      report_changed = true;
    }
    return;
  }

  // Track all logical keys; report generation selects the first six.
  if (num_pressed_keys < KEYBOARD_HID_MAX_HELD_KEYS) {
    pressed_keys[num_pressed_keys++] = keycode;
    report_changed = true;
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
    if ((keyboard_report.modifier & modifier_mask) != 0u) {
      keyboard_report.modifier &= (uint8_t)(~modifier_mask);
      report_changed = true;
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
      report_changed = true;
      return;
    }
  }
}

bool keyboard_hid_send_report_if_changed(void) {
  if (!report_changed) {
    return false;
  }

  if (!tud_mounted() || !tud_hid_n_ready(HID_ITF_KEYBOARD)) {
    return false;
  }

  // Build keycodes array
  uint8_t keycodes[6] = {0};
  for (uint8_t i = 0; i < num_pressed_keys && i < 6; i++) {
    keycodes[i] = pressed_keys[i];
  }

  if (keyboard_hid_send_report(keyboard_report.modifier, keycodes)) {
    report_changed = false;
    return true;
  }
  return false;
}

uint8_t keyboard_hid_get_modifier_state(void) {
  return keyboard_report.modifier;
}

void keyboard_hid_task(void) {
  // Send pending report if any
  keyboard_hid_send_report_if_changed();
}

void keyboard_hid_on_umount(void) {
  /* Endpoint transfers can be aborted without report-complete. Keep logical
   * ownership and republish it when the host enumerates again. */
  report_pending = false;
  report_changed = true;
}

//--------------------------------------------------------------------+
// TinyUSB HID Callbacks (requis par TinyUSB)
//--------------------------------------------------------------------+

// Invoked when device is mounted/configured.
void tud_mount_cb(void) { led_matrix_set_usb_suspend_state(false); }

// Invoked when device is unmounted.
void tud_umount_cb(void) {
  keyboard_hid_on_umount();
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
    // Retourner le rapport clavier courant
    response_len = reqlen < sizeof(keyboard_report)
                       ? reqlen
                       : (uint16_t)sizeof(keyboard_report);
    memcpy(buffer, &keyboard_report, response_len);
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
    report_pending = false;
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
