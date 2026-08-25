#ifndef KBHE_XINPUT_TEST_GAMEPAD_HID_H_
#define KBHE_XINPUT_TEST_GAMEPAD_HID_H_

#include <stdint.h>

typedef struct __attribute__((packed)) {
  uint32_t buttons;
  int8_t lx;
  int8_t ly;
  int8_t rx;
  int8_t ry;
  uint8_t lt;
  uint8_t rt;
} gamepad_report_t;

const gamepad_report_t *gamepad_hid_get_report(void);

#endif
