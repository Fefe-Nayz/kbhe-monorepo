#ifndef KBHE_HOST_TUSB_STUB_H_
#define KBHE_HOST_TUSB_STUB_H_

#include <stdbool.h>
#include <stdint.h>

bool tud_hid_n_ready(uint8_t instance);
bool tud_mounted(void);
bool tud_hid_n_report(uint8_t instance, uint8_t report_id,
                      const void *report, uint16_t len);

#define HID_USAGE_CONSUMER_VOLUME_INCREMENT 0x00E9u
#define HID_USAGE_CONSUMER_VOLUME_DECREMENT 0x00EAu
#define HID_USAGE_CONSUMER_MUTE 0x00E2u
#define HID_USAGE_CONSUMER_PLAY_PAUSE 0x00CDu

#endif
