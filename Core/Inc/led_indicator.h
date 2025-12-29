/*
 * led_indicator.h
 * Status LED indicator on PE0 for caps lock and num lock
 */

#ifndef LED_INDICATOR_H_
#define LED_INDICATOR_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

//--------------------------------------------------------------------+
// HID Keyboard LED bit masks
//--------------------------------------------------------------------+
#define HID_LED_NUM_LOCK (1 << 0)
#define HID_LED_CAPS_LOCK (1 << 1)
#define HID_LED_SCROLL_LOCK (1 << 2)
#define HID_LED_COMPOSE (1 << 3)
#define HID_LED_KANA (1 << 4)

//--------------------------------------------------------------------+
// API
//--------------------------------------------------------------------+

/**
 * @brief Initialize the LED indicator (PE0)
 */
void led_indicator_init(void);

/**
 * @brief Update LED state from HID keyboard LED report
 * @param led_state HID LED state byte
 *
 * Behavior:
 * - Caps Lock ON: LED solid ON
 * - Num Lock ON (no caps): LED blinking
 * - Both OFF: LED OFF
 */
void led_indicator_set_state(uint8_t led_state);

/**
 * @brief Tick function for blinking, call from main loop
 * @param tick_ms Current system tick in ms (e.g., HAL_GetTick())
 */
void led_indicator_tick(uint32_t tick_ms);

/**
 * @brief Get current caps lock state
 */
bool led_indicator_is_caps_lock(void);

/**
 * @brief Get current num lock state
 */
bool led_indicator_is_num_lock(void);

#ifdef __cplusplus
}
#endif

#endif /* LED_INDICATOR_H_ */
