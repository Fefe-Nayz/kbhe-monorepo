#ifndef UPDATER_BOOTLOADER_H_
#define UPDATER_BOOTLOADER_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void updater_bootloader_init(void);
bool updater_bootloader_process_packet(const uint8_t *request,
                                       uint8_t *response);
void updater_bootloader_notify_response_sent(void);
bool updater_bootloader_should_jump_to_app(void);

#ifdef __cplusplus
}
#endif

#endif /* UPDATER_BOOTLOADER_H_ */
