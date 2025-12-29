/*
 * usb_hid_nkro.h
 * NKRO (N-Key Rollover) Keyboard HID support
 */

#ifndef USB_HID_NKRO_H_
#define USB_HID_NKRO_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

//--------------------------------------------------------------------+
// NKRO Report Structure
// Byte 0: Modifier keys (8 bits)
// Bytes 1-16: Key bitmap (128 keys, 16 bytes)
// Total: 17 bytes
//--------------------------------------------------------------------+
#define NKRO_REPORT_SIZE 17
#define NKRO_KEY_BITMAP_SIZE 16

typedef struct __attribute__((packed)) {
  uint8_t modifier;                      // Modifier keys
  uint8_t keys[NKRO_KEY_BITMAP_SIZE];    // Bitmap for keys 0-127
} nkro_keyboard_report_t;

//--------------------------------------------------------------------+
// API Functions
//--------------------------------------------------------------------+

/**
 * @brief Check if NKRO interface is ready to send a report
 * @return true if ready, false otherwise
 */
bool usb_hid_nkro_is_ready(void);

/**
 * @brief Press a key in NKRO mode
 * @param keycode HID keycode (0-127)
 */
void usb_hid_nkro_key_press(uint8_t keycode);

/**
 * @brief Release a key in NKRO mode
 * @param keycode HID keycode (0-127)
 */
void usb_hid_nkro_key_release(uint8_t keycode);

/**
 * @brief Send NKRO report if changed
 * @return true if report was sent
 */
bool usb_hid_nkro_send_report_if_changed(void);

/**
 * @brief NKRO task - call in main loop
 */
void usb_hid_nkro_task(void);

/**
 * @brief Clear all pressed keys
 */
void usb_hid_nkro_release_all(void);

#ifdef __cplusplus
}
#endif

#endif /* USB_HID_NKRO_H_ */
