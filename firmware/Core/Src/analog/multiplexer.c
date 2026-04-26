#include "analog/multiplexer.h"
#include "board_config.h"
#include "main.h"

#define MUX_BSRR_STATE(pin_mask, high) ((high) ? (uint32_t)(pin_mask) : ((uint32_t)(pin_mask) << 16))

static const uint32_t mux_channel_bsrr_masks[NUM_MUX_CHANNELS] = {
    MUX_BSRR_STATE(M0_Pin, 0) | MUX_BSRR_STATE(M1_Pin, 0) | MUX_BSRR_STATE(M2_Pin, 0),
    MUX_BSRR_STATE(M0_Pin, 1) | MUX_BSRR_STATE(M1_Pin, 0) | MUX_BSRR_STATE(M2_Pin, 0),
    MUX_BSRR_STATE(M0_Pin, 0) | MUX_BSRR_STATE(M1_Pin, 1) | MUX_BSRR_STATE(M2_Pin, 0),
    MUX_BSRR_STATE(M0_Pin, 1) | MUX_BSRR_STATE(M1_Pin, 1) | MUX_BSRR_STATE(M2_Pin, 0),
    MUX_BSRR_STATE(M0_Pin, 0) | MUX_BSRR_STATE(M1_Pin, 0) | MUX_BSRR_STATE(M2_Pin, 1),
    MUX_BSRR_STATE(M0_Pin, 1) | MUX_BSRR_STATE(M1_Pin, 0) | MUX_BSRR_STATE(M2_Pin, 1),
    MUX_BSRR_STATE(M0_Pin, 0) | MUX_BSRR_STATE(M1_Pin, 1) | MUX_BSRR_STATE(M2_Pin, 1),
    MUX_BSRR_STATE(M0_Pin, 1) | MUX_BSRR_STATE(M1_Pin, 1) | MUX_BSRR_STATE(M2_Pin, 1),
};

void multiplexer_init(void) {
    multiplexer_select_mux_channel(0);
}

void multiplexer_select_mux_channel(uint8_t channel) {
    if (channel >= NUM_MUX_CHANNELS) {
        channel = 0u;
    }

    M0_GPIO_Port->BSRR = mux_channel_bsrr_masks[channel];
}
