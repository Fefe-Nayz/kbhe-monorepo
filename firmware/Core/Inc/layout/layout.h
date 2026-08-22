#include <stdint.h>

/* Action programs run independently from physical keys. One owner per macro
 * slot lets all 16 programs overlap while releasing exactly the keys acquired
 * by each instance. action_engine.c statically checks this stays in sync. */
#define LAYOUT_ACTION_OWNER_COUNT 16u

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

void layout_press_action_owned(uint8_t owner, uint16_t keycode);

void layout_release_action_owned(uint8_t owner, uint16_t keycode);

void layout_tap_action(uint16_t keycode);

/* Reconcile NKRO/boot-report routing after USB protocol/readiness changes,
 * including while keys are already held. */
void layout_refresh_output_routes(void);

void layout_reset_state(void);
