/*
 * keyboard_nkro_hid.h
 * NKRO (N-Key Rollover) Keyboard HID support
 */

#ifndef KEYBOARD_NKRO_HID_H_
#define KEYBOARD_NKRO_HID_H_

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
#define NKRO_ENUMERATION_TIMEOUT_MS 1000u
#define NKRO_ACTIVE_STALL_TIMEOUT_MS 100u

typedef struct __attribute__((packed)) {
  uint8_t modifier;                   // Modifier keys
  uint8_t keys[NKRO_KEY_BITMAP_SIZE]; // Bitmap for keys 0-127
} nkro_keyboard_report_t;

//--------------------------------------------------------------------+
// API Functions
//--------------------------------------------------------------------+

/**
 * @brief Check if NKRO interface is ready to send a report
 * @return true if ready, false otherwise
 */
bool keyboard_nkro_hid_is_ready(void);

/**
 * @brief Press a key in NKRO mode
 * @param keycode HID keycode (0-127)
 */
void keyboard_nkro_hid_key_press(uint8_t keycode);

/**
 * @brief Release a key in NKRO mode
 * @param keycode HID keycode (0-127)
 */
void keyboard_nkro_hid_key_release(uint8_t keycode);

/**
 * @brief Send NKRO report if changed
 * @return true if report was sent
 */
bool keyboard_nkro_hid_send_report_if_changed(void);

/**
 * @brief Returns true when NKRO should be used for key routing
 *
 * In Auto mode, this becomes true once NKRO is ready after USB enumeration.
 * If NKRO is not ready within NKRO_ENUMERATION_TIMEOUT_MS, runtime fallback
 * stays on 6KRO until USB disconnect.
 */
bool keyboard_nkro_hid_can_route_keycodes(void);

/**
 * @brief Returns true when runtime fallback has switched to 6KRO
 */
bool keyboard_nkro_hid_is_runtime_fallback_active(void);

/**
 * @brief NKRO task - call in main loop
 */
void keyboard_nkro_hid_task(void);

/**
 * @brief Clear all pressed keys
 */
void keyboard_nkro_hid_release_all(void);

#ifdef __cplusplus
}
#endif

#endif /* KEYBOARD_NKRO_HID_H_ */
