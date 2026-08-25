#ifndef KBHE_XINPUT_TEST_SETTINGS_H_
#define KBHE_XINPUT_TEST_SETTINGS_H_

typedef enum {
  GAMEPAD_API_HID = 0,
  GAMEPAD_API_XINPUT = 1,
} gamepad_api_mode_t;

gamepad_api_mode_t settings_get_gamepad_api_mode(void);

#endif
