#ifndef KBHE_KEYBOARD_TEST_LED_MATRIX_H_
#define KBHE_KEYBOARD_TEST_LED_MATRIX_H_

#include <stdbool.h>

void led_matrix_set_usb_suspend_state(bool suspended);
void led_matrix_update(void);

#endif
