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

#if KBHE_CUSTOM_BOOTLOADER_ENABLED
static void clear_interrupt_state(void) {
  SysTick->CTRL = 0;
  SysTick->LOAD = 0;
  SysTick->VAL = 0;

  for (uint32_t i = 0; i < 8u; i++) {
    NVIC->ICER[i] = 0xFFFFFFFFu;
    NVIC->ICPR[i] = 0xFFFFFFFFu;
  }
}

static void restore_core_state_for_bootloader(void) {
  __set_CONTROL(0u);
  __set_BASEPRI(0u);
  __enable_irq();
  __DSB();
  __ISB();
}
#endif

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

static void updater_app_shutdown_usb_only(void) {
  if (tusb_inited()) {
    (void)tud_disconnect();
    HAL_Delay(120);
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

  HAL_NVIC_SetPriority(OTG_HS_IRQn, 0, 0);
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

#if KBHE_CUSTOM_BOOTLOADER_ENABLED
static void jump_to_bootloader(void) {
  const uint32_t *boot_vector = (const uint32_t *)UPDATER_BOOTLOADER_BASE;
  uint32_t boot_stack = boot_vector[0];
  uint32_t boot_entry = boot_vector[1];

  HAL_RCC_DeInit();
  HAL_DeInit();

  __disable_irq();
  clear_interrupt_state();
  SCB->VTOR = UPDATER_BOOTLOADER_BASE;
  __set_MSP(boot_stack);
  __set_PSP(boot_stack);
  restore_core_state_for_bootloader();

  ((void (*)(void))boot_entry)();
  while (1) {
  }
}
#endif

bool updater_app_schedule_action(updater_app_action_t action) {
  if (action == UPDATER_APP_ACTION_NONE) {
    return false;
  }

#if !KBHE_CUSTOM_BOOTLOADER_ENABLED
  if (action == UPDATER_APP_ACTION_ENTER_UPDATER) {
    return false;
  }
#endif

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

  if (s_pending_action == UPDATER_APP_ACTION_USB_REENUMERATE) {
    updater_app_shutdown_usb_only();
    HAL_Delay(120);
    updater_app_reinit_usb_only();
    s_pending_action = UPDATER_APP_ACTION_NONE;
    s_response_sent = false;
    return;
  }

  updater_app_shutdown_peripherals();

#if KBHE_CUSTOM_BOOTLOADER_ENABLED
  if (s_pending_action == UPDATER_APP_ACTION_ENTER_UPDATER) {
    boot_request_set(BOOT_REQUEST_ACTION_ENTER_UPDATER);
    jump_to_bootloader();
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
