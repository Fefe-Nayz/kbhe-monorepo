/*
 * keyboard_nkro_hid.c
 * NKRO (N-Key Rollover) Keyboard HID implementation
 */

#include "hid/keyboard_nkro_hid.h"
#include "layout/layout.h"
#include "stm32f7xx_hal.h"
#include "tusb.h"
#include "usb_descriptors.h"
#include <string.h>

//--------------------------------------------------------------------+
// Internal Variables
//--------------------------------------------------------------------+

// NKRO report
static nkro_keyboard_report_t nkro_report = {0};
static uint8_t key_reference_counts[256] = {0};
static bool desired_report_dirty = false;

#define NKRO_HID_REPORT_QUEUE_CAPACITY 129u
static nkro_keyboard_report_t report_queue[NKRO_HID_REPORT_QUEUE_CAPACITY];
static uint16_t report_queue_head = 0u;
static uint16_t report_queue_tail = 0u;
static bool report_in_flight = false;
static bool report_resync_required = false;
static uint32_t report_queue_overflow_count = 0u;
static uint16_t report_queue_high_watermark = 0u;
static uint32_t report_transfer_failed_count = 0u;

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
    return;
  }

  if (runtime_state == NKRO_RUNTIME_DISCONNECTED) {
    runtime_state = NKRO_RUNTIME_PENDING;
    mount_timestamp_ms = HAL_GetTick();
    last_nkro_progress_ms = mount_timestamp_ms;
  }

  if (runtime_state == NKRO_RUNTIME_PENDING) {
    if (tud_hid_n_ready(HID_ITF_NKRO)) {
      runtime_state = NKRO_RUNTIME_ACTIVE;
      last_nkro_progress_ms = HAL_GetTick();
      return;
    }

    if ((HAL_GetTick() - mount_timestamp_ms) >= NKRO_ENUMERATION_TIMEOUT_MS) {
      runtime_state = NKRO_RUNTIME_FALLBACK;
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

static bool
keyboard_nkro_hid_report_is_neutral(const nkro_keyboard_report_t *report) {
  static const nkro_keyboard_report_t neutral = {0};
  return report != NULL && memcmp(report, &neutral, sizeof(neutral)) == 0;
}

static uint16_t keyboard_nkro_hid_queue_next(uint16_t index) {
  index++;
  return index >= NKRO_HID_REPORT_QUEUE_CAPACITY ? 0u : index;
}

static bool keyboard_nkro_hid_queue_is_empty(void) {
  return report_queue_head == report_queue_tail;
}

static bool keyboard_nkro_hid_queue_is_full(void) {
  return keyboard_nkro_hid_queue_next(report_queue_tail) == report_queue_head;
}

static uint16_t keyboard_nkro_hid_queue_depth(void) {
  if (report_queue_tail >= report_queue_head) {
    return (uint16_t)(report_queue_tail - report_queue_head);
  }
  return (uint16_t)(NKRO_HID_REPORT_QUEUE_CAPACITY -
                    (report_queue_head - report_queue_tail));
}

static bool keyboard_nkro_hid_queue_push_snapshot(
    const nkro_keyboard_report_t *report) {
  uint16_t previous_tail = 0u;
  uint16_t depth = 0u;

  if (report == NULL) {
    return false;
  }

  if (!keyboard_nkro_hid_queue_is_empty()) {
    previous_tail = report_queue_tail == 0u
                        ? (NKRO_HID_REPORT_QUEUE_CAPACITY - 1u)
                        : (uint16_t)(report_queue_tail - 1u);
    if (memcmp(&report_queue[previous_tail], report, sizeof(*report)) == 0) {
      return true;
    }
  }

  if (keyboard_nkro_hid_queue_is_full()) {
    report_queue_overflow_count++;
    report_resync_required = true;
    return false;
  }

  report_queue[report_queue_tail] = *report;
  report_queue_tail = keyboard_nkro_hid_queue_next(report_queue_tail);
  depth = keyboard_nkro_hid_queue_depth();
  if (depth > report_queue_high_watermark) {
    report_queue_high_watermark = depth;
  }
  return true;
}

static bool keyboard_nkro_hid_queue_desired_report(void) {
  if (!tud_mounted()) {
    report_resync_required = true;
    return true;
  }
  return keyboard_nkro_hid_queue_push_snapshot(&nkro_report);
}

static void keyboard_nkro_hid_try_queue_resync(void) {
  if (!report_resync_required || !tud_mounted() ||
      keyboard_nkro_hid_queue_is_full()) {
    return;
  }

  report_resync_required = false;
  if (!keyboard_nkro_hid_queue_desired_report()) {
    report_resync_required = true;
  }
}

static void keyboard_nkro_hid_discard_pending_reports(void) {
  if (report_in_flight && !keyboard_nkro_hid_queue_is_empty()) {
    report_queue_tail = keyboard_nkro_hid_queue_next(report_queue_head);
  } else {
    report_queue_head = 0u;
    report_queue_tail = 0u;
    report_in_flight = false;
  }
  report_resync_required = false;
}

static bool keyboard_nkro_hid_pump_queue(void) {
  const nkro_keyboard_report_t *report = NULL;

  keyboard_nkro_hid_try_queue_resync();
  if (report_in_flight || keyboard_nkro_hid_queue_is_empty()) {
    return false;
  }

  report = &report_queue[report_queue_head];
  if (runtime_state != NKRO_RUNTIME_ACTIVE &&
      !(runtime_state == NKRO_RUNTIME_FALLBACK &&
        keyboard_nkro_hid_report_is_neutral(report))) {
    return false;
  }

  if (!tud_mounted() || !tud_hid_n_ready(HID_ITF_NKRO)) {
    return false;
  }

  if (!tud_hid_n_report(HID_ITF_NKRO, 0u, report, sizeof(*report))) {
    return false;
  }

  report_in_flight = true;
  last_nkro_progress_ms = HAL_GetTick();
  return true;
}

//--------------------------------------------------------------------+
// Public API
//--------------------------------------------------------------------+

bool keyboard_nkro_hid_is_ready(void) { return tud_hid_n_ready(HID_ITF_NKRO); }

void keyboard_nkro_hid_key_press(uint8_t keycode) {
  if (keycode == 0)
    return;

  if (key_reference_counts[keycode] == 0xFFu) {
    return;
  }
  key_reference_counts[keycode]++;
  if (key_reference_counts[keycode] > 1u) {
    return;
  }

  // Handle modifier keys (224-231)
  if (keycode >= 224 && keycode <= 231) {
    uint8_t modifier_bit = 1 << (keycode - 224);
    if (!(nkro_report.modifier & modifier_bit)) {
      nkro_report.modifier |= modifier_bit;
      desired_report_dirty = true;
    }
  } else if (keycode < 128) {
    // Regular key
    if (!get_key_bit(keycode)) {
      set_key_bit(keycode);
      desired_report_dirty = true;
    }
  }
}

void keyboard_nkro_hid_key_release(uint8_t keycode) {
  if (keycode == 0)
    return;

  if (key_reference_counts[keycode] == 0u) {
    return;
  }
  key_reference_counts[keycode]--;
  if (key_reference_counts[keycode] > 0u) {
    return;
  }

  // Handle modifier keys (224-231)
  if (keycode >= 224 && keycode <= 231) {
    uint8_t modifier_bit = 1 << (keycode - 224);
    if (nkro_report.modifier & modifier_bit) {
      nkro_report.modifier &= ~modifier_bit;
      desired_report_dirty = true;
    }
  } else if (keycode < 128) {
    // Regular key
    if (get_key_bit(keycode)) {
      clear_key_bit(keycode);
      desired_report_dirty = true;
    }
  }
}

bool keyboard_nkro_hid_send_report_if_changed(void) {
  if (desired_report_dirty) {
    desired_report_dirty = false;
    (void)keyboard_nkro_hid_queue_desired_report();
  }
  return keyboard_nkro_hid_pump_queue();
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
  /* USB protocol/readiness can change without a key edge. Reconcile the
   * immutable per-press routes here so held keys migrate transactionally. */
  layout_refresh_output_routes();
  keyboard_nkro_hid_send_report_if_changed();
}

void keyboard_nkro_hid_release_all(void) {
  memset(&nkro_report, 0, sizeof(nkro_report));
  memset(key_reference_counts, 0, sizeof(key_reference_counts));
  desired_report_dirty = false;
  keyboard_nkro_hid_discard_pending_reports();
  (void)keyboard_nkro_hid_queue_desired_report();
  (void)keyboard_nkro_hid_pump_queue();
}

void keyboard_nkro_hid_on_umount(void) {
  runtime_state = NKRO_RUNTIME_DISCONNECTED;
  mount_timestamp_ms = 0u;
  last_nkro_progress_ms = 0u;
  report_queue_head = 0u;
  report_queue_tail = 0u;
  report_in_flight = false;
  desired_report_dirty = false;
  report_resync_required = true;
}

void keyboard_nkro_hid_on_report_complete(void) {
  if (!report_in_flight || keyboard_nkro_hid_queue_is_empty()) {
    return;
  }

  report_in_flight = false;
  report_queue_head = keyboard_nkro_hid_queue_next(report_queue_head);
  keyboard_nkro_hid_try_queue_resync();
  (void)keyboard_nkro_hid_pump_queue();
}

void keyboard_nkro_hid_on_report_failed(void) {
  if (!report_in_flight) {
    return;
  }

  report_in_flight = false;
  report_transfer_failed_count++;
  /* Once routing has fallen back to 6KRO, an old non-neutral NKRO report must
   * never be retried ahead of the queued neutral resync. The failed transfer
   * was not observed by the host, so it is safe to discard here. */
  if (runtime_state == NKRO_RUNTIME_FALLBACK &&
      !keyboard_nkro_hid_queue_is_empty() &&
      !keyboard_nkro_hid_report_is_neutral(
          &report_queue[report_queue_head])) {
    report_queue_head = keyboard_nkro_hid_queue_next(report_queue_head);
    keyboard_nkro_hid_try_queue_resync();
  }
  (void)keyboard_nkro_hid_pump_queue();
}

uint32_t keyboard_nkro_hid_get_queue_overflow_count(void) {
  return report_queue_overflow_count;
}

uint16_t keyboard_nkro_hid_get_queue_high_watermark(void) {
  return report_queue_high_watermark;
}

uint32_t keyboard_nkro_hid_get_transfer_failed_count(void) {
  return report_transfer_failed_count;
}
