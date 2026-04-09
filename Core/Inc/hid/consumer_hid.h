#ifndef CONSUMER_HID_H_
#define CONSUMER_HID_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void consumer_hid_init(void);
bool consumer_hid_is_ready(void);
bool consumer_hid_send_usage(uint16_t usage_code);
bool consumer_hid_volume_up(void);
bool consumer_hid_volume_down(void);
bool consumer_hid_play_pause(void);
void consumer_hid_task(void);
void consumer_hid_on_report_complete(void);

#ifdef __cplusplus
}
#endif

#endif /* CONSUMER_HID_H_ */
