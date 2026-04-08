#include "analog/multiplexer.h"
#include "stm32f723xx.h"
#include <stdbool.h>
#include <stdint.h>

void multiplexer_init(void) {
    multiplexer_select_mux_channel(0);
}

static inline void set_bsrr_mask_pin_state(uint32_t *bsrr_mask, uint16_t pin, uint8_t state) {
    uint32_t state_bit = !!state;
    *bsrr_mask |= (state_bit << pin) | ((state_bit ^ 1) << (pin + 16));
}

static inline void apply_bsrr_mask(GPIO_TypeDef *GPIO_Port, uint32_t bsrr_mask) {
    GPIO_Port->BSRR = bsrr_mask;
}

void multiplexer_select_mux_channel(uint8_t channel) {
    uint32_t bsrr_mask = 0;

    // /*
    //  * 75HE
    //  */

    // S0
    set_bsrr_mask_pin_state(&bsrr_mask, 4, channel & 0x01);
    // S1
    set_bsrr_mask_pin_state(&bsrr_mask, 5, channel & 0x02);
    // S2
    set_bsrr_mask_pin_state(&bsrr_mask, 6, channel & 0x04);

    apply_bsrr_mask(GPIOD, bsrr_mask);

    // /*
    //  * Prototype 6HE
    //  */
    // set_bsrr_mask_pin_state(&bsrr_mask, 1, channel & 0x01);
    // set_bsrr_mask_pin_state(&bsrr_mask, 0, channel & 0x02);

    // apply_bsrr_mask(GPIOD, bsrr_mask);

    // bsrr_mask = 0;

    // set_bsrr_mask_pin_state(&bsrr_mask, 12, channel & 0x04);
    // set_bsrr_mask_pin_state(&bsrr_mask, 11, channel & 0x08);

    // apply_bsrr_mask(GPIOC, bsrr_mask);
}