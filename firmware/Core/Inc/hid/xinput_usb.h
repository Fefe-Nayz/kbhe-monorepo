#ifndef XINPUT_USB_H_
#define XINPUT_USB_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef XINPUT_SUBCLASS_DEFAULT
#define XINPUT_SUBCLASS_DEFAULT 0x5D
#endif

#ifndef XINPUT_PROTOCOL_DEFAULT
#define XINPUT_PROTOCOL_DEFAULT 0x01
#endif

#ifndef XINPUT_EP_SIZE
#define XINPUT_EP_SIZE 32
#endif

#ifndef XINPUT_DESC_LEN
#define XINPUT_DESC_LEN 39
#endif

typedef enum {
  XINPUT_BUTTON_UP = 1u << 0,
  XINPUT_BUTTON_DOWN = 1u << 1,
  XINPUT_BUTTON_LEFT = 1u << 2,
  XINPUT_BUTTON_RIGHT = 1u << 3,
  XINPUT_BUTTON_START = 1u << 4,
  XINPUT_BUTTON_BACK = 1u << 5,
  XINPUT_BUTTON_LS = 1u << 6,
  XINPUT_BUTTON_RS = 1u << 7,
  XINPUT_BUTTON_LB = 1u << 8,
  XINPUT_BUTTON_RB = 1u << 9,
  XINPUT_BUTTON_HOME = 1u << 10,
  XINPUT_BUTTON_A = 1u << 12,
  XINPUT_BUTTON_B = 1u << 13,
  XINPUT_BUTTON_X = 1u << 14,
  XINPUT_BUTTON_Y = 1u << 15,
} xinput_button_mask_t;

typedef struct __attribute__((packed)) {
  uint8_t report_id;
  uint8_t report_size;
  uint16_t buttons;
  uint8_t lt;
  uint8_t rt;
  int16_t lx;
  int16_t ly;
  int16_t rx;
  int16_t ry;
  uint8_t reserved[6];
} xinput_report_t;

void xinput_usb_init(void);
void xinput_usb_task(void);

#ifdef __cplusplus
}
#endif

#endif /* XINPUT_USB_H_ */
