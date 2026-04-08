#include <stdint.h>

uint16_t layout_get_default_keycode(uint8_t key);

uint16_t layout_get_active_keycode(uint8_t key);

void layout_press(uint8_t key);

void layout_release(uint8_t key);
