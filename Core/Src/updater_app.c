#include "updater_app.h"

#include "main.h"
#include "tusb.h"
#include "updater_shared.h"
#include "usb_descriptors.h"

extern ADC_HandleTypeDef hadc1;
extern TIM_HandleTypeDef htim3;
extern TIM_HandleTypeDef htim4;

static volatile updater_app_action_t s_pending_action = UPDATER_APP_ACTION_NONE;
static volatile bool s_response_sent = false;

static void updater_app_shutdown_peripherals(void) {
  (void)HAL_ADC_Stop_DMA(&hadc1);
  (void)HAL_TIM_Base_Stop(&htim4);
  (void)HAL_TIM_PWM_Stop_DMA(&htim3, TIM_CHANNEL_2);
  (void)HAL_TIM_PWM_Stop(&htim3, TIM_CHANNEL_2);

  if (tusb_inited()) {
    (void)tud_disconnect();
    HAL_Delay(20);
    (void)tusb_deinit(USB_RHPORT_HS);
  }

  HAL_NVIC_DisableIRQ(OTG_HS_IRQn);
  __HAL_RCC_USB_OTG_HS_ULPI_CLK_DISABLE();
  __HAL_RCC_USB_OTG_HS_CLK_DISABLE();
  __HAL_RCC_OTGPHYC_CLK_DISABLE();
}

bool updater_app_schedule_action(updater_app_action_t action) {
  if (action == UPDATER_APP_ACTION_NONE) {
    return false;
  }

  s_pending_action = action;
  s_response_sent = false;
  return true;
}

void updater_app_notify_response_sent(void) {
  if (s_pending_action != UPDATER_APP_ACTION_NONE) {
    s_response_sent = true;
  }
}

void updater_app_task(void) {
  if ((s_pending_action == UPDATER_APP_ACTION_NONE) || !s_response_sent) {
    return;
  }

  updater_app_shutdown_peripherals();

  if (s_pending_action == UPDATER_APP_ACTION_ENTER_UPDATER) {
    boot_request_set(BOOT_REQUEST_ACTION_ENTER_UPDATER);
  } else {
    boot_request_clear();
  }

  __disable_irq();
  NVIC_SystemReset();
}
