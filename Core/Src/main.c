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
#include "adc_ema.h"
#include "stm32f7xx_hal_adc.h"
#include "trigger.h"
#include "tusb.h"
#include "usb_hid.h"
#include <stdint.h>
#include <stdio.h>

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;
DMA_HandleTypeDef hdma_adc1;

TIM_HandleTypeDef htim3;
TIM_HandleTypeDef htim4;

PCD_HandleTypeDef hpcd_USB_OTG_HS;

/* USER CODE BEGIN PV */
/**
 * ADC DMA NUMBER OF CONVERSIONS COMPLETED
 */
uint32_t num_conv = 0;

/**
 * EMA FILTERING
 */
#define ADC_EMA_NOISE_BAND 30u
#define ADC_EMA_ALPHA_MIN_Q15 ADC_EMA_Q15_FROM_RATIO(1u, 32u)
#define ADC_EMA_ALPHA_MAX_Q15 ADC_EMA_Q15_FROM_RATIO(1u, 4u)

/**
 * MUX CONFIGURATION
 */
/**
 * NUMBER OF CHANNELS PER MUX AND NUMBER OF MULTIPLEXERS
 */
#define NUM_MUX_CHANNELS 13
#define NUM_MUX 8

/**
 * CURRENT MUX CHANNEL
 */
uint16_t mux_channel = 0;

/**
 * ADC DMA BUFFER
 */
uint16_t adc_buffer[NUM_MUX];

/**
 * ADC VALUES FOR ALL MUX CHANNELS
 * Format: [MUX0_CH0, MUX0_CH1, ..., MUX0_CH5, MUX1_CH0, ..., MUX1_CH5]
 */
uint16_t adc_values[NUM_MUX * NUM_MUX_CHANNELS];

/**
 * EMA state per logical channel (mux input i, mux_channel).
 */
static adc_ema_t adc_ema_states[NUM_MUX * NUM_MUX_CHANNELS];

/**
 * OVERSAMPLING
 */

#define SAMPLE_COUNT 1

uint16_t adc_samples_index = 0;
uint16_t adc_samples[NUM_MUX * SAMPLE_COUNT];

/**
 * Timings measurement variables
 */
// Temps de scan total (conversions ADC seulement)
uint32_t adc_total_scan_us = 0;
uint32_t adc_total_scan_start_cycles = 0;
uint32_t adc_total_scan_end_cycles = 0;
// Temps entre deux scans complets (incluant loop principale)
uint32_t adc_full_cycle_us = 0;
uint32_t adc_full_cycle_start_cycles = 0;
// Temps de conversion ADC
uint32_t adc_callback_us = 0;
uint32_t adc_callback_start_cycles = 0;
uint32_t adc_callback_end_cycles = 0;
// Temps de commutation GPIO
uint32_t gpio_switching_us = 0;

/**
 * Liste des valeurs pour la touche 1
 */
uint16_t key_1_values_raw = 0;
uint16_t key_1_values_filtered = 0;
// uint16_t key_1_values_index = 0;
// uint16_t key_1_values[512];
// uint16_t key_1_values_filtered[512];
// uint32_t key_2_values_index = 0;
// uint16_t key_2_values[512];
// uint32_t key_5_values_index = 0;
// uint16_t key_5_values[512];
// uint16_t key_gnd_values_index = 0;
// uint16_t key_gnd_values[512];
// uint16_t key_high_values_index = 0;
// uint16_t key_high_values[512];
/**
 * Flag to indicate a full scan is complete and main loop can process
 */
static volatile uint8_t adc_scan_complete = 0;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MPU_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_ADC1_Init(void);
static void MX_USB_OTG_HS_PCD_Init(void);
static void MX_TIM3_Init(void);
static void MX_TIM4_Init(void);
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

KBHE_ALWAYS_INLINE void GPIO_WriteMasked_BSRR(GPIO_TypeDef *port,
                                              uint16_t pins_mask,
                                              uint16_t pins_set) {
  const uint32_t set = (uint32_t)(pins_set & pins_mask);
  const uint32_t reset = (uint32_t)((pins_mask & (uint16_t)~pins_set) << 16);
  port->BSRR = set | reset;
}

static void DWT_CycleCounter_Init(void) {
  /* Active le compteur de cycles (Cortex-M7) */
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

static uint32_t cycles_to_us(uint32_t cycles) {
  if (SystemCoreClock == 0U) {
    return 0U;
  }
  // Calcul en 64 bits pour éviter l'overflow lors de la multiplication
  return (uint32_t)(((uint64_t)cycles * 1000000ULL) /
                    (uint64_t)SystemCoreClock);
}

static void TIM4_StartOneShot_TRGO(void) {
  TIM4->SR = ~TIM_SR_UIF;
  TIM4->CNT = 0;
  TIM4->CR1 |= TIM_CR1_CEN;
}

/**
 * Selects the current channel on the multiplexer.
 */
static void MUX_SelectChannel(uint8_t channel) {
  // uint32_t start_cycles = DWT->CYCCNT;
  /* M0..M3 = bus d'adresse (0..15). On utilise ici 0..NUM_MUX_CHANNELS-1. */
  if (channel >= (uint8_t)NUM_MUX_CHANNELS) {
    channel = 0U;
  }

  /* Chemin critique: évite HAL_GPIO_WritePin (très coûteux) */
  if (M0_GPIO_Port == M1_GPIO_Port) {
    uint16_t pins_set = 0U;
    if ((channel & 0x01U) != 0U) {
      pins_set |= M0_Pin;
    }
    if ((channel & 0x02U) != 0U) {
      pins_set |= M1_Pin;
    }
    GPIO_WriteMasked_BSRR(M0_GPIO_Port, (uint16_t)(M0_Pin | M1_Pin), pins_set);
  } else {
    GPIO_WriteMasked_BSRR(M0_GPIO_Port, M0_Pin,
                          ((channel & 0x01U) != 0U) ? M0_Pin : 0U);
    GPIO_WriteMasked_BSRR(M1_GPIO_Port, M1_Pin,
                          ((channel & 0x02U) != 0U) ? M1_Pin : 0U);
  }

  if (M2_GPIO_Port == M3_GPIO_Port) {
    uint16_t pins_set = 0U;
    if ((channel & 0x04U) != 0U) {
      pins_set |= M2_Pin;
    }
    if ((channel & 0x08U) != 0U) {
      pins_set |= M3_Pin;
    }
    GPIO_WriteMasked_BSRR(M2_GPIO_Port, (uint16_t)(M2_Pin | M3_Pin), pins_set);
  } else {
    GPIO_WriteMasked_BSRR(M2_GPIO_Port, M2_Pin,
                          ((channel & 0x04U) != 0U) ? M2_Pin : 0U);
    GPIO_WriteMasked_BSRR(M3_GPIO_Port, M3_Pin,
                          ((channel & 0x08U) != 0U) ? M3_Pin : 0U);
  }

  mux_channel = channel;

  // uint32_t end_cycles = DWT->CYCCNT;
  // gpio_switching_us = cycles_to_us(end_cycles - start_cycles);
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
  for (uint16_t i = 0; i < NUM_MUX * NUM_MUX_CHANNELS; i++) {
    adc_values[i] = 0;
  }

  // Initialize EMA state for each logical ADC channel.
  for (uint16_t i = 0; i < (uint16_t)(NUM_MUX * NUM_MUX_CHANNELS); i++) {
    adc_ema_init(&adc_ema_states[i], (uint16_t)ADC_EMA_NOISE_BAND,
                 (uint16_t)ADC_EMA_ALPHA_MIN_Q15,
                 (uint16_t)ADC_EMA_ALPHA_MAX_Q15);
  }
  /* USER CODE END 1 */

  /* MPU Configuration--------------------------------------------------------*/
  MPU_Config();

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
  MX_TIM3_Init();
  MX_TIM4_Init();

  /* USER CODE BEGIN 2 */

  // Initialisation TinyUSB - RHPORT 1 = USB HS avec PHY intégré
  const tusb_rhport_init_t rhport_init = {.role = TUSB_ROLE_DEVICE,
                                          .speed = TUSB_SPEED_HIGH};
  tusb_init(1, &rhport_init);
  // tusb_init();

  DWT_CycleCounter_Init();

  MUX_SelectChannel(0);

  adc_total_scan_start_cycles = DWT->CYCCNT;
  adc_full_cycle_start_cycles = DWT->CYCCNT;
  if (HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adc_buffer, NUM_MUX) != HAL_OK) {
    Error_Handler();
  }
  TIM4_StartOneShot_TRGO();

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1) {
    // TinyUSB device task - DOIT être appelé régulièrement
    tud_task();

    // Optionnel: tâche HID pour gérer les envois de rapport
    usb_hid_task();

    // If a full ADC scan is complete, restart it
    if (adc_scan_complete) {
      adc_scan_complete = 0;

      // Measure time since last scan start (full cycle time)
      uint32_t now = DWT->CYCCNT;
      adc_full_cycle_us = cycles_to_us(now - adc_full_cycle_start_cycles);
      adc_full_cycle_start_cycles = now;

      MUX_SelectChannel(0);
      TIM4_StartOneShot_TRGO();
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
  hadc1.Init.NbrOfConversion = 8;
  hadc1.Init.DMAContinuousRequests = ENABLE;
  hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  if (HAL_ADC_Init(&hadc1) != HAL_OK) {
    Error_Handler();
  }

  /** Configure for the selected ADC regular channel its corresponding rank in
   * the sequencer and its sample time.
   */
  sConfig.Channel = ADC_CHANNEL_10;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_3CYCLES;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) {
    Error_Handler();
  }

  /** Configure for the selected ADC regular channel its corresponding rank in
   * the sequencer and its sample time.
   */
  sConfig.Channel = ADC_CHANNEL_11;
  sConfig.Rank = ADC_REGULAR_RANK_2;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) {
    Error_Handler();
  }

  /** Configure for the selected ADC regular channel its corresponding rank in
   * the sequencer and its sample time.
   */
  sConfig.Channel = ADC_CHANNEL_12;
  sConfig.Rank = ADC_REGULAR_RANK_3;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) {
    Error_Handler();
  }

  /** Configure for the selected ADC regular channel its corresponding rank in
   * the sequencer and its sample time.
   */
  sConfig.Channel = ADC_CHANNEL_13;
  sConfig.Rank = ADC_REGULAR_RANK_4;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) {
    Error_Handler();
  }

  /** Configure for the selected ADC regular channel its corresponding rank in
   * the sequencer and its sample time.
   */
  sConfig.Channel = ADC_CHANNEL_0;
  sConfig.Rank = ADC_REGULAR_RANK_5;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) {
    Error_Handler();
  }

  /** Configure for the selected ADC regular channel its corresponding rank in
   * the sequencer and its sample time.
   */
  sConfig.Channel = ADC_CHANNEL_1;
  sConfig.Rank = ADC_REGULAR_RANK_6;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) {
    Error_Handler();
  }

  /** Configure for the selected ADC regular channel its corresponding rank in
   * the sequencer and its sample time.
   */
  sConfig.Channel = ADC_CHANNEL_2;
  sConfig.Rank = ADC_REGULAR_RANK_7;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) {
    Error_Handler();
  }

  /** Configure for the selected ADC regular channel its corresponding rank in
   * the sequencer and its sample time.
   */
  sConfig.Channel = ADC_CHANNEL_3;
  sConfig.Rank = ADC_REGULAR_RANK_8;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC1_Init 2 */

  /* USER CODE END ADC1_Init 2 */
}

/**
 * @brief TIM3 Initialization Function
 * @param None
 * @retval None
 */
static void MX_TIM3_Init(void) {

  /* USER CODE BEGIN TIM3_Init 0 */

  /* USER CODE END TIM3_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM3_Init 1 */

  /* USER CODE END TIM3_Init 1 */
  htim3.Instance = TIM3;
  htim3.Init.Prescaler = 107;
  htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim3.Init.Period = 124;
  htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
  if (HAL_TIM_Base_Init(&htim3) != HAL_OK) {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim3, &sClockSourceConfig) != HAL_OK) {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim3, &sMasterConfig) != HAL_OK) {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM3_Init 2 */

  /* USER CODE END TIM3_Init 2 */
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

  /* DMA interrupt init */
  /* DMA2_Stream0_IRQn interrupt configuration */
  /* Priority 2 (lower than USB at 0) so USB can preempt ADC DMA */
  HAL_NVIC_SetPriority(DMA2_Stream0_IRQn, 2, 0);
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
  HAL_GPIO_WritePin(GPIOC, M3_Pin | M2_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOD, M1_Pin | M0_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pins : M3_Pin M2_Pin */
  GPIO_InitStruct.Pin = M3_Pin | M2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pins : M1_Pin M0_Pin */
  GPIO_InitStruct.Pin = M1_Pin | M0_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc) {
  /**
   * When an ADC burst is complete, store the values and switch to the next MUX
   * channel
   */
  if (hadc->Instance == ADC1) {
    adc_callback_start_cycles = DWT->CYCCNT;

    // Increment number of conversions completed
    num_conv++;

    // // Oversampling handling
    // if (SAMPLE_COUNT > 1) {
    //   // When we have enough samples, process them
    //   if (adc_samples_index >= SAMPLE_COUNT) {
    //     adc_samples_index = 0U;
    //     // Average samples
    //     uint16_t sum0 = 0U;
    //     uint16_t sum1 = 0U;

    //     for (uint16_t i = 0; i < SAMPLE_COUNT; i++) {
    //       sum0 += adc_samples[i];
    //       sum1 += adc_samples[SAMPLE_COUNT + i];
    //     }

    //     uint16_t avg0 = (uint16_t)sum0 / SAMPLE_COUNT;
    //     uint16_t avg1 = (uint16_t)sum1 / SAMPLE_COUNT;

    //     // Store ADC values
    //     adc_values[mux_channel] = avg0;
    //     adc_values[NUM_MUX_CHANNELS + mux_channel] = avg1;
    //   } else {
    //     // Store samples for oversampling
    //     adc_samples[adc_samples_index] = adc_buffer[0];
    //     adc_samples[SAMPLE_COUNT + adc_samples_index] = adc_buffer[1];
    //     adc_samples_index++;
    //     TIM4_StartOneShot_TRGO();
    //     return;
    //   }
    // }

    // Store ADC values for all MUX
    for (uint8_t i = 0; i < NUM_MUX; i++) {
      /**
       * MUX0_CH0, MUX0_CH1, ..., MUX0_CH13, MUX1_CH0, ..., MUX7_CH13
       */
      uint16_t new_adc_value = adc_buffer[i];

      const uint16_t logical_index =
          (uint16_t)(mux_channel + (i * NUM_MUX_CHANNELS));
      const uint16_t filtered_adc_value =
          adc_ema_update(&adc_ema_states[logical_index], new_adc_value);

      /* Store key 1 and key 2 values for analysis */
      if (mux_channel == 0 && i == 0) {
        // if (key_1_values_index >= 512) {
        //   key_1_values_index = 0;
        // }

        // const uint16_t k = key_1_values_index;
        // key_1_values[k] = new_adc_value;                // raw
        // key_1_values_filtered[k] = filtered_adc_value;  // filtered
        // key_1_values_index = (uint16_t)(key_1_values_index + 1u);
        key_1_values_raw = new_adc_value;
        key_1_values_filtered = filtered_adc_value;
      }

      /* END ANALYSIS */

      // Store filtered value
      adc_values[logical_index] = filtered_adc_value;
    }

    // Increment MUX channel
    mux_channel++;

    // If all channels have been read, reset to channel 0
    if (mux_channel >= NUM_MUX_CHANNELS) {
      mux_channel = 0U;

      adc_total_scan_end_cycles = DWT->CYCCNT;
      adc_total_scan_us =
          cycles_to_us(adc_total_scan_end_cycles - adc_total_scan_start_cycles);

      adc_total_scan_start_cycles = DWT->CYCCNT;

      // Signal that a full scan is complete - let main loop restart
      adc_scan_complete = 1;
      return; // Don't restart timer here, let main loop do it
    }

    // Select next MUX channel
    MUX_SelectChannel(mux_channel);

    // Start 0.5us timer to wait for MUX settling time then trigger ADC
    // Only restart immediately - the main loop will have time between full
    // scans because the DMA priority is lower than USB
    TIM4_StartOneShot_TRGO();

    adc_callback_end_cycles = DWT->CYCCNT;
    adc_callback_us =
        cycles_to_us(adc_callback_end_cycles - adc_callback_start_cycles);
  }
}

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
  MPU_InitStruct.BaseAddress = 0x0;
  MPU_InitStruct.Size = MPU_REGION_SIZE_4GB;
  MPU_InitStruct.SubRegionDisable = 0x87;
  MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL0;
  MPU_InitStruct.AccessPermission = MPU_REGION_NO_ACCESS;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
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
