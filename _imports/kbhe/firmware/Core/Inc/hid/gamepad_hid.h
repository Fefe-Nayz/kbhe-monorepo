#ifndef GAMEPAD_HID_H_
#define GAMEPAD_HID_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct __attribute__((packed)) {
  uint32_t buttons;
  int8_t lx;
  int8_t ly;
  int8_t rx;
  int8_t ry;
  uint8_t lt;
  uint8_t rt;
} gamepad_report_t;

void gamepad_hid_init(void);
bool gamepad_hid_is_ready(void);
void gamepad_hid_reload_settings(void);
void gamepad_hid_refresh_state(void);
bool gamepad_hid_send_report_if_changed(void);
void gamepad_hid_task(void);
const gamepad_report_t *gamepad_hid_get_report(void);
void gamepad_hid_set_enabled(bool enabled);
bool gamepad_hid_is_enabled(void);
void gamepad_hid_custom_button_press(uint8_t button);
void gamepad_hid_custom_button_release(uint8_t button);
void gamepad_hid_custom_axis_press(uint8_t axis, uint8_t direction);
void gamepad_hid_custom_axis_release(uint8_t axis, uint8_t direction);
void gamepad_hid_custom_clear(void);

#ifdef __cplusplus
}
#endif

#endif /* GAMEPAD_HID_H_ */
