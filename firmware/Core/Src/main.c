/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file           : main.c
 * @brief          : Main program body
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2025 STMicroelectronics.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 *
 ******************************************************************************
 */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "analog/multiplexer.h"
#include "analog/lut.h"
#include "analog/analog.h"
#include "analog/filter.h"
#include "analog/calibration.h"
#include "diagnostics.h"

#include "settings.h"

#include "stm32f7xx_hal_gpio.h"
#include "trigger/trigger.h"
#include "trigger/socd.h"

#include "adc_capture.h"
// #include "adc_ema.h"
#include "led_indicator.h"
#include "led_matrix.h"
#include "hid/raw_hid.h"
#include "stm32f7xx_hal_adc.h"
// #include "trigger.h"
#include "tusb.h"
#include "updater_app.h"
#include "hid/gamepad_hid.h"
#include "hid/xinput_usb.h"
#include "usb_descriptors.h"
#include "hid/consumer_hid.h"
#include "hid/keyboard_hid.h"
#include "hid/keyboard_nkro_hid.h"
#include "hid/mouse_hid.h"
#include "rotary_encoder.h"
#include "stm32f7xx_ll_adc.h"
#include "ws2812.h" // Include WS2812 header
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/cdefs.h>

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define KBHE_TIMING_PROFILE_DECIMATION 32u
#define MCU_LED_THERMAL_LIMIT_C 70
#define MCU_LED_THERMAL_HYSTERESIS_C 3
#define MCU_LED_THERMAL_BRIGHTNESS_MAX 96u

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;
DMA_HandleTypeDef hdma_adc1;

TIM_HandleTypeDef htim2;
TIM_HandleTypeDef htim4;
DMA_HandleTypeDef hdma_tim2_ch1;

PCD_HandleTypeDef hpcd_USB_OTG_HS;

/* USER CODE BEGIN PV */
// /**
//  * ADC DMA NUMBER OF CONVERSIONS COMPLETED
//  */
// uint32_t num_conv = 0;

/**
 * LED DMA BUFFER
 */
uint32_t ws2812_dma_buffer[BUFFER_SIZE * 2] __attribute__((aligned(256)));

/**
 * EMA FILTERING - Default values (can be overridden via settings)
 */
// #define ADC_EMA_NOISE_BAND_DEFAULT 30u
// #define ADC_EMA_ALPHA_MIN_DENOM_DEFAULT 32u
// #define ADC_EMA_ALPHA_MAX_DENOM_DEFAULT 4u

// // Runtime filter parameters
// static uint8_t filter_enabled = 1;
// static uint8_t filter_noise_band = ADC_EMA_NOISE_BAND_DEFAULT;
// static uint8_t filter_alpha_min_denom = ADC_EMA_ALPHA_MIN_DENOM_DEFAULT;
// static uint8_t filter_alpha_max_denom = ADC_EMA_ALPHA_MAX_DENOM_DEFAULT;

// /**
//  * MUX CONFIGURATION
//  */
// /**
//  * NUMBER OF CHANNELS PER MUX AND NUMBER OF MULTIPLEXERS
//  */
// #define NUM_MUX_CHANNELS 13
// #define NUM_MUX 8

// /**
//  * CURRENT MUX CHANNEL
//  */
// uint16_t mux_channel = 0;

/**
 * ADC DMA BUFFER
 */
// Déclarer un buffer de 128 valeurs de type uint16_t (2 octets chacune) soit
// 256 octets
// #define ADC_BUFFER_LENGTH 128
// uint16_t adc_buffer[ADC_BUFFER_LENGTH]
//     __attribute__((aligned(ADC_BUFFER_LENGTH * 2)));

// /**
//  * ADC VALUES FOR ALL MUX CHANNELS
//  * Format: [MUX0_CH0, MUX0_CH1, ..., MUX0_CH5, MUX1_CH0, ..., MUX1_CH5]
//  */
// uint16_t adc_values[NUM_MUX * NUM_MUX_CHANNELS];

/**
 * EMA state per logical channel (mux input i, mux_channel).
 */
// static adc_ema_t adc_ema_states[NUM_MUX * NUM_MUX_CHANNELS];

// /**
//  * OVERSAMPLING
//  */

// #define SAMPLE_COUNT 1

// uint16_t adc_samples_index = 0;
// uint16_t adc_samples[NUM_MUX * SAMPLE_COUNT];

/**
 * Timings measurement variables
 */
// Temps entre deux scans complets (incluant loop principale)
uint32_t adc_full_cycle_us = 0;
uint32_t adc_full_cycle_start_cycles = 0;

// Time spent in each processing task after a full ADC scan.
uint32_t task_analog_us = 0;
uint32_t task_trigger_us = 0;
uint32_t task_socd_us = 0;
uint32_t task_keyboard_us = 0;
uint32_t task_keyboard_nkro_us = 0;
uint32_t task_gamepad_us = 0;
uint32_t task_led_us = 0;
uint32_t task_total_us = 0;
uint32_t mcu_scan_cycle_us_live = 0;
uint32_t mcu_work_us_live = 0;
uint16_t mcu_load_permille_live = 0;
int16_t mcu_temperature_c_live = 0;
uint16_t mcu_vref_mv_live = 0;
uint8_t mcu_temperature_valid_live = 0;
static uint8_t timing_profile_counter = 0u;
static uint32_t mcu_metrics_next_sample_ms = 0u;
static bool mcu_led_thermal_limit_active = false;
static uint8_t mcu_led_last_applied_brightness = 0xFFu;

//--------------------------------------------------------------------+
// Filter Management Functions
//--------------------------------------------------------------------+

// /**
//  * @brief Reinitialize all EMA filters with current parameters
//  */
// static void reinit_ema_filters(void) {
//   uint16_t alpha_min = ADC_EMA_Q15_FROM_RATIO(1u, filter_alpha_min_denom);
//   uint16_t alpha_max = ADC_EMA_Q15_FROM_RATIO(1u, filter_alpha_max_denom);

//   for (uint16_t i = 0; i < (uint16_t)(NUM_MUX * NUM_MUX_CHANNELS); i++) {
//     adc_ema_init(&adc_ema_states[i], filter_noise_band, alpha_min, alpha_max);
//   }
// }

// uint8_t get_filter_enabled(void) { return filter_enabled; }

// void set_filter_enabled(uint8_t enabled) { filter_enabled = enabled ? 1 : 0; }

// void get_filter_params(uint8_t *noise_band, uint8_t *alpha_min_denom,
//                        uint8_t *alpha_max_denom) {
//   if (noise_band)
//     *noise_band = filter_noise_band;
//   if (alpha_min_denom)
//     *alpha_min_denom = filter_alpha_min_denom;
//   if (alpha_max_denom)
//     *alpha_max_denom = filter_alpha_max_denom;
// }

// void set_filter_params(uint8_t noise_band, uint8_t alpha_min_denom,
//                        uint8_t alpha_max_denom) {
//   // Validate and clamp parameters
//   filter_noise_band = noise_band > 0 ? noise_band : 1;
//   filter_alpha_min_denom = alpha_min_denom > 0 ? alpha_min_denom : 1;
//   filter_alpha_max_denom = alpha_max_denom > 0 ? alpha_max_denom : 1;

//   // Reinitialize filters with new parameters
//   reinit_ema_filters();
// }

// uint16_t get_filtered_adc_value(uint8_t key) {
//   if (key >= NUM_KEYS) {
//     return 0u;
//   }
//   return adc_values[key];
// }

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MPU_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_ADC1_Init(void);
static void MX_USB_OTG_HS_PCD_Init(void);
static void MX_TIM4_Init(void);
static void MX_TIM2_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

//============================================================================+
// TINYUSB PLATFORM FUNCTIONS
//============================================================================+

/*
 * Fonction requise par TinyUSB pour obtenir le temps en millisecondes
 *
 * TinyUSB utilise cette fonction pour:
 *   - Gérer les timeouts des transactions USB
 *   - Implémenter les délais nécessaires au protocole USB
 *   - Mesurer les intervalles entre les événements
 *
 * On utilise HAL_GetTick() qui est incrémenté dans SysTick_Handler
 */
uint32_t tusb_time_millis_api(void) { return HAL_GetTick(); }

/*
 * Fonction optionnelle pour délai en millisecondes
 * Utilisée pendant l'initialisation USB et pour certains délais de reset
 */
void tusb_time_delay_ms_api(uint32_t ms) { HAL_Delay(ms); }

#if defined(__GNUC__) || defined(__clang__)
#define KBHE_ALWAYS_INLINE __attribute__((always_inline)) static inline
#else
#define KBHE_ALWAYS_INLINE static inline
#endif

// KBHE_ALWAYS_INLINE void GPIO_WriteMasked_BSRR(GPIO_TypeDef *port,
//                                               uint16_t pins_mask,
//                                               uint16_t pins_set) {
//   const uint32_t set = (uint32_t)(pins_set & pins_mask);
//   const uint32_t reset = (uint32_t)((pins_mask & (uint16_t)~pins_set) << 16);
//   port->BSRR = set | reset;
// }

static void DWT_CycleCounter_Init(void) {
  /* Active le compteur de cycles (Cortex-M7) */
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

static uint32_t cycles_to_us(uint32_t cycles) {
  uint32_t cycles_per_us = SystemCoreClock / 1000000U;

  if (cycles_per_us == 0U) {
    return 0U;
  }

  return (cycles + (cycles_per_us / 2U)) / cycles_per_us;
}

static uint32_t smooth_u32(uint32_t previous, uint32_t sample) {
  if (previous == 0u) {
    return sample;
  }
  return (uint32_t)(((uint64_t)previous * 7u + sample + 4u) / 8u);
}

static void mcu_refresh_runtime_metrics(uint32_t scan_cycle_us,
                                        uint32_t work_us) {
  mcu_scan_cycle_us_live = smooth_u32(mcu_scan_cycle_us_live, scan_cycle_us);
  mcu_work_us_live = smooth_u32(mcu_work_us_live, work_us);

  if (mcu_scan_cycle_us_live == 0u) {
    mcu_load_permille_live = 0u;
    return;
  }

  uint32_t permille =
      ((uint32_t)mcu_work_us_live * 1000u + (mcu_scan_cycle_us_live / 2u)) /
      mcu_scan_cycle_us_live;
  if (permille > 1000u) {
    permille = 1000u;
  }
  mcu_load_permille_live = (uint16_t)permille;
}

static void mcu_init_injected_sensors(void) {
  ADC_InjectionConfTypeDef sInjectedConfig = {0};

  sInjectedConfig.InjectedOffset = 0u;
  sInjectedConfig.InjectedNbrOfConversion = 2u;
  sInjectedConfig.InjectedDiscontinuousConvMode = DISABLE;
  sInjectedConfig.AutoInjectedConv = DISABLE;
  sInjectedConfig.ExternalTrigInjecConv = ADC_INJECTED_SOFTWARE_START;
  sInjectedConfig.ExternalTrigInjecConvEdge =
      ADC_EXTERNALTRIGINJECCONVEDGE_NONE;
  sInjectedConfig.InjectedSamplingTime = ADC_SAMPLETIME_480CYCLES;

  sInjectedConfig.InjectedChannel = ADC_CHANNEL_VREFINT;
  sInjectedConfig.InjectedRank = ADC_INJECTED_RANK_1;
  if (HAL_ADCEx_InjectedConfigChannel(&hadc1, &sInjectedConfig) != HAL_OK) {
    mcu_temperature_valid_live = 0u;
    return;
  }

  sInjectedConfig.InjectedChannel = ADC_CHANNEL_TEMPSENSOR;
  sInjectedConfig.InjectedRank = ADC_INJECTED_RANK_2;
  if (HAL_ADCEx_InjectedConfigChannel(&hadc1, &sInjectedConfig) != HAL_OK) {
    mcu_temperature_valid_live = 0u;
  }
}

static void mcu_sample_internal_sensors(uint32_t now_ms) {
  if (!diagnostics_is_perf_active() &&
      !settings_is_led_thermal_protection_enabled()) {
    return;
  }
  if (now_ms < mcu_metrics_next_sample_ms) {
    return;
  }
  mcu_metrics_next_sample_ms = now_ms + 1000u;

  if (HAL_ADCEx_InjectedStart(&hadc1) != HAL_OK) {
    mcu_temperature_valid_live = 0u;
    return;
  }

  if (HAL_ADCEx_InjectedPollForConversion(&hadc1, 4u) != HAL_OK) {
    (void)HAL_ADCEx_InjectedStop(&hadc1);
    mcu_temperature_valid_live = 0u;
    return;
  }

  uint32_t vref_raw = HAL_ADCEx_InjectedGetValue(&hadc1, ADC_INJECTED_RANK_1);
  uint32_t temp_raw = HAL_ADCEx_InjectedGetValue(&hadc1, ADC_INJECTED_RANK_2);
  (void)HAL_ADCEx_InjectedStop(&hadc1);

  if (vref_raw == 0u || temp_raw == 0u) {
    mcu_temperature_valid_live = 0u;
    return;
  }

  uint32_t vref_mv =
      __LL_ADC_CALC_VREFANALOG_VOLTAGE(vref_raw, LL_ADC_RESOLUTION_12B);
  int32_t temperature_c =
      __LL_ADC_CALC_TEMPERATURE(vref_mv, temp_raw, LL_ADC_RESOLUTION_12B);

  mcu_vref_mv_live = (uint16_t)vref_mv;
  mcu_temperature_c_live = (int16_t)temperature_c;
  mcu_temperature_valid_live = 1u;
}

static void mcu_apply_led_thermal_protection(void) {
  uint8_t target_brightness = settings_get_led_brightness();
  bool protection_enabled = settings_is_led_thermal_protection_enabled();
  int16_t clear_threshold =
      (int16_t)(MCU_LED_THERMAL_LIMIT_C - MCU_LED_THERMAL_HYSTERESIS_C);

  if (!protection_enabled) {
    mcu_led_thermal_limit_active = false;
  } else if (mcu_temperature_valid_live != 0u) {
    if (mcu_led_thermal_limit_active) {
      if (mcu_temperature_c_live <= clear_threshold) {
        mcu_led_thermal_limit_active = false;
      }
    } else if (mcu_temperature_c_live > MCU_LED_THERMAL_LIMIT_C) {
      mcu_led_thermal_limit_active = true;
    }
  }

  if (mcu_led_thermal_limit_active &&
      target_brightness > MCU_LED_THERMAL_BRIGHTNESS_MAX) {
    target_brightness = MCU_LED_THERMAL_BRIGHTNESS_MAX;
  }

  if (target_brightness == mcu_led_last_applied_brightness) {
    return;
  }

  led_matrix_set_brightness(target_brightness);
  mcu_led_last_applied_brightness = target_brightness;
}

static void TIM4_StartOneShot_TRGO(void) {
  TIM4->SR = ~TIM_SR_UIF;
  TIM4->CNT = 0;
  TIM4->CR1 |= TIM_CR1_CEN;
}

static bool should_profile_timing_scan(void) {
  if (!diagnostics_is_perf_active()) {
    adc_full_cycle_us = 0u;
    task_analog_us = 0u;
    task_trigger_us = 0u;
    task_socd_us = 0u;
    task_keyboard_us = 0u;
    task_keyboard_nkro_us = 0u;
    task_gamepad_us = 0u;
    task_led_us = 0u;
    task_total_us = 0u;
    timing_profile_counter = 0u;
    return false;
  }

  bool collect_profile = (timing_profile_counter == 0u);

  timing_profile_counter++;
  if (timing_profile_counter >= KBHE_TIMING_PROFILE_DECIMATION) {
    timing_profile_counter = 0u;
  }

  return collect_profile;
}

// /**
//  * Selects the current channel on the multiplexer.
//  */
// static void MUX_SelectChannel(uint8_t channel) {
//   // uint32_t start_cycles = DWT->CYCCNT;
//   /* M0..M3 = bus d'adresse (0..15). On utilise ici 0..NUM_MUX_CHANNELS-1. */
//   if (channel >= (uint8_t)NUM_MUX_CHANNELS) {
//     channel = 0U;
//   }

//   /* Chemin critique: évite HAL_GPIO_WritePin (très coûteux) */
//   if (M0_GPIO_Port == M1_GPIO_Port) {
//     uint16_t pins_set = 0U;
//     if ((channel & 0x01U) != 0U) {
//       pins_set |= M0_Pin;
//     }
//     if ((channel & 0x02U) != 0U) {
//       pins_set |= M1_Pin;
//     }
//     GPIO_WriteMasked_BSRR(M0_GPIO_Port, (uint16_t)(M0_Pin | M1_Pin), pins_set);
//   } else {
//     GPIO_WriteMasked_BSRR(M0_GPIO_Port, M0_Pin,
//                           ((channel & 0x01U) != 0U) ? M0_Pin : 0U);
//     GPIO_WriteMasked_BSRR(M1_GPIO_Port, M1_Pin,
//                           ((channel & 0x02U) != 0U) ? M1_Pin : 0U);
//   }

//   if (M2_GPIO_Port == M3_GPIO_Port) {
//     uint16_t pins_set = 0U;
//     if ((channel & 0x04U) != 0U) {
//       pins_set |= M2_Pin;
//     }
//     if ((channel & 0x08U) != 0U) {
//       pins_set |= M3_Pin;
//     }
//     GPIO_WriteMasked_BSRR(M2_GPIO_Port, (uint16_t)(M2_Pin | M3_Pin), pins_set);
//   } else {
//     GPIO_WriteMasked_BSRR(M2_GPIO_Port, M2_Pin,
//                           ((channel & 0x04U) != 0U) ? M2_Pin : 0U);
//     GPIO_WriteMasked_BSRR(M3_GPIO_Port, M3_Pin,
//                           ((channel & 0x08U) != 0U) ? M3_Pin : 0U);
//   }

//   mux_channel = channel;

//   // uint32_t end_cycles = DWT->CYCCNT;
//   // gpio_switching_us = cycles_to_us(end_cycles - start_cycles);
// }

//============================================================================+
// WS2812 LED DMA CALLBACKS
//============================================================================+

// External reference to the WS2812 handle from led_matrix.c
extern ws2812_handleTypeDef led_ws2812_handle;

// DMA Half Transfer callback - update first half of buffer
void HAL_TIM_PWM_PulseFinishedHalfCpltCallback(TIM_HandleTypeDef *htim) {
  if (htim->Instance == TIM2) {
    ws2812_update_buffer(&led_ws2812_handle, led_ws2812_handle.dma_buffer);
  }
}

// DMA Transfer Complete callback - update second half of buffer
void HAL_TIM_PWM_PulseFinishedCallback(TIM_HandleTypeDef *htim) {
  if (htim->Instance == TIM2) {
    ws2812_update_buffer(&led_ws2812_handle,
                         &led_ws2812_handle.dma_buffer[BUFFER_SIZE]);
  }
}

/* USER CODE END 0 */

/**
 * @brief  The application entry point.
 * @retval int
 */
int main(void) {

  /* USER CODE BEGIN 1 */
  /**
   * INITIALIZE ADC VALUES ARRAY
   */
  // for (uint16_t i = 0; i < NUM_MUX * NUM_MUX_CHANNELS; i++) {
  //   adc_values[i] = 0;
  // }

  /*
   * INITIALIZE ANALOG MODULE
   */
  AnalogConfig_t analog_config = {
    .hadc = &hadc1
  };
  
  analog_init(&analog_config);

  uint16_t *adc_buffer = analog_get_adc_buffer_ptr();

  /**
   * INITIALIZE FILTER MODULE
   */
  filter_init();

  /*
   * INITIALIZE CALIBRATION MODULE
   */
  calibration_init();

  // // Initialize EMA state for each logical ADC channel using runtime parameters
  // reinit_ema_filters();
  /* USER CODE END 1 */

  /* MPU Configuration--------------------------------------------------------*/
  MPU_Config();

  /* Enable the CPU Cache */

  /* Enable I-Cache---------------------------------------------------------*/
  SCB_EnableICache();

  /* Enable D-Cache---------------------------------------------------------*/
  SCB_EnableDCache();

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick.
   */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_ADC1_Init();
  MX_USB_OTG_HS_PCD_Init();
  MX_TIM4_Init();
  MX_TIM2_Init();
  /* USER CODE BEGIN 2 */

  /*
   * INITIALIZE MULTIPLEXER MODULE
   */
  multiplexer_init();

  /*
   * INITIALIZE TRIGGER MODULE
   */
  trigger_init();

  /*
   * INITIALIZE SOCD MODULE
   */
  socd_init();

  // Affectation du buffer DMA non-cacheable au handle WS2812
  led_ws2812_handle.dma_buffer = ws2812_dma_buffer;

  // Initialize WS2812 LED Matrix (8x8 = 64 LEDs)
  if (!led_matrix_init(&htim2, TIM_CHANNEL_1)) {
    // LED init failed, but continue anyway
  }

  // Initialize keyboard lock-state tracking (used to override the Caps Lock
  // key LED inside the RGB matrix).
  led_indicator_init();

  /*
   * Load/apply persisted settings after HAL, Flash and the LED driver are
   * fully initialized, but before USB enumeration. This lets the USB
   * descriptors reflect the saved gamepad API mode on the next enumeration.
   */
  settings_init();

  // Initialisation TinyUSB - RHPORT 1 = USB HS avec PHY intégré
  const tusb_rhport_init_t rhport_init = {.role = TUSB_ROLE_DEVICE,
                                          .speed = TUSB_SPEED_HIGH};
  tusb_init(USB_RHPORT_HS, &rhport_init);

  raw_hid_init();
  consumer_hid_init();
  mouse_hid_init();
  xinput_usb_init();
  diagnostics_init();
  mcu_init_injected_sensors();

  // triggerInit();

  rotary_encoder_init();

  DWT_CycleCounter_Init();

  multiplexer_select_mux_channel(0U);

  adc_full_cycle_start_cycles = DWT->CYCCNT;
  if (HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adc_buffer, NUM_MUX) != HAL_OK) {
    Error_Handler();
  }
  TIM4_StartOneShot_TRGO();

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1) {
    bool processed_scan = false;
    bool profile_timing = false;
    uint32_t task_start_cycles = 0;
    uint32_t live_task_start_cycles = 0;
    uint32_t live_scan_cycle_us = 0;

    tud_task(); // TinyUSB device task
    raw_hid_task();
    consumer_hid_task();
    mouse_hid_task();
    updater_app_task();

    // If a full ADC scan is complete, process keys and restart
    if (analog_is_scan_complete()) {
      uint32_t now = DWT->CYCCNT;
      live_scan_cycle_us = cycles_to_us(now - adc_full_cycle_start_cycles);
      profile_timing = should_profile_timing_scan();
      live_task_start_cycles = now;
      if (profile_timing) {
        adc_full_cycle_us = live_scan_cycle_us;
        task_start_cycles = now;
      }
      adc_full_cycle_start_cycles = now;
      processed_scan = true;

      if (profile_timing) {
        uint32_t step_start_cycles = DWT->CYCCNT;
        analog_task();
        task_analog_us = cycles_to_us(DWT->CYCCNT - step_start_cycles);

        calibration_guided_on_scan(HAL_GetTick());

        step_start_cycles = DWT->CYCCNT;
        trigger_task();
        task_trigger_us = cycles_to_us(DWT->CYCCNT - step_start_cycles);

        step_start_cycles = DWT->CYCCNT;
        socd_task();
        task_socd_us = cycles_to_us(DWT->CYCCNT - step_start_cycles);

        step_start_cycles = DWT->CYCCNT;
        keyboard_hid_task();
        task_keyboard_us = cycles_to_us(DWT->CYCCNT - step_start_cycles);

        step_start_cycles = DWT->CYCCNT;
        keyboard_nkro_hid_task();
        task_keyboard_nkro_us = cycles_to_us(DWT->CYCCNT - step_start_cycles);

        step_start_cycles = DWT->CYCCNT;
        gamepad_hid_refresh_state();
        gamepad_hid_task();
        xinput_usb_task();
        task_gamepad_us = cycles_to_us(DWT->CYCCNT - step_start_cycles);
      } else {
        analog_task();
        calibration_guided_on_scan(HAL_GetTick());
        trigger_task();
        socd_task();
        keyboard_hid_task();
        keyboard_nkro_hid_task();
        gamepad_hid_refresh_state();
        gamepad_hid_task();
        xinput_usb_task();
      }

      analog_set_scan_complete(false);
      TIM4_StartOneShot_TRGO();
    }

    // LED/UI timing should not depend on ADC scan state. The scan completes so
    // quickly on the 82-key board that gating animation updates behind the
    // "no scan complete" branch effectively starves LED effects.
    uint32_t now_ms = HAL_GetTick();
    mcu_sample_internal_sensors(now_ms);
    uint32_t led_start_cycles = 0u;
    if (profile_timing) {
      led_start_cycles = DWT->CYCCNT;
    }
    calibration_guided_tick(now_ms);
    rotary_encoder_task(now_ms);
    led_matrix_effect_tick(now_ms);
    led_indicator_tick(now_ms);
    settings_task(now_ms);
    mcu_apply_led_thermal_protection();
    if (profile_timing) {
      task_led_us = cycles_to_us(DWT->CYCCNT - led_start_cycles);
    }

    if (processed_scan && profile_timing) {
      task_total_us = cycles_to_us(DWT->CYCCNT - task_start_cycles);
    }
    if (processed_scan && diagnostics_is_perf_active()) {
      uint32_t live_work_us = cycles_to_us(DWT->CYCCNT - live_task_start_cycles);
      mcu_refresh_runtime_metrics(live_scan_cycle_us, live_work_us);
    }
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
 * @brief System Clock Configuration
 * @retval None
 */
void SystemClock_Config(void) {
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
   */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
   * in the RCC_OscInitTypeDef structure.
   */
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

  /** Activate the Over-Drive mode
   */
  if (HAL_PWREx_EnableOverDrive() != HAL_OK) {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
   */
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

/**
 * @brief ADC1 Initialization Function
 * @param None
 * @retval None
 */
static void MX_ADC1_Init(void) {

  /* USER CODE BEGIN ADC1_Init 0 */

  /* USER CODE END ADC1_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC1_Init 1 */

  /* USER CODE END ADC1_Init 1 */

  /** Configure the global features of the ADC (Clock, Resolution, Data
   * Alignment and number of conversion)
   */
  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
  hadc1.Init.Resolution = ADC_RESOLUTION_12B;
  hadc1.Init.ScanConvMode = ADC_SCAN_ENABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_RISING;
  hadc1.Init.ExternalTrigConv = ADC_EXTERNALTRIGCONV_T4_TRGO;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.NbrOfConversion = 11;
  hadc1.Init.DMAContinuousRequests = ENABLE;
  hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  if (HAL_ADC_Init(&hadc1) != HAL_OK) {
    Error_Handler();
  }

  /** Configure for the selected ADC regular channel its corresponding rank in
   * the sequencer and its sample time.
   */
  sConfig.SamplingTime = ADC_SAMPLETIME_3CYCLES;

  sConfig.Channel = ADC_CHANNEL_1;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) {
    Error_Handler();
  }

  sConfig.Channel = ADC_CHANNEL_2;
  sConfig.Rank = ADC_REGULAR_RANK_2;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) {
    Error_Handler();
  }

  /** Configure for the selected ADC regular channel its corresponding rank in
   * the sequencer and its sample time.
   */
  sConfig.Channel = ADC_CHANNEL_3;
  sConfig.Rank = ADC_REGULAR_RANK_3;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) {
    Error_Handler();
  }

  /** Configure for the selected ADC regular channel its corresponding rank in
   * the sequencer and its sample time.
   */
  sConfig.Channel = ADC_CHANNEL_4;
  sConfig.Rank = ADC_REGULAR_RANK_4;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) {
    Error_Handler();
  }

  /** Configure for the selected ADC regular channel its corresponding rank in
   * the sequencer and its sample time.
   */
  sConfig.Channel = ADC_CHANNEL_5;
  sConfig.Rank = ADC_REGULAR_RANK_5;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) {
    Error_Handler();
  }

  /** Configure for the selected ADC regular channel its corresponding rank in
   * the sequencer and its sample time.
   */
  sConfig.Channel = ADC_CHANNEL_6;
  sConfig.Rank = ADC_REGULAR_RANK_6;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) {
    Error_Handler();
  }

  /** Configure for the selected ADC regular channel its corresponding rank in
   * the sequencer and its sample time.
   */
  sConfig.Channel = ADC_CHANNEL_7;
  sConfig.Rank = ADC_REGULAR_RANK_7;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) {
    Error_Handler();
  }

  /** Configure for the selected ADC regular channel its corresponding rank in
   * the sequencer and its sample time.
   */
  sConfig.Channel = ADC_CHANNEL_8;
  sConfig.Rank = ADC_REGULAR_RANK_8;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) {
    Error_Handler();
  }

  /** Configure for the selected ADC regular channel its corresponding rank in
   * the sequencer and its sample time.
   */
  sConfig.Channel = ADC_CHANNEL_9;
  sConfig.Rank = ADC_REGULAR_RANK_9;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) {
    Error_Handler();
  }

  /** Configure for the selected ADC regular channel its corresponding rank in
   * the sequencer and its sample time.
   */
  sConfig.Channel = ADC_CHANNEL_14;
  sConfig.Rank = ADC_REGULAR_RANK_10;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) {
    Error_Handler();
  }

  sConfig.Channel = ADC_CHANNEL_15;
  sConfig.Rank = ADC_REGULAR_RANK_11;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) {
    Error_Handler();
  }

  /* USER CODE BEGIN ADC1_Init 2 */

  /* USER CODE END ADC1_Init 2 */
}

/**
 * @brief TIM2 Initialization Function
 * @param None
 * @retval None
 */
static void MX_TIM2_Init(void) {

  /* USER CODE BEGIN TIM2_Init 0 */

  /* USER CODE END TIM2_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  /* USER CODE BEGIN TIM2_Init 1 */

  /* USER CODE END TIM2_Init 1 */
  htim2.Instance = TIM2;
  htim2.Init.Prescaler = 0;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = 134;
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
  if (HAL_TIM_Base_Init(&htim2) != HAL_OK) {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim2, &sClockSourceConfig) != HAL_OK) {
    Error_Handler();
  }
  if (HAL_TIM_PWM_Init(&htim2) != HAL_OK) {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig) != HAL_OK) {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim2, &sConfigOC, TIM_CHANNEL_1) != HAL_OK) {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM2_Init 2 */

  /* USER CODE END TIM2_Init 2 */
  HAL_TIM_MspPostInit(&htim2);
}

/**
 * @brief TIM4 Initialization Function
 * @param None
 * @retval None
 */
static void MX_TIM4_Init(void) {

  /* USER CODE BEGIN TIM4_Init 0 */

  /* USER CODE END TIM4_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM4_Init 1 */

  /* USER CODE END TIM4_Init 1 */
  htim4.Instance = TIM4;
  htim4.Init.Prescaler = 0;
  htim4.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim4.Init.Period = 53;
  htim4.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim4.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
  if (HAL_TIM_Base_Init(&htim4) != HAL_OK) {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim4, &sClockSourceConfig) != HAL_OK) {
    Error_Handler();
  }
  if (HAL_TIM_OnePulse_Init(&htim4, TIM_OPMODE_SINGLE) != HAL_OK) {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_UPDATE;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim4, &sMasterConfig) != HAL_OK) {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM4_Init 2 */

  /* USER CODE END TIM4_Init 2 */
}

/**
 * @brief USB_OTG_HS Initialization Function
 * @param None
 * @retval None
 */
static void MX_USB_OTG_HS_PCD_Init(void) {

  /* USER CODE BEGIN USB_OTG_HS_Init 0 */

  /* USER CODE END USB_OTG_HS_Init 0 */

  /* USER CODE BEGIN USB_OTG_HS_Init 1 */
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /*
   * ==========================================================================
   * Configuration minimale pour TinyUSB - il fait le reste!
   * ==========================================================================
   */

  // 1. Activer les clocks
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_OTGPHYC_CLK_ENABLE(); // Clock du PHY HS intégré
  __HAL_RCC_USB_OTG_HS_CLK_ENABLE();
  __HAL_RCC_USB_OTG_HS_ULPI_CLK_ENABLE(); // Requis pour core reset même avec
                                          // PHY intégré!

  // Désactiver ULPI en mode Low-Power
#if defined(RCC_AHB1LPENR_OTGHSULPILPEN)
  RCC->AHB1LPENR &= ~RCC_AHB1LPENR_OTGHSULPILPEN;
#endif

  // 2. Configurer les GPIOs USB HS (PB14=DM, PB15=DP)
  GPIO_InitStruct.Pin = GPIO_PIN_14 | GPIO_PIN_15;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF12_OTG_HS_FS;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  // 3. Configurer les interruptions
  HAL_NVIC_SetPriority(OTG_HS_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(OTG_HS_IRQn);

  // TinyUSB fera le reste via tusb_init() et dwc2_phy_init()
  return;
  /* USER CODE END USB_OTG_HS_Init 1 */
  hpcd_USB_OTG_HS.Instance = USB_OTG_HS;
  hpcd_USB_OTG_HS.Init.dev_endpoints = 9;
  hpcd_USB_OTG_HS.Init.dma_enable = DISABLE;
  hpcd_USB_OTG_HS.Init.phy_itface = USB_OTG_HS_EMBEDDED_PHY;
  hpcd_USB_OTG_HS.Init.Sof_enable = DISABLE;
  hpcd_USB_OTG_HS.Init.low_power_enable = DISABLE;
  hpcd_USB_OTG_HS.Init.lpm_enable = DISABLE;
  hpcd_USB_OTG_HS.Init.vbus_sensing_enable = DISABLE;
  hpcd_USB_OTG_HS.Init.use_dedicated_ep1 = DISABLE;
  hpcd_USB_OTG_HS.Init.use_external_vbus = DISABLE;
  if (HAL_PCD_Init(&hpcd_USB_OTG_HS) != HAL_OK) {
    Error_Handler();
  }
  /* USER CODE BEGIN USB_OTG_HS_Init 2 */

  /* USER CODE END USB_OTG_HS_Init 2 */
}

/**
 * Enable DMA controller clock
 */
static void MX_DMA_Init(void) {

  /* DMA controller clock enable */
  __HAL_RCC_DMA2_CLK_ENABLE();
  __HAL_RCC_DMA1_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA1_Stream5_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Stream5_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Stream5_IRQn);
  /* DMA2_Stream0_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA2_Stream0_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA2_Stream0_IRQn);
}

/**
 * @brief GPIO Initialization Function
 * @param None
 * @retval None
 */
static void MX_GPIO_Init(void) {
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(M0_GPIO_Port, M0_Pin | M1_Pin | M2_Pin, GPIO_PIN_RESET);

  // /*Configure GPIO pins : M3_Pin M2_Pin */
  // GPIO_InitStruct.Pin = M3_Pin | M2_Pin;
  // GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  // GPIO_InitStruct.Pull = GPIO_NOPULL;
  // GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  // HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pins : M2_Pin M1_Pin M0_Pin */
  GPIO_InitStruct.Pin = M0_Pin | M1_Pin | M2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  HAL_GPIO_Init(M0_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : ENCODER_SW_Pin ENCODER_WAVE_1_Pin ENCODER_WAVE_2_Pin */
  GPIO_InitStruct.Pin = ENCODER_SW_Pin | ENCODER_WAVE_1_Pin | ENCODER_WAVE_2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
// void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc) {
//   /**
//    * When an ADC burst is complete, store the values and switch to the next MUX
//    * channel
//    */
//   if (hadc->Instance == ADC1) {
//     adc_callback_start_cycles = DWT->CYCCNT;

//     // Increment number of conversions completed
//     num_conv++;

//     // Store ADC values for all MUX
//     for (uint8_t i = 0; i < NUM_MUX; i++) {
//       /**
//        * MUX0_CH0, MUX0_CH1, ..., MUX0_CH13, MUX1_CH0, ..., MUX7_CH13
//        */
//       uint16_t new_adc_value = adc_buffer[i];

//       uint16_t logical_index = (uint16_t)(mux_channel + (i * NUM_MUX_CHANNELS));

//       // Store raw value
//       adc_values_raw[logical_index] = new_adc_value;
//     }

//     // Increment MUX channel
//     mux_channel++;

//     // If all channels have been read, reset to channel 0
//     if (mux_channel >= NUM_MUX_CHANNELS) {
//       mux_channel = 0U;

//       // Signal that a full scan is complete - let main loop restart
//       adc_scan_complete = 1;
//       return; // Don't restart timer here, let main loop do it
//     }

//     // Select next MUX channel
//     MUX_SelectChannel(mux_channel);

//     // Start 0.5us timer to wait for MUX settling time then trigger ADC
//     // Only restart immediately - the main loop will have time between full
//     // scans because the DMA priority is lower than USB
//     TIM4_StartOneShot_TRGO();

//     adc_callback_end_cycles = DWT->CYCCNT;
//     adc_callback_us =
//         cycles_to_us(adc_callback_end_cycles - adc_callback_start_cycles);
//   }
// }

/* USER CODE END 4 */

/* MPU Configuration */

void MPU_Config(void) {
  MPU_Region_InitTypeDef MPU_InitStruct = {0};

  /* Disables the MPU */
  HAL_MPU_Disable();

  /** Initializes and configures the Region and the memory to be protected
   */
  MPU_InitStruct.Enable = MPU_REGION_ENABLE;
  MPU_InitStruct.Number = MPU_REGION_NUMBER0;
  MPU_InitStruct.BaseAddress = (uint32_t)(uintptr_t)analog_get_adc_buffer_ptr();
  MPU_InitStruct.Size = MPU_REGION_SIZE_256B;
  MPU_InitStruct.SubRegionDisable = 0x00;
  MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL0;
  MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_ENABLE;
  MPU_InitStruct.IsShareable = MPU_ACCESS_SHAREABLE;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
  MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  /** Initializes and configures the Region and the memory to be protected
   */
  MPU_InitStruct.Number = MPU_REGION_NUMBER1;
  MPU_InitStruct.BaseAddress = (uint32_t)&ws2812_dma_buffer;
  MPU_InitStruct.Size = MPU_REGION_SIZE_256B;
  MPU_InitStruct.SubRegionDisable = 0x0;
  MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL0;
  MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_ENABLE;
  MPU_InitStruct.IsShareable = MPU_ACCESS_SHAREABLE;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
  MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);
  /* Enables the MPU */
  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}

/**
 * @brief  This function is executed in case of error occurrence.
 * @retval None
 */
void Error_Handler(void) {
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1) {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
 * @brief  Reports the name of the source file and the source line number
 *         where the assert_param error has occurred.
 * @param  file: pointer to the source file name
 * @param  line: assert_param error line source number
 * @retval None
 */
void assert_failed(uint8_t *file, uint32_t line) {
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line
     number, ex: printf("Wrong parameters value: file %s on line %d\r\n", file,
     line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
