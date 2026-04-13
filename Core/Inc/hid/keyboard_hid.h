/*
 * keyboard_hid.h
 * API pour le clavier HID USB 8kHz
 */

#ifndef KEYBOARD_HID_H_
#define KEYBOARD_HID_H_

#include <stdbool.h>
#include <stdint.h>


#ifdef __cplusplus
extern "C" {
#endif

// Note: Keyboard modifier keys and LED status are defined by TinyUSB in hid.h
// Use the hid_keyboard_modifier_bm_t and hid_keyboard_led_bm_t enums instead

//--------------------------------------------------------------------+
// Common HID Keycodes (USB HID Usage Tables)
//--------------------------------------------------------------------+
// Letters
#define HID_KEY_A 0x04
#define HID_KEY_B 0x05
#define HID_KEY_C 0x06
#define HID_KEY_D 0x07
#define HID_KEY_E 0x08
#define HID_KEY_F 0x09
#define HID_KEY_G 0x0A
#define HID_KEY_H 0x0B
#define HID_KEY_I 0x0C
#define HID_KEY_J 0x0D
#define HID_KEY_K 0x0E
#define HID_KEY_L 0x0F
#define HID_KEY_M 0x10
#define HID_KEY_N 0x11
#define HID_KEY_O 0x12
#define HID_KEY_P 0x13
#define HID_KEY_Q 0x14
#define HID_KEY_R 0x15
#define HID_KEY_S 0x16
#define HID_KEY_T 0x17
#define HID_KEY_U 0x18
#define HID_KEY_V 0x19
#define HID_KEY_W 0x1A
#define HID_KEY_X 0x1B
#define HID_KEY_Y 0x1C
#define HID_KEY_Z 0x1D

// Numbers
#define HID_KEY_1 0x1E
#define HID_KEY_2 0x1F
#define HID_KEY_3 0x20
#define HID_KEY_4 0x21
#define HID_KEY_5 0x22
#define HID_KEY_6 0x23
#define HID_KEY_7 0x24
#define HID_KEY_8 0x25
#define HID_KEY_9 0x26
#define HID_KEY_0 0x27

// Special keys
#define HID_KEY_ENTER 0x28
#define HID_KEY_ESCAPE 0x29
#define HID_KEY_BACKSPACE 0x2A
#define HID_KEY_TAB 0x2B
#define HID_KEY_SPACE 0x2C

// Function keys
#define HID_KEY_F1 0x3A
#define HID_KEY_F2 0x3B
#define HID_KEY_F3 0x3C
#define HID_KEY_F4 0x3D
#define HID_KEY_F5 0x3E
#define HID_KEY_F6 0x3F
#define HID_KEY_F7 0x40
#define HID_KEY_F8 0x41
#define HID_KEY_F9 0x42
#define HID_KEY_F10 0x43
#define HID_KEY_F11 0x44
#define HID_KEY_F12 0x45

// Arrow keys
#define HID_KEY_ARROW_RIGHT 0x4F
#define HID_KEY_ARROW_LEFT 0x50
#define HID_KEY_ARROW_DOWN 0x51
#define HID_KEY_ARROW_UP 0x52

//--------------------------------------------------------------------+
// API Functions
//--------------------------------------------------------------------+

/**
 * @brief Check if HID interface is ready to send a report
 * @return true if ready, false otherwise
 */
bool keyboard_hid_is_ready(void);

/**
 * @brief Returns true when the keyboard interface is in HID Boot protocol mode
 */
bool keyboard_hid_is_boot_protocol_active(void);

/**
 * @brief Send a keyboard report with specified modifier and keycodes
 * @param modifier Modifier keys (KEYBOARD_MODIFIER_*)
 * @param keycodes Array of 6 keycodes (HID_KEY_*)
 * @return true if report was queued successfully
 */
bool keyboard_hid_send_report(uint8_t modifier, const uint8_t keycodes[6]);

/**
 * @brief Send a single key press
 * @param modifier Modifier keys
 * @param keycode Single keycode to press
 * @return true if report was queued successfully
 */
bool keyboard_hid_press_key(uint8_t modifier, uint8_t keycode);

/**
 * @brief Release all keys (send empty report)
 * @return true if report was queued successfully
 */
bool keyboard_hid_release_all(void);

/**
 * @brief Clear the internal pressed-key bookkeeping without sending a new
 *        report. Useful when a higher-level mode takes exclusive input control.
 */
void keyboard_hid_reset_state(void);

/**
 * @brief Register a key as pressed (add to report buffer)
 * @param keycode HID keycode to add
 */
void keyboard_hid_key_press(uint8_t keycode);

/**
 * @brief Register a key as released (remove from report buffer)
 * @param keycode HID keycode to remove
 */
void keyboard_hid_key_release(uint8_t keycode);

/**
 * @brief Send the current key state report if USB is ready
 * @return true if report was sent
 */
bool keyboard_hid_send_report_if_changed(void);

/**
 * @brief HID task to be called in main loop
 *        Handles pending HID operations
 */
void keyboard_hid_task(void);

#ifdef __cplusplus
}
#endif

#endif /* KEYBOARD_HID_H_ */
