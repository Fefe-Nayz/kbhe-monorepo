/*
 * keyboard_hid.c
 * Implémentation des callbacks HID TinyUSB
 * et fonctions d'envoi de rapport clavier
 */

#include "hid/keyboard_hid.h"
#include "led_indicator.h"
#include "led_matrix.h"
#include "hid/raw_hid.h"
#include "tusb.h"
#include "usb_descriptors.h"
#include <string.h>

//--------------------------------------------------------------------+
// Variables internes
//--------------------------------------------------------------------+

// Rapport clavier courant
static hid_keyboard_report_t keyboard_report = {0};

// Buffer des touches actuellement pressées (max 6 comme HID standard)
static uint8_t pressed_keys[6] = {0};
static uint8_t num_pressed_keys = 0;

// Flag pour indiquer qu'un rapport a changé et doit être envoyé
static volatile bool report_changed = false;

// Flag pour indiquer qu'un rapport est prêt à être envoyé
static volatile bool report_pending = false;

//--------------------------------------------------------------------+
// API Publique - Fonctions d'envoi de rapport clavier
//--------------------------------------------------------------------+

bool keyboard_hid_is_ready(void) {
  return tud_hid_n_ready(HID_ITF_KEYBOARD);
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
  return keyboard_hid_send_report(0, NULL);
}

void keyboard_hid_key_press(uint8_t keycode) {
  if (keycode == 0)
    return;

  // Check if key is already pressed
  for (uint8_t i = 0; i < num_pressed_keys; i++) {
    if (pressed_keys[i] == keycode) {
      return; // Already pressed
    }
  }

  // Add key if space available
  if (num_pressed_keys < 6) {
    pressed_keys[num_pressed_keys++] = keycode;
    report_changed = true;
  }
}

void keyboard_hid_key_release(uint8_t keycode) {
  if (keycode == 0)
    return;

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

  if (keyboard_hid_send_report(0, keycodes)) {
    report_changed = false;
    return true;
  }
  return false;
}

void keyboard_hid_task(void) {
  // Send pending report if any
  keyboard_hid_send_report_if_changed();
}

//--------------------------------------------------------------------+
// TinyUSB HID Callbacks (requis par TinyUSB)
//--------------------------------------------------------------------+

/*
 * Invoked when received GET_REPORT control request
 * Application must fill buffer report's content and return its length.
 * Return zero will cause the stack to STALL request
 */
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                               hid_report_type_t report_type, uint8_t *buffer,
                               uint16_t reqlen) {
  (void)instance;
  (void)report_id;
  (void)reqlen;

  if (report_type == HID_REPORT_TYPE_INPUT) {
    // Retourner le rapport clavier courant
    memcpy(buffer, &keyboard_report, sizeof(keyboard_report));
    return sizeof(keyboard_report);
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
    if (report_type == HID_REPORT_TYPE_OUTPUT && bufsize >= 1) {
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

  default:
    // Other instances if needed
    break;
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

  default:
    break;
  }
}
