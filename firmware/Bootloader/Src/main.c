#include "bootloader_usb.h"
#include "stm32f7xx_hal.h"
#include "tusb.h"
#include "updater_bootloader.h"
#include "updater_bootloader_version.h"
#include "updater_shared.h"
#include "updater_version_floor.h"
#include "usb_descriptors.h"

static bool s_hal_initialized = false;

UPDATER_DECLARE_BOOTLOADER_VERSION_RECORD(g_kbhe_bootloader_version_record);

static void SystemClock_Config(void);
static void USB_HS_Init(void);
static void jump_to_application(bool runtime_cleanup);
__attribute__((naked, noreturn, noinline)) static void
jump_to_application_trampoline(uint32_t app_stack, uint32_t app_entry);
static void reboot_to_application(void);
void Error_Handler(void);

uint32_t tusb_time_millis_api(void) { return HAL_GetTick(); }

void tusb_time_delay_ms_api(uint32_t ms) { HAL_Delay(ms); }

void HAL_MspInit(void) {
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_RCC_SYSCFG_CLK_ENABLE();
}

static void SystemClock_Config(void) {
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 8;
  RCC_OscInitStruct.PLL.PLLN = 216;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
    Error_Handler();
  }

  if (HAL_PWREx_EnableOverDrive() != HAL_OK) {
    Error_Handler();
  }

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                                RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;
  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_7) != HAL_OK) {
    Error_Handler();
  }
}

static void USB_HS_Init(void) {
  GPIO_InitTypeDef GPIO_InitStruct = {0};

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
}

static void clear_interrupt_state(void) {
  SysTick->CTRL = 0;
  SysTick->LOAD = 0;
  SysTick->VAL = 0;
  SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk | SCB_ICSR_PENDSVCLR_Msk;

  for (uint32_t i = 0; i < 8u; i++) {
    NVIC->ICER[i] = 0xFFFFFFFFu;
    NVIC->ICPR[i] = 0xFFFFFFFFu;
  }
}

static void restore_core_state_for_application(void) {
  /* Match the architectural reset state while still using the bootloader's
   * stack. PRIMASK stays set until the final stackless trampoline. */
  __set_CONTROL(0u);
  __set_BASEPRI(0u);
  __set_FAULTMASK(0u);
  __DSB();
  __ISB();
}

__attribute__((naked, noreturn, noinline)) static void
jump_to_application_trampoline(
    uint32_t app_stack __attribute__((unused)),
    uint32_t app_entry __attribute__((unused))) {
  __asm volatile("msr msp, r0\n"
                 "cpsie i\n"
                 "dsb\n"
                 "isb\n"
                 "bx r1\n");
}

static void jump_to_application(bool runtime_cleanup) {
  const uint32_t *app_vector = (const uint32_t *)UPDATER_APP_BASE;
  uint32_t app_stack = app_vector[0];
  uint32_t app_entry = app_vector[1];

  if (runtime_cleanup) {
    if (tusb_inited()) {
      (void)tud_disconnect();
      HAL_Delay(20);
      (void)tusb_deinit(USB_RHPORT_HS);
    }

    if (s_hal_initialized) {
      HAL_NVIC_DisableIRQ(OTG_HS_IRQn);
      __HAL_RCC_USB_OTG_HS_ULPI_CLK_DISABLE();
      __HAL_RCC_USB_OTG_HS_CLK_DISABLE();
      __HAL_RCC_OTGPHYC_CLK_DISABLE();
      HAL_RCC_DeInit();
      HAL_DeInit();
    }
  }

  __disable_irq();
  clear_interrupt_state();
  SCB->VTOR = UPDATER_APP_BASE;
  __set_PSP(app_stack);
  restore_core_state_for_application();
  __DSB();
  __ISB();
  jump_to_application_trampoline(app_stack, app_entry);
}

static void reboot_to_application(void) {
  if (tusb_inited()) {
    (void)tud_disconnect();
    HAL_Delay(30);
    (void)tusb_deinit(USB_RHPORT_HS);
  }

  if (s_hal_initialized) {
    HAL_NVIC_DisableIRQ(OTG_HS_IRQn);
    __HAL_RCC_USB_OTG_HS_ULPI_CLK_DISABLE();
    __HAL_RCC_USB_OTG_HS_CLK_DISABLE();
    __HAL_RCC_OTGPHYC_CLK_DISABLE();
  }

  boot_request_clear();
  HAL_Delay(10);
  NVIC_SystemReset();
}

int main(void) {
  bool stay_in_updater = boot_request_take(BOOT_REQUEST_ACTION_ENTER_UPDATER);
  updater_fw_version_t installed = {0};

  /* Image authentication hashes the complete application and performs an
   * Ed25519 verification.  Run it at the configured core frequency instead of
   * the 16 MHz reset clock; otherwise this work delays USB enumeration by
   * several seconds on every cold boot. */
  HAL_Init();
  s_hal_initialized = true;
  SystemClock_Config();

  if (!stay_in_updater && updater_read_valid_app_version(&installed) &&
      updater_version_floor_allows(installed)) {
    /* HAL and the PLL are now live solely for validation.  Restore the reset
     * clock/peripheral state before handing control to the application, whose
     * startup configures them independently. */
    jump_to_application(true);
  }

  boot_request_clear();

  USB_HS_Init();

  updater_bootloader_init();
  bootloader_usb_init();

  while (1) {
    tud_task();
    bootloader_usb_task();

    if (updater_bootloader_should_jump_to_app()) {
      reboot_to_application();
    }
  }
}

void Error_Handler(void) {
  __disable_irq();
  while (1) {
  }
}
