#include <stdint.h>

uint16_t layout_get_default_keycode(uint8_t key);

uint16_t layout_get_default_layer_keycode(uint8_t layer, uint8_t key);

uint16_t layout_get_active_keycode(uint8_t key);

uint8_t layout_get_active_layer_top(void);

uint8_t layout_get_active_modifier_mask(void);

void layout_press(uint8_t key);

void layout_release(uint8_t key);

void layout_press_action_for_key(uint8_t source_key, uint16_t keycode);

void layout_release_action_for_key(uint8_t source_key, uint16_t keycode);

void layout_press_action(uint16_t keycode);

void layout_release_action(uint16_t keycode);

void layout_tap_action(uint16_t keycode);

void layout_reset_state(void);
