#ifndef MOUSE_HID_H_
#define MOUSE_HID_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MOUSE_HID_BUTTON_LEFT 0x01u
#define MOUSE_HID_BUTTON_RIGHT 0x02u
#define MOUSE_HID_BUTTON_MIDDLE 0x04u
#define MOUSE_HID_BUTTON_BACK 0x08u
#define MOUSE_HID_BUTTON_FORWARD 0x10u

void mouse_hid_init(void);
bool mouse_hid_is_ready(void);
void mouse_hid_task(void);
void mouse_hid_on_report_complete(void);
void mouse_hid_button_press(uint8_t button_mask);
void mouse_hid_button_release(uint8_t button_mask);
bool mouse_hid_scroll(int8_t vertical, int8_t horizontal);
void mouse_hid_release_all(void);

#ifdef __cplusplus
}
#endif

#endif /* MOUSE_HID_H_ */
