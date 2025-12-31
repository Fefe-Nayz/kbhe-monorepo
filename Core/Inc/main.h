/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file           : main.h
 * @brief          : Header for main.c file.
 *                   This file contains the common defines of the application.
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f7xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

void HAL_TIM_MspPostInit(TIM_HandleTypeDef *htim);

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define LED_DATA_Pin GPIO_PIN_7
#define LED_DATA_GPIO_Port GPIOC
#define M3_Pin GPIO_PIN_11
#define M3_GPIO_Port GPIOC
#define M2_Pin GPIO_PIN_12
#define M2_GPIO_Port GPIOC
#define M1_Pin GPIO_PIN_0
#define M1_GPIO_Port GPIOD
#define M0_Pin GPIO_PIN_1
#define M0_GPIO_Port GPIOD

/* USER CODE BEGIN Private defines */
// // WS2812 LED timing: Timer period for 800kHz PWM (96MHz / 120 = 800kHz)
// #define LED_CNT 120

// // WS2812 output pin (TIM3_CH2 = PC7)
// #define WS2812_Pin GPIO_PIN_7
// #define WS2812_GPIO_Port GPIOC

// Timing measurement (accessible from other modules)
extern uint32_t adc_full_cycle_us; // Time for full main loop cycle
extern uint32_t adc_total_scan_us; // Time for ADC scan only

// ADC EMA Filter control functions
uint8_t get_filter_enabled(void);
void set_filter_enabled(uint8_t enabled);
void get_filter_params(uint8_t *noise_band, uint8_t *alpha_min_denom, uint8_t *alpha_max_denom);
void set_filter_params(uint8_t noise_band, uint8_t alpha_min_denom, uint8_t alpha_max_denom);
/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
