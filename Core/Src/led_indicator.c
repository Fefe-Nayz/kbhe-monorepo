/*
 * led_indicator.c
 * Status LED indicator on PE0 for caps lock and num lock
 */

#include "led_indicator.h"
#include "stm32f7xx_hal.h"

//--------------------------------------------------------------------+
// PE0 LED Pin Configuration
//--------------------------------------------------------------------+
#define LED_IND_PIN GPIO_PIN_0
#define LED_IND_PORT GPIOE

//--------------------------------------------------------------------+
// Internal State
//--------------------------------------------------------------------+
static bool caps_lock = false;
static bool num_lock = false;

// Blink state for num lock / both lock modes
static bool blink_state = false;
static uint32_t last_blink_tick = 0;
#define BLINK_INTERVAL_SLOW_MS 250 // Blink at 2Hz for num lock only
#define BLINK_INTERVAL_FAST_MS 100 // Blink at 5Hz for both locks

//--------------------------------------------------------------------+
// Internal Functions
//--------------------------------------------------------------------+

static void led_on(void) {
  HAL_GPIO_WritePin(LED_IND_PORT, LED_IND_PIN, GPIO_PIN_SET);
}

static void led_off(void) {
  HAL_GPIO_WritePin(LED_IND_PORT, LED_IND_PIN, GPIO_PIN_RESET);
}

//--------------------------------------------------------------------+
// Public API
//--------------------------------------------------------------------+

void led_indicator_init(void) {
  // Enable GPIOE clock
  __HAL_RCC_GPIOE_CLK_ENABLE();

  // Configure PE0 as push-pull output
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  GPIO_InitStruct.Pin = LED_IND_PIN;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LED_IND_PORT, &GPIO_InitStruct);

  // Start with LED off
  led_off();
}

void led_indicator_set_state(uint8_t led_state) {
  caps_lock = (led_state & HID_LED_CAPS_LOCK) != 0;
  num_lock = (led_state & HID_LED_NUM_LOCK) != 0;

  // Update LED immediately based on new state
  // Both on: solid ON
  // Both off: LED off
  // Caps lock only: fast blink
  // Num lock only: slow blink

  if (caps_lock && num_lock) {
    // Both locks on: solid ON
    led_on();
  } else if (caps_lock && !num_lock) {
    // Caps lock only: fast blink (handled in tick)
    if (blink_state) {
      led_on();
    } else {
      led_off();
    }
  } else if (num_lock && !caps_lock) {
    // Num lock only: slow blink (handled in tick)
    if (blink_state) {
      led_on();
    } else {
      led_off();
    }
  } else {
    // Both off: LED off
    led_off();
  }
}

void led_indicator_tick(uint32_t tick_ms) {
  // Determine blink interval based on state
  uint32_t blink_interval = 0;

  if (caps_lock && !num_lock) {
    // Caps lock only: fast blink
    blink_interval = BLINK_INTERVAL_FAST_MS;
  } else if (num_lock && !caps_lock) {
    // Num lock only: slow blink
    blink_interval = BLINK_INTERVAL_SLOW_MS;
  }
  // Note: Both on = solid, both off = off, no blinking needed

  // Only blink if needed
  if (blink_interval > 0) {
    if (tick_ms - last_blink_tick >= blink_interval) {
      last_blink_tick = tick_ms;
      blink_state = !blink_state;

      if (blink_state) {
        led_on();
      } else {
        led_off();
      }
    }
  }
}

bool led_indicator_is_caps_lock(void) { return caps_lock; }

bool led_indicator_is_num_lock(void) { return num_lock; }
