#ifndef BOOTLOADER_USB_H_
#define BOOTLOADER_USB_H_

#ifdef __cplusplus
extern "C" {
#endif

void bootloader_usb_init(void);
void bootloader_usb_task(void);

#ifdef __cplusplus
}
#endif

#endif /* BOOTLOADER_USB_H_ */
