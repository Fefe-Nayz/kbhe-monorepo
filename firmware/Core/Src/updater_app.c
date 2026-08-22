#include "updater_app.h"

#include "diagnostics.h"
#include "hid/consumer_hid.h"
#include "hid/gamepad_hid.h"
#include "hid/mouse_hid.h"
#include "hid/raw_hid.h"
#include "hid/xinput_usb.h"
#include "main.h"
#include "settings.h"
#include "tusb.h"
#include "updater_shared.h"
#include "usb_descriptors.h"

#ifndef KBHE_CUSTOM_BOOTLOADER_ENABLED
#define KBHE_CUSTOM_BOOTLOADER_ENABLED 1
#endif

extern ADC_HandleTypeDef hadc1;
extern TIM_HandleTypeDef htim2;
extern TIM_HandleTypeDef htim4;

static volatile updater_app_action_t s_pending_action = UPDATER_APP_ACTION_NONE;
static volatile bool s_response_sent = false;
typedef enum {
  USB_REENUMERATE_IDLE = 0,
  USB_REENUMERATE_WAIT_DISCONNECT,
  USB_REENUMERATE_WAIT_REINIT,
} usb_reenumerate_phase_t;
static usb_reenumerate_phase_t s_usb_reenumerate_phase = USB_REENUMERATE_IDLE;
static uint32_t s_usb_reenumerate_deadline_ms = 0u;

static bool updater_app_deadline_reached(uint32_t now_ms,
                                         uint32_t deadline_ms) {
  return (int32_t)(now_ms - deadline_ms) >= 0;
}

static void updater_app_shutdown_peripherals(void) {
  (void)HAL_ADC_Stop_DMA(&hadc1);
  (void)HAL_TIM_Base_Stop(&htim4);
  (void)HAL_TIM_PWM_Stop_DMA(&htim2, TIM_CHANNEL_1);
  (void)HAL_TIM_PWM_Stop(&htim2, TIM_CHANNEL_1);

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

static void updater_app_deinit_usb_only(void) {
  if (tusb_inited()) {
    (void)tusb_deinit(USB_RHPORT_HS);
  }

  HAL_NVIC_DisableIRQ(OTG_HS_IRQn);
  __HAL_RCC_USB_OTG_HS_ULPI_CLK_DISABLE();
  __HAL_RCC_USB_OTG_HS_CLK_DISABLE();
  __HAL_RCC_OTGPHYC_CLK_DISABLE();
}

static void updater_app_reinit_usb_only(void) {
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  const tusb_rhport_init_t rhport_init = {
      .role = TUSB_ROLE_DEVICE,
      .speed = TUSB_SPEED_HIGH,
  };

  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_OTGPHYC_CLK_ENABLE();
  __HAL_RCC_USB_OTG_HS_CLK_ENABLE();
  __HAL_RCC_USB_OTG_HS_ULPI_CLK_ENABLE();

#if defined(RCC_AHB1LPENR_OTGHSULPILPEN)
  RCC->AHB1LPENR &= ~RCC_AHB1LPENR_OTGHSULPILPEN;
#endif

  GPIO_InitStruct.Pin = GPIO_PIN_14 | GPIO_PIN_15;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF12_OTG_HS_FS;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  HAL_NVIC_SetPriority(OTG_HS_IRQn, KBHE_NVIC_PRIORITY_USB, 0);
  HAL_NVIC_EnableIRQ(OTG_HS_IRQn);

  (void)tusb_init(USB_RHPORT_HS, &rhport_init);
  raw_hid_init();
  consumer_hid_init();
  mouse_hid_init();
  xinput_usb_init();
  diagnostics_init();
  gamepad_hid_init();
  gamepad_hid_set_enabled(settings_is_gamepad_enabled());
  gamepad_hid_reload_settings();
  (void)tud_connect();
}

bool updater_app_schedule_action(updater_app_action_t action) {
  if (action == UPDATER_APP_ACTION_NONE) {
    return false;
  }

  /* Do not replace a partially executed re-enumeration with another action:
   * doing so could leave USB disconnected or its clocks disabled. */
  if (s_pending_action != UPDATER_APP_ACTION_NONE) {
    return false;
  }

#if !KBHE_CUSTOM_BOOTLOADER_ENABLED
  if (action == UPDATER_APP_ACTION_ENTER_UPDATER) {
    return false;
  }
#endif

  s_pending_action = action;
  s_response_sent = false;
  s_usb_reenumerate_phase = USB_REENUMERATE_IDLE;
  s_usb_reenumerate_deadline_ms = 0u;
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

  if (s_pending_action == UPDATER_APP_ACTION_USB_REENUMERATE) {
    uint32_t now_ms = HAL_GetTick();

    if (s_usb_reenumerate_phase == USB_REENUMERATE_IDLE) {
      if (tusb_inited()) {
        (void)tud_disconnect();
      }
      s_usb_reenumerate_deadline_ms = now_ms + 120u;
      s_usb_reenumerate_phase = USB_REENUMERATE_WAIT_DISCONNECT;
      return;
    }

    if (s_usb_reenumerate_phase == USB_REENUMERATE_WAIT_DISCONNECT) {
      if (!updater_app_deadline_reached(now_ms,
                                        s_usb_reenumerate_deadline_ms)) {
        return;
      }
      updater_app_deinit_usb_only();
      s_usb_reenumerate_deadline_ms = now_ms + 120u;
      s_usb_reenumerate_phase = USB_REENUMERATE_WAIT_REINIT;
      return;
    }

    if (!updater_app_deadline_reached(now_ms,
                                      s_usb_reenumerate_deadline_ms)) {
      return;
    }
    updater_app_reinit_usb_only();
    s_usb_reenumerate_phase = USB_REENUMERATE_IDLE;
    s_pending_action = UPDATER_APP_ACTION_NONE;
    s_response_sent = false;
    return;
  }

  /* Reboot only after the budgeted persistence state machine has committed.
   * This keeps the 8 kHz input loop alive while settings drain instead of
   * performing a multi-kilobyte synchronous flash write in the HID handler. */
  if (!settings_is_ram_only_mode() && settings_has_unsaved_changes()) {
    (void)settings_request_save();
    return;
  }

  updater_app_shutdown_peripherals();

#if KBHE_CUSTOM_BOOTLOADER_ENABLED
  if (s_pending_action == UPDATER_APP_ACTION_ENTER_UPDATER) {
    boot_request_set(BOOT_REQUEST_ACTION_ENTER_UPDATER);
    __disable_irq();
#if (__DCACHE_PRESENT == 1U)
    /* The boot request lives in retained SRAM. Commit its cache line before
     * reset so the bootloader cannot observe stale memory. Reset also gives
     * the bootloader clean I/D-cache and peripheral state. */
    SCB_CleanDCache();
#endif
    __DSB();
    __ISB();
    NVIC_SystemReset();
  } else {
    boot_request_clear();
    __disable_irq();
    NVIC_SystemReset();
  }
#else
  s_pending_action = UPDATER_APP_ACTION_NONE;
  s_response_sent = false;
  __disable_irq();
  NVIC_SystemReset();
#endif
}
