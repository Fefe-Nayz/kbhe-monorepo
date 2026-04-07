/*
 * gamepad_hid.c
 * Implémentation du gamepad HID pour touches Hall Effect
 * Chaque touche est mappée sur un axe (X, Y, Z, Rx, Ry, Rz)
 */

#include "hid/gamepad_hid.h"
#include "settings.h"
#include "tusb.h"
#include "usb_descriptors.h"
#include <math.h>
#include <string.h>

//--------------------------------------------------------------------+
// Variables internes
//--------------------------------------------------------------------+

// Current gamepad report
static gamepad_report_t gamepad_report = {0};

// Previous report for change detection
static gamepad_report_t prev_gamepad_report = {0};

// Flag for report change detection
static volatile bool gamepad_report_changed = false;

// Gamepad enabled flag (default: enabled)
static bool gamepad_enabled = true;

// Raw axis values before processing (for snappy mode)
static float raw_axis_values[GAMEPAD_NUM_AXES] = {0};

//--------------------------------------------------------------------+
// Internal: Analog curve processing
//--------------------------------------------------------------------+

/**
 * Apply deadzone to a value
 * @param value Input value (0.0 - 1.0)
 * @param deadzone Deadzone threshold (0.0 - 1.0)
 * @return Processed value with deadzone applied
 */
static float apply_deadzone(float value, float deadzone) {
  if (value < deadzone) {
    return 0.0f;
  }
  // Rescale remaining range to 0-1
  return (value - deadzone) / (1.0f - deadzone);
}

/**
 * Apply analog curve
 * @param value Input value (0.0 - 1.0)
 * @param curve_type 0=linear, 1=smooth, 2=aggressive
 * @return Processed value
 */
static float apply_curve(float value, uint8_t curve_type) {
  switch (curve_type) {
  case 1: // Smooth (quadratic ease-in-out)
    return value * value;
  case 2: // Aggressive (cubic)
    return value * value * value;
  default: // Linear
    return value;
  }
}

//--------------------------------------------------------------------+
// Public API Implementation
//--------------------------------------------------------------------+

void gamepad_hid_init(void) {
  // Initialize all axes to center (released position)
  memset(&gamepad_report, 0, sizeof(gamepad_report));
  memset(&prev_gamepad_report, 0, sizeof(prev_gamepad_report));
  memset(raw_axis_values, 0, sizeof(raw_axis_values));
  gamepad_report_changed = false;
  gamepad_enabled = true;
}

bool gamepad_hid_is_ready(void) { return tud_hid_n_ready(HID_ITF_GAMEPAD); }

void gamepad_hid_set_axis(uint8_t axis_index, uint8_t value) {
  if (axis_index >= GAMEPAD_NUM_AXES)
    return;

  if (gamepad_report.axes[axis_index] != value) {
    gamepad_report.axes[axis_index] = value;
    gamepad_report_changed = true;
  }
}

void gamepad_hid_set_axis_from_distance(uint8_t axis_index, float distance) {
  if (axis_index >= GAMEPAD_NUM_AXES)
    return;

  // Clamp distance to 0.0 - 1.0
  if (distance < 0.0f)
    distance = 0.0f;
  if (distance > 1.0f)
    distance = 1.0f;

  // Store raw value for snappy mode processing
  raw_axis_values[axis_index] = distance;

  // Get gamepad settings
  const settings_t *s = settings_get();
  float deadzone = s ? (s->gamepad.deadzone / 255.0f) : 0.0f;
  uint8_t curve_type = s ? s->gamepad.curve_type : 0;
  bool snappy_mode = s ? s->gamepad.snappy_mode : false;
  bool square_mode = s ? s->gamepad.square_mode : false;

  // Apply deadzone
  float processed = apply_deadzone(distance, deadzone);

  // Apply analog curve
  processed = apply_curve(processed, curve_type);

  // Snappy mode: use max of opposing axes (for axes 3↔5 which are A↔D)
  if (snappy_mode) {
    // Keys 3 and 5 are A and D (opposing)
    if (axis_index == 3 && raw_axis_values[5] > processed) {
      processed = 0.0f; // D is stronger, suppress A
    } else if (axis_index == 5 && raw_axis_values[3] > processed) {
      processed = 0.0f; // A is stronger, suppress D
    }
  }

  // Square mode: remove circular boundary limitation
  // This is typically applied to X/Y pairs but we just max out the range
  if (square_mode) {
    // Already 0-1 linear, square mode means no circular normalization needed
    // For now just ensure full range is available
  }

  // Convert to 0-255 range
  uint8_t value = (uint8_t)(processed * 255.0f);

  gamepad_hid_set_axis(axis_index, value);
}

bool gamepad_hid_send_report_if_changed(void) {
  if (!gamepad_enabled) {
    return false;
  }

  if (!gamepad_report_changed) {
    return false;
  }

  if (!tud_mounted() || !tud_hid_n_ready(HID_ITF_GAMEPAD)) {
    return false;
  }

  // Check if report actually changed from last sent
  if (memcmp(&gamepad_report, &prev_gamepad_report, sizeof(gamepad_report)) ==
      0) {
    gamepad_report_changed = false;
    return false;
  }

  // Send report (report_id = 0, no report ID in descriptor)
  if (tud_hid_n_report(HID_ITF_GAMEPAD, 0, &gamepad_report,
                       sizeof(gamepad_report))) {
    memcpy(&prev_gamepad_report, &gamepad_report, sizeof(gamepad_report));
    gamepad_report_changed = false;
    return true;
  }

  return false;
}

void gamepad_hid_task(void) { gamepad_hid_send_report_if_changed(); }

void gamepad_hid_set_enabled(bool enabled) { gamepad_enabled = enabled; }

bool gamepad_hid_is_enabled(void) { return gamepad_enabled; }
