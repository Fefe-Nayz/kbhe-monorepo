/**
 ******************************************************************************
 * @file           : ws2812.c
 * @brief          : Ws2812 library source
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2022 - 2025 Lars Boegild Thomsen <lbthomsen@gmail.com>
 * All rights reserved
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 *
 ******************************************************************************
 */

/**
 * Notice, a timer with a DMA driven PWM output will need to be configured
 * before this library is initialized.
 */

#include <string.h>
#include <stdbool.h>

#include "main.h"

#include "ws2812.h"
#include "color_values.h"

static uint8_t ws2812_led_storage[WS2812_MAX_LEDS * 3];

#if WS2812_USE_SOFTWARE_BACKEND
static inline void ws2812_enable_cycle_counter(void) {
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

static inline void ws2812_wait_until(uint32_t target_cycles) {
    while ((int32_t)(DWT->CYCCNT - target_cycles) < 0) {
    }
}
#endif

/*
 * Update next 24 bits in the dma buffer - assume dma_buffer_pointer is pointing
 * to the buffer that is safe to update.  The dma_buffer_pointer and the call to
 * this function is handled by the dma callbacks.
 */
inline void ws2812_update_buffer(ws2812_handleTypeDef *ws2812, uint32_t *dma_buffer_pointer) {

#ifdef BUFF_GPIO_Port
	HAL_GPIO_WritePin(BUFF_GPIO_Port, BUFF_Pin, GPIO_PIN_SET);
#endif

    // A simple state machine - we're either resetting (two buffers worth of zeros),
    // idle (just winging out zero buffers) or
    // we are transmitting data for the "current" led.

    ++ws2812->dma_cbs;

    if (ws2812->led_state == LED_RES) { // Latch state - 10 or more full buffers of zeros

        // This one is simple - we got a bunch of zeros of the right size - just throw
        // that into the buffer.  Twice will do (two half buffers).
        if (ws2812->zero_halves < 2) {
            memset(dma_buffer_pointer, 0, BUFFER_SIZE * sizeof(uint32_t)); // Fill one half-buffer with zeros
            ws2812->zero_halves++; // We only need to update two half buffers
        }

        ws2812->res_cnt++;

        if (ws2812->res_cnt >= LED_RESET_CYCLES) { // done enough reset cycles - move to next state
            ws2812->led_cnt = 0;	// prepare to send data
            if (ws2812->is_dirty) {
                ws2812->is_dirty = false;
                ws2812->led_state = LED_DAT;
            } else {
                ws2812->led_state = LED_IDL;
            }
        }

    } else if (ws2812->led_state == LED_IDL) { // idle state

        if (ws2812->is_dirty) { // we do nothing here except waiting for a dirty flag
            ws2812->is_dirty = false;
            ws2812->led_state = LED_DAT; // when dirty - start processing data
        }

    } else { // LED_DAT

        ++ws2812->dat_cbs;

        // First let's deal with the current LED
        uint8_t *led = (uint8_t*) &ws2812->led[3 * ws2812->led_cnt];

        for (uint8_t c = 0; c < 3; c++) { // Deal with the 3 color leds in one led package

            // Copy values from the pre-filled color_value buffer
            memcpy(dma_buffer_pointer, color_value[led[c]], 8 * sizeof(uint32_t)); // Lookup the actual buffer data
            dma_buffer_pointer += 8; // next 8 bytes

        }

        // Now move to next LED switching to reset state when all leds have been updated
        ws2812->led_cnt++; // Next led
        if (ws2812->led_cnt >= ws2812->leds) { // reached top
            ws2812->led_cnt = 0; // back to first
            ws2812->zero_halves = 0;
            ws2812->res_cnt = 0;
            ws2812->led_state = LED_RES;
        }

    }

#ifdef BUFF_GPIO_Port
	HAL_GPIO_WritePin(BUFF_GPIO_Port, BUFF_Pin, GPIO_PIN_RESET);
#endif

}

ws2812_resultTypeDef zeroLedValues(ws2812_handleTypeDef *ws2812) {
    ws2812_resultTypeDef res = WS2812_Ok;
    memset(ws2812->led, 0, ws2812->leds * 3); // Zero it all
    ws2812->is_dirty = true; // Mark buffer dirty
    return res;
}

ws2812_resultTypeDef setLedValue(ws2812_handleTypeDef *ws2812, uint16_t led, uint8_t col, uint8_t value) {
    ws2812_resultTypeDef res = WS2812_Ok;
    if (led < ws2812->leds) {
        ws2812->led[3 * led + col] = value;
        ws2812->is_dirty = true; // Mark buffer dirty
    } else {
        res = WS2812_Err;
    }
    return res;
}

// Just throw values into led_value array - the dma interrupt will
// handle updating the dma buffer when needed
ws2812_resultTypeDef setLedValues(ws2812_handleTypeDef *ws2812, uint16_t led, uint8_t r, uint8_t g, uint8_t b) {
    ws2812_resultTypeDef res = WS2812_Ok;
    if (led < ws2812->leds) {
        ws2812->led[3 * led + RL] = r;
        ws2812->led[3 * led + GL] = g;
        ws2812->led[3 * led + BL] = b;
        ws2812->is_dirty = true; // Mark buffer dirty
    } else {
        res = WS2812_Err;
    }
    return res;
}

ws2812_resultTypeDef ws2812_init(ws2812_handleTypeDef *ws2812, TIM_HandleTypeDef *timer, uint32_t channel, uint16_t leds) {

    ws2812_resultTypeDef res = WS2812_Ok;

    if (leds > WS2812_MAX_LEDS) {
        return WS2812_Err;
    }

    // Store timer handle for later
    ws2812->timer = timer;

    // Store channel
    ws2812->channel = channel;

    ws2812->leds = leds;

    ws2812->led_state = LED_RES;
    ws2812->led_cnt = 0;
    ws2812->res_cnt = 0;
    ws2812->is_dirty = 0;
    ws2812->zero_halves = 2;
    ws2812->dma_cbs = 0;
    ws2812->dat_cbs = 0;

    ws2812->led = ws2812_led_storage;
    memset(ws2812->led, 0, leds * 3);

#if WS2812_USE_SOFTWARE_BACKEND
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    __HAL_RCC_GPIOA_CLK_ENABLE();
    GPIO_InitStruct.Pin = LED_DATA_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(LED_DATA_GPIO_Port, &GPIO_InitStruct);
    HAL_GPIO_WritePin(LED_DATA_GPIO_Port, LED_DATA_Pin, GPIO_PIN_RESET);

    ws2812_enable_cycle_counter();
#else
    if (ws2812->dma_buffer == NULL) {
        return WS2812_Err;
    }

    memset(ws2812->dma_buffer, 0, BUFFER_SIZE * 2 * sizeof(uint32_t));

    if (HAL_TIM_PWM_Start_DMA(
            ws2812->timer,
            ws2812->channel,
            (uint32_t *)ws2812->dma_buffer,
            BUFFER_SIZE * 2) != HAL_OK) {
        return WS2812_Err;
    }
#endif

    return res;

}

void ws2812_show(ws2812_handleTypeDef *ws2812) {
    if (ws2812 == NULL || ws2812->led == NULL) {
        return;
    }

#if WS2812_USE_SOFTWARE_BACKEND
    ws2812_enable_cycle_counter();

    const uint32_t bit_total_cycles =
        (uint32_t)((((uint64_t)SystemCoreClock) + 400000ULL) / 800000ULL);
    const uint32_t bit0_high_cycles =
        (uint32_t)((((uint64_t)SystemCoreClock) * 350ULL) / 1000000000ULL);
    const uint32_t bit1_high_cycles =
        (uint32_t)((((uint64_t)SystemCoreClock) * 700ULL) / 1000000000ULL);
    const uint32_t reset_cycles =
        (uint32_t)((((uint64_t)SystemCoreClock) * 80ULL) / 1000000ULL);

    const uint32_t pin_set_mask = LED_DATA_Pin;
    const uint32_t pin_reset_mask = ((uint32_t)LED_DATA_Pin) << 16;
    GPIO_TypeDef *port = LED_DATA_GPIO_Port;

    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    port->BSRR = pin_reset_mask;

    for (uint16_t led_idx = 0; led_idx < ws2812->leds; led_idx++) {
        uint8_t *led = &ws2812->led[led_idx * 3];
        for (uint8_t color_idx = 0; color_idx < 3; color_idx++) {
            uint8_t value = led[color_idx];
            for (int bit = 7; bit >= 0; bit--) {
                uint32_t start_cycles = DWT->CYCCNT;
                uint32_t high_cycles =
                    (value & (1U << bit)) ? bit1_high_cycles : bit0_high_cycles;

                port->BSRR = pin_set_mask;
                ws2812_wait_until(start_cycles + high_cycles);
                port->BSRR = pin_reset_mask;
                ws2812_wait_until(start_cycles + bit_total_cycles);
            }
        }
    }

    uint32_t reset_start_cycles = DWT->CYCCNT;
    port->BSRR = pin_reset_mask;
    ws2812_wait_until(reset_start_cycles + reset_cycles);

    if (primask == 0U) {
        __enable_irq();
    }
#else
    ws2812->is_dirty = true;
#endif
}

/* 
 * vim: ts=4 nowrap
 */
