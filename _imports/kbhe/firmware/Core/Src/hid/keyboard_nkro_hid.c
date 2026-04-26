/*
 * keyboard_nkro_hid.c
 * NKRO (N-Key Rollover) Keyboard HID implementation
 */

#include "hid/keyboard_nkro_hid.h"
#include "stm32f7xx_hal.h"
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

typedef enum {
  NKRO_RUNTIME_DISCONNECTED = 0,
  NKRO_RUNTIME_PENDING,
  NKRO_RUNTIME_ACTIVE,
  NKRO_RUNTIME_FALLBACK,
} nkro_runtime_state_t;

static nkro_runtime_state_t runtime_state = NKRO_RUNTIME_DISCONNECTED;
static uint32_t mount_timestamp_ms = 0u;
static uint32_t last_nkro_progress_ms = 0u;

static void keyboard_nkro_hid_update_runtime_state(void) {
  const bool mounted = tud_mounted();

  if (!mounted) {
    runtime_state = NKRO_RUNTIME_DISCONNECTED;
    mount_timestamp_ms = 0u;
    last_nkro_progress_ms = 0u;
    memset(&nkro_report, 0, sizeof(nkro_report));
    report_changed = false;
    return;
  }

  if (runtime_state == NKRO_RUNTIME_DISCONNECTED) {
    runtime_state = NKRO_RUNTIME_PENDING;
    mount_timestamp_ms = HAL_GetTick();
    last_nkro_progress_ms = mount_timestamp_ms;
    memset(&nkro_report, 0, sizeof(nkro_report));
    report_changed = false;
  }

  if (runtime_state == NKRO_RUNTIME_PENDING) {
    if (tud_hid_n_ready(HID_ITF_NKRO)) {
      runtime_state = NKRO_RUNTIME_ACTIVE;
      last_nkro_progress_ms = HAL_GetTick();
      return;
    }

    if ((HAL_GetTick() - mount_timestamp_ms) >= NKRO_ENUMERATION_TIMEOUT_MS) {
      runtime_state = NKRO_RUNTIME_FALLBACK;
      memset(&nkro_report, 0, sizeof(nkro_report));
      report_changed = false;
    }
  }

  if (runtime_state == NKRO_RUNTIME_ACTIVE) {
    uint32_t now_ms = HAL_GetTick();

    if (tud_hid_n_ready(HID_ITF_NKRO)) {
      last_nkro_progress_ms = now_ms;
      return;
    }

    if ((now_ms - last_nkro_progress_ms) >= NKRO_ACTIVE_STALL_TIMEOUT_MS) {
      runtime_state = NKRO_RUNTIME_FALLBACK;
      memset(&nkro_report, 0, sizeof(nkro_report));
      report_changed = false;
    }
  }
}

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
  if (runtime_state != NKRO_RUNTIME_ACTIVE) {
    return false;
  }

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
    last_nkro_progress_ms = HAL_GetTick();
    return true;
  }
  return false;
}

uint8_t keyboard_nkro_hid_get_modifier_state(void) {
  return nkro_report.modifier;
}

bool keyboard_nkro_hid_can_route_keycodes(void) {
  keyboard_nkro_hid_update_runtime_state();
  return runtime_state == NKRO_RUNTIME_ACTIVE;
}

bool keyboard_nkro_hid_is_runtime_fallback_active(void) {
  keyboard_nkro_hid_update_runtime_state();
  return runtime_state == NKRO_RUNTIME_FALLBACK;
}

void keyboard_nkro_hid_task(void) {
  keyboard_nkro_hid_update_runtime_state();
  keyboard_nkro_hid_send_report_if_changed();
}

void keyboard_nkro_hid_release_all(void) {
  memset(&nkro_report, 0, sizeof(nkro_report));
  report_changed = true;
}
