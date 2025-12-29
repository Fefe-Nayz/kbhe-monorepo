/*
 * usb_gamepad.h
 * API pour le gamepad HID USB avec axes Hall Effect
 */

#ifndef USB_GAMEPAD_H_
#define USB_GAMEPAD_H_

#include <stdbool.h>
#include <stdint.h>


#ifdef __cplusplus
extern "C" {
#endif

//--------------------------------------------------------------------+
// Gamepad Configuration
//--------------------------------------------------------------------+

// Number of axes (one per Hall Effect key)
#define GAMEPAD_NUM_AXES 6

// Axis value range
#define GAMEPAD_AXIS_MIN 0
#define GAMEPAD_AXIS_MAX 255
#define GAMEPAD_AXIS_CENTER 0 // Released position = 0 (no travel)

//--------------------------------------------------------------------+
// Gamepad Report Structure
//--------------------------------------------------------------------+
typedef struct __attribute__((packed)) {
  uint8_t axes[GAMEPAD_NUM_AXES]; // 6 axes: X, Y, Z, Rx, Ry, Rz
} gamepad_report_t;

//--------------------------------------------------------------------+
// Public API
//--------------------------------------------------------------------+

/**
 * Initialize the gamepad module
 */
void usb_gamepad_init(void);

/**
 * Check if gamepad HID interface is ready
 * @return true if ready to send reports
 */
bool usb_gamepad_is_ready(void);

/**
 * Set axis value for a specific key
 * @param axis_index Index 0-5 corresponding to key 0-5
 * @param value Axis value 0-255 (0 = released, 255 = fully pressed)
 */
void usb_gamepad_set_axis(uint8_t axis_index, uint8_t value);

/**
 * Set axis value from key travel distance (0.0 - 1.0 normalized)
 * @param axis_index Index 0-5 corresponding to key 0-5
 * @param distance Normalized distance 0.0 (released) to 1.0 (fully pressed)
 */
void usb_gamepad_set_axis_from_distance(uint8_t axis_index, float distance);

/**
 * Send gamepad report if changed
 * Should be called in main loop
 * @return true if report was sent
 */
bool usb_gamepad_send_report_if_changed(void);

/**
 * Main task for gamepad - call in main loop
 */
void usb_gamepad_task(void);

/**
 * Enable or disable gamepad output
 * @param enabled true to enable, false to disable
 */
void usb_gamepad_set_enabled(bool enabled);

/**
 * Check if gamepad output is enabled
 * @return true if enabled
 */
bool usb_gamepad_is_enabled(void);

#ifdef __cplusplus
}
#endif

#endif /* USB_GAMEPAD_H_ */
