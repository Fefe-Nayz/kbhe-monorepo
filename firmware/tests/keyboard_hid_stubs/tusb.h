#ifndef KBHE_KEYBOARD_TEST_TUSB_H_
#define KBHE_KEYBOARD_TEST_TUSB_H_

#include <stdbool.h>
#include <stdint.h>

typedef enum {
  HID_REPORT_TYPE_INVALID = 0,
  HID_REPORT_TYPE_INPUT = 1,
  HID_REPORT_TYPE_OUTPUT = 2,
  HID_REPORT_TYPE_FEATURE = 3,
} hid_report_type_t;

typedef struct {
  uint8_t modifier;
  uint8_t reserved;
  uint8_t keycode[6];
} hid_keyboard_report_t;

#define HID_PROTOCOL_BOOT 0u
#define HID_PROTOCOL_REPORT 1u
#define HID_KEY_CONTROL_LEFT 0xE0u
#define HID_KEY_GUI_RIGHT 0xE7u

bool tud_mounted(void);
bool tud_hid_n_ready(uint8_t instance);
uint8_t tud_hid_n_get_protocol(uint8_t instance);
bool tud_hid_n_keyboard_report(uint8_t instance, uint8_t report_id,
                               uint8_t modifier,
                               const uint8_t keycodes[6]);
bool tud_hid_n_report(uint8_t instance, uint8_t report_id,
                      const void *report, uint16_t len);

void tud_hid_report_complete_cb(uint8_t instance, const uint8_t *report,
                                uint16_t len);
void tud_hid_report_failed_cb(uint8_t instance,
                              hid_report_type_t report_type,
                              const uint8_t *report, uint16_t xferred_bytes);
void tud_umount_cb(void);

#endif
