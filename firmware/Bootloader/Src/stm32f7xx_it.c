#include "stm32f7xx_hal.h"
#include "tusb.h"
#include "usb_descriptors.h"

void SysTick_Handler(void) {
  HAL_IncTick();
}

void OTG_HS_IRQHandler(void) {
  tud_int_handler(USB_RHPORT_HS);
}
