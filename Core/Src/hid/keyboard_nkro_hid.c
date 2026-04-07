/*
 * keyboard_nkro_hid.c
 * NKRO (N-Key Rollover) Keyboard HID implementation
 */

#include "hid/keyboard_nkro_hid.h"
#include "tusb.h"
#include "usb_descriptors.h"
#include <string.h>

//--------------------------------------------------------------------+
// Internal Variables
//--------------------------------------------------------------------+

// NKRO report
static nkro_keyboard_report_t nkro_report = {0};

// Flag indicating report has changed
static volatile bool report_changed = false;

//--------------------------------------------------------------------+
// Helper Functions
//--------------------------------------------------------------------+

static inline void set_key_bit(uint8_t keycode) {
  if (keycode < 128) {
    nkro_report.keys[keycode / 8] |= (1 << (keycode % 8));
  }
}

static inline void clear_key_bit(uint8_t keycode) {
  if (keycode < 128) {
    nkro_report.keys[keycode / 8] &= ~(1 << (keycode % 8));
  }
}

static inline bool get_key_bit(uint8_t keycode) {
  if (keycode < 128) {
    return (nkro_report.keys[keycode / 8] & (1 << (keycode % 8))) != 0;
  }
  return false;
}

//--------------------------------------------------------------------+
// Public API
//--------------------------------------------------------------------+

bool keyboard_nkro_hid_is_ready(void) { return tud_hid_n_ready(HID_ITF_NKRO); }

void keyboard_nkro_hid_key_press(uint8_t keycode) {
  if (keycode == 0)
    return;

  // Handle modifier keys (224-231)
  if (keycode >= 224 && keycode <= 231) {
    uint8_t modifier_bit = 1 << (keycode - 224);
    if (!(nkro_report.modifier & modifier_bit)) {
      nkro_report.modifier |= modifier_bit;
      report_changed = true;
    }
  } else if (keycode < 128) {
    // Regular key
    if (!get_key_bit(keycode)) {
      set_key_bit(keycode);
      report_changed = true;
    }
  }
}

void keyboard_nkro_hid_key_release(uint8_t keycode) {
  if (keycode == 0)
    return;

  // Handle modifier keys (224-231)
  if (keycode >= 224 && keycode <= 231) {
    uint8_t modifier_bit = 1 << (keycode - 224);
    if (nkro_report.modifier & modifier_bit) {
      nkro_report.modifier &= ~modifier_bit;
      report_changed = true;
    }
  } else if (keycode < 128) {
    // Regular key
    if (get_key_bit(keycode)) {
      clear_key_bit(keycode);
      report_changed = true;
    }
  }
}

bool keyboard_nkro_hid_send_report_if_changed(void) {
  if (!report_changed) {
    return false;
  }

  if (!tud_mounted() || !tud_hid_n_ready(HID_ITF_NKRO)) {
    return false;
  }

  // Send the NKRO report
  // Note: tud_hid_n_report sends raw report data
  if (tud_hid_n_report(HID_ITF_NKRO, 0, &nkro_report, sizeof(nkro_report))) {
    report_changed = false;
    return true;
  }
  return false;
}

void keyboard_nkro_hid_task(void) { keyboard_nkro_hid_send_report_if_changed(); }

void keyboard_nkro_hid_release_all(void) {
  memset(&nkro_report, 0, sizeof(nkro_report));
  report_changed = true;
}
