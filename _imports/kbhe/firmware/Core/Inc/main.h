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
#define LED_DATA_Pin GPIO_PIN_15
#define LED_DATA_GPIO_Port GPIOA
#define ENCODER_SW_Pin GPIO_PIN_3
#define ENCODER_SW_GPIO_Port GPIOB
#define ENCODER_WAVE_1_Pin GPIO_PIN_5
#define ENCODER_WAVE_1_GPIO_Port GPIOB
#define ENCODER_WAVE_2_Pin GPIO_PIN_4
#define ENCODER_WAVE_2_GPIO_Port GPIOB
#define M2_Pin GPIO_PIN_6
#define M2_GPIO_Port GPIOD
#define M1_Pin GPIO_PIN_5
#define M1_GPIO_Port GPIOD
#define M0_Pin GPIO_PIN_4
#define M0_GPIO_Port GPIOD

/* USER CODE BEGIN Private defines */
// // WS2812 LED timing: Timer period for 800kHz PWM (96MHz / 120 = 800kHz)
// #define LED_CNT 120

// // WS2812 output pin (TIM2_CH1 = PA15)
// #define WS2812_Pin GPIO_PIN_15
// #define WS2812_GPIO_Port GPIOA

// Timing measurement (accessible from other modules)
extern uint32_t adc_full_cycle_us; // Time for full main loop cycle
extern uint32_t adc_total_scan_us; // Time for ADC scan only
extern uint32_t task_analog_us;
extern uint32_t task_trigger_us;
extern uint32_t task_socd_us;
extern uint32_t task_keyboard_us;
extern uint32_t task_keyboard_nkro_us;
extern uint32_t task_gamepad_us;
extern uint32_t task_led_us;
extern uint32_t task_total_us;
extern uint32_t mcu_scan_cycle_us_live;
extern uint32_t mcu_work_us_live;
extern uint16_t mcu_load_permille_live;
extern int16_t mcu_temperature_c_live;
extern uint16_t mcu_vref_mv_live;
extern uint8_t mcu_temperature_valid_live;

// ADC EMA Filter control functions
// uint8_t get_filter_enabled(void);
// void set_filter_enabled(uint8_t enabled);
// void get_filter_params(uint8_t *noise_band, uint8_t *alpha_min_denom,
//                        uint8_t *alpha_max_denom);
// void set_filter_params(uint8_t noise_band, uint8_t alpha_min_denom,
//                        uint8_t alpha_max_denom);
/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
