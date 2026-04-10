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
void gamepad_hid_set_enabled(bool enabled);
bool gamepad_hid_is_enabled(void);

#ifdef __cplusplus
}
#endif

#endif /* GAMEPAD_HID_H_ */
