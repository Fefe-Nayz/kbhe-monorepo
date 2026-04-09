#include "rotary_encoder.h"

#include <stdbool.h>

#include "hid/consumer_hid.h"
#include "led_matrix.h"
#include "main.h"
#include "stm32f7xx_hal_gpio.h"

#define ROTARY_BUTTON_DEBOUNCE_MS 20u
#define ROTARY_QUAD_TIMEOUT_MS 80u

// Quadrature decode table using state bits [A:B].
// Delta is accumulated until a full step is reached.
static const int8_t QUAD_TABLE[16] = {
    0,  -1, 1,  0,
    1,  0,  0, -1,
   -1,  0,  0,  1,
    0,  1, -1,  0,
};

static uint8_t last_ab_state = 0u;
static int8_t quadrature_accum = 0;
static bool button_stable_pressed = false;
static bool button_raw_pressed = false;
static uint32_t button_last_change_ms = 0u;
static uint32_t last_quad_transition_ms = 0u;

static inline uint8_t read_ab_state(void) {
  uint8_t a = (HAL_GPIO_ReadPin(ENCODER_WAVE_1_GPIO_Port, ENCODER_WAVE_1_Pin) ==
               GPIO_PIN_SET)
                  ? 1u
                  : 0u;
  uint8_t b = (HAL_GPIO_ReadPin(ENCODER_WAVE_2_GPIO_Port, ENCODER_WAVE_2_Pin) ==
               GPIO_PIN_SET)
                  ? 1u
                  : 0u;
  return (uint8_t)((a << 1) | b);
}

static inline bool read_button_pressed(void) {
  return HAL_GPIO_ReadPin(ENCODER_SW_GPIO_Port, ENCODER_SW_Pin) == GPIO_PIN_RESET;
}

static void emit_rotation_step(int8_t direction) {
  if (direction > 0) {
    (void)consumer_hid_volume_up();
  } else {
    (void)consumer_hid_volume_down();
  }

  led_matrix_show_host_volume_overlay();
}

void rotary_encoder_init(void) {
  consumer_hid_init();
  last_ab_state = read_ab_state();
  quadrature_accum = 0;
  button_raw_pressed = read_button_pressed();
  button_stable_pressed = button_raw_pressed;
  button_last_change_ms = HAL_GetTick();
  last_quad_transition_ms = button_last_change_ms;
}

void rotary_encoder_task(uint32_t now_ms) {
  uint8_t ab_state = read_ab_state();
  if (ab_state != last_ab_state) {
    int8_t delta = QUAD_TABLE[(last_ab_state << 2) | ab_state];

    if ((uint32_t)(now_ms - last_quad_transition_ms) > ROTARY_QUAD_TIMEOUT_MS) {
      quadrature_accum = 0;
    }

    last_ab_state = ab_state;
    last_quad_transition_ms = now_ms;

    if (delta != 0) {
      quadrature_accum += delta;
      if (quadrature_accum >= 4) {
        quadrature_accum = 0;
        emit_rotation_step(+1);
      } else if (quadrature_accum <= -4) {
        quadrature_accum = 0;
        emit_rotation_step(-1);
      }
    }
  } else if (quadrature_accum != 0 &&
             (uint32_t)(now_ms - last_quad_transition_ms) > ROTARY_QUAD_TIMEOUT_MS) {
    quadrature_accum = 0;
  }

  bool button_pressed = read_button_pressed();
  if (button_pressed != button_raw_pressed) {
    button_raw_pressed = button_pressed;
    button_last_change_ms = now_ms;
  }

  if ((uint32_t)(now_ms - button_last_change_ms) >= ROTARY_BUTTON_DEBOUNCE_MS &&
      button_stable_pressed != button_raw_pressed) {
    button_stable_pressed = button_raw_pressed;
    if (button_stable_pressed) {
      (void)consumer_hid_play_pause();
    }
  }

  consumer_hid_task();
}
