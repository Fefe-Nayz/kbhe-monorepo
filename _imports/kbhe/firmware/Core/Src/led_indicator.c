/*
 * led_indicator.c
 * Keyboard lock state tracking for Caps/Num/Scroll lock.
 */

#include "led_indicator.h"

static bool caps_lock = false;
static bool num_lock = false;
static bool scroll_lock = false;

void led_indicator_init(void) {
  caps_lock = false;
  num_lock = false;
  scroll_lock = false;
}

void led_indicator_set_state(uint8_t led_state) {
  num_lock = (led_state & HID_LED_NUM_LOCK) != 0;
  caps_lock = (led_state & HID_LED_CAPS_LOCK) != 0;
  scroll_lock = (led_state & HID_LED_SCROLL_LOCK) != 0;
}

void led_indicator_tick(uint32_t tick_ms) { (void)tick_ms; }

bool led_indicator_is_caps_lock(void) { return caps_lock; }

bool led_indicator_is_num_lock(void) { return num_lock; }

bool led_indicator_is_scroll_lock(void) { return scroll_lock; }
