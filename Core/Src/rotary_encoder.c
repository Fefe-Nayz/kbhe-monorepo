#include "rotary_encoder.h"

#include <stdbool.h>

#include "hid/consumer_hid.h"
#include "led_matrix.h"
#include "main.h"
#include "settings.h"
#include "stm32f7xx_hal_gpio.h"

#define ROTARY_BUTTON_DEBOUNCE_MS 20u
#define ROTARY_QUAD_TIMEOUT_MS 80u
#define ROTARY_BRIGHTNESS_STEP_UNIT 4u
#define ROTARY_EFFECT_SPEED_STEP_UNIT 4u
#define ROTARY_HUE_STEP_UNIT 4u

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

static const uint8_t ROTARY_CYCLABLE_LED_EFFECTS[] = {
    LED_EFFECT_STATIC_MATRIX,
    LED_EFFECT_RAINBOW,
    LED_EFFECT_BREATHING,
    LED_EFFECT_STATIC_RAINBOW,
    LED_EFFECT_SOLID,
    LED_EFFECT_PLASMA,
    LED_EFFECT_FIRE,
    LED_EFFECT_OCEAN,
    LED_EFFECT_MATRIX,
    LED_EFFECT_SPARKLE,
    LED_EFFECT_BREATHING_RAINBOW,
    LED_EFFECT_SPIRAL,
    LED_EFFECT_COLOR_CYCLE,
    LED_EFFECT_REACTIVE,
    LED_EFFECT_DISTANCE_SENSOR,
};

static inline uint8_t clamp_u8_from_i16(int16_t value) {
  if (value < 0) {
    return 0u;
  }
  if (value > 255) {
    return 255u;
  }
  return (uint8_t)value;
}

static uint8_t rotary_step_size(const settings_rotary_encoder_t *rotary) {
  if (rotary == NULL || rotary->step_size == 0u) {
    return 1u;
  }

  return rotary->step_size;
}

static uint8_t rotary_transition_threshold(uint8_t sensitivity) {
  if (sensitivity >= 13u) {
    return 1u;
  }
  if (sensitivity >= 9u) {
    return 2u;
  }
  if (sensitivity >= 5u) {
    return 3u;
  }

  return 4u;
}

static uint8_t rotary_effect_index(uint8_t effect) {
  uint8_t effect_count = (uint8_t)(sizeof(ROTARY_CYCLABLE_LED_EFFECTS) /
                                   sizeof(ROTARY_CYCLABLE_LED_EFFECTS[0]));

  for (uint8_t i = 0; i < effect_count; i++) {
    if (ROTARY_CYCLABLE_LED_EFFECTS[i] == effect) {
      return i;
    }
  }

  return 0u;
}

static uint8_t rotary_effect_level(uint8_t effect) {
  uint8_t effect_count = (uint8_t)(sizeof(ROTARY_CYCLABLE_LED_EFFECTS) /
                                   sizeof(ROTARY_CYCLABLE_LED_EFFECTS[0]));
  if (effect_count <= 1u) {
    return 255u;
  }

  return (uint8_t)(((uint16_t)rotary_effect_index(effect) * 255u) /
                   (uint16_t)(effect_count - 1u));
}

static void rotary_rgb_to_hsv(uint8_t r, uint8_t g, uint8_t b, uint8_t *h,
                              uint8_t *s, uint8_t *v) {
  uint8_t rgb_min = r;
  uint8_t rgb_max = r;

  if (g < rgb_min) {
    rgb_min = g;
  }
  if (b < rgb_min) {
    rgb_min = b;
  }
  if (g > rgb_max) {
    rgb_max = g;
  }
  if (b > rgb_max) {
    rgb_max = b;
  }

  *v = rgb_max;
  {
    uint8_t delta = rgb_max - rgb_min;
    if (rgb_max == 0u || delta == 0u) {
      *h = 0u;
      *s = 0u;
      return;
    }

    *s = (uint8_t)(((uint16_t)delta * 255u) / rgb_max);

    {
      int16_t hue;
      if (rgb_max == r) {
        hue = (int16_t)(((int16_t)(g - b) * 43) / delta);
      } else if (rgb_max == g) {
        hue = (int16_t)(85 + (((int16_t)(b - r) * 43) / delta));
      } else {
        hue = (int16_t)(171 + (((int16_t)(r - g) * 43) / delta));
      }

      if (hue < 0) {
        hue += 255;
      } else if (hue > 255) {
        hue -= 255;
      }

      *h = (uint8_t)hue;
    }
  }
}

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

static uint8_t rotary_cycle_led_effect(uint8_t current, int8_t direction,
                                       uint8_t amount) {
  uint8_t current_index = 0u;
  uint8_t effect_count = (uint8_t)(sizeof(ROTARY_CYCLABLE_LED_EFFECTS) /
                                   sizeof(ROTARY_CYCLABLE_LED_EFFECTS[0]));

  for (uint8_t i = 0; i < effect_count; i++) {
    if (ROTARY_CYCLABLE_LED_EFFECTS[i] == current) {
      current_index = i;
      break;
    }
  }

  for (uint8_t i = 0; i < amount; i++) {
    if (direction > 0) {
      current_index = (uint8_t)((current_index + 1u) % effect_count);
    } else {
      current_index = (uint8_t)((current_index + effect_count - 1u) %
                                effect_count);
    }
  }

  return ROTARY_CYCLABLE_LED_EFFECTS[current_index];
}

static void rotary_adjust_led_brightness(int16_t delta) {
  int16_t next =
      (int16_t)settings_get_led_brightness() + delta;
  settings_set_led_brightness(clamp_u8_from_i16(next));
}

static void rotary_adjust_effect_speed(int16_t delta) {
  int16_t next = (int16_t)settings_get_led_effect_speed() + delta;
  if (next < 1) {
    next = 1;
  } else if (next > 255) {
    next = 255;
  }
  settings_set_led_effect_speed((uint8_t)next);
}

static void rotary_adjust_effect_hue(int16_t delta) {
  uint8_t r = 0u;
  uint8_t g = 0u;
  uint8_t b = 0u;
  uint8_t h = 0u;
  uint8_t s = 0u;
  uint8_t v = 0u;
  int16_t next_hue;

  settings_get_led_effect_color(&r, &g, &b);
  rotary_rgb_to_hsv(r, g, b, &h, &s, &v);
  if (s == 0u) {
    s = 255u;
  }
  next_hue = (int16_t)h + delta;
  while (next_hue < 0) {
    next_hue += 256;
  }
  while (next_hue > 255) {
    next_hue -= 256;
  }

  led_matrix_hsv_to_rgb((uint8_t)next_hue, s, (v == 0u) ? 255u : v, &r, &g, &b);
  settings_set_led_effect_color(r, g, b);
}

static uint8_t rotary_rgb_customizer_overlay_level(
    const settings_rotary_encoder_t *rotary) {
  uint8_t r = 0u;
  uint8_t g = 0u;
  uint8_t b = 0u;
  uint8_t h = 0u;
  uint8_t s = 0u;
  uint8_t v = 0u;

  if (rotary == NULL) {
    return 0u;
  }

  switch ((rotary_rgb_behavior_t)rotary->rgb_behavior) {
  case ROTARY_RGB_BEHAVIOR_HUE:
    settings_get_led_effect_color(&r, &g, &b);
    rotary_rgb_to_hsv(r, g, b, &h, &s, &v);
    return h;
  case ROTARY_RGB_BEHAVIOR_BRIGHTNESS:
    return settings_get_led_brightness();
  case ROTARY_RGB_BEHAVIOR_EFFECT_SPEED:
    return settings_get_led_effect_speed();
  case ROTARY_RGB_BEHAVIOR_EFFECT_CYCLE:
    return rotary_effect_level(settings_get_led_effect_mode());
  default:
    return 0u;
  }
}

static void rotary_show_action_overlay(const settings_rotary_encoder_t *rotary) {
  if (rotary == NULL) {
    return;
  }

  switch ((rotary_action_t)rotary->rotation_action) {
  case ROTARY_ACTION_VOLUME:
    led_matrix_show_host_volume_overlay();
    break;
  case ROTARY_ACTION_LED_BRIGHTNESS:
    led_matrix_set_progress_overlay(settings_get_led_brightness());
    break;
  case ROTARY_ACTION_LED_EFFECT_SPEED:
    led_matrix_set_progress_overlay(settings_get_led_effect_speed());
    break;
  case ROTARY_ACTION_LED_EFFECT_CYCLE:
    led_matrix_set_progress_overlay(
        rotary_effect_level(settings_get_led_effect_mode()));
    break;
  case ROTARY_ACTION_RGB_CUSTOMIZER:
    led_matrix_set_progress_overlay(rotary_rgb_customizer_overlay_level(rotary));
    break;
  default:
    break;
  }
}

static void rotary_apply_rgb_customizer(
    const settings_rotary_encoder_t *rotary, int8_t direction) {
  uint8_t amount = rotary_step_size(rotary);

  if (rotary->rgb_behavior != ROTARY_RGB_BEHAVIOR_EFFECT_CYCLE &&
      settings_get_led_effect_mode() != rotary->rgb_effect_mode) {
    settings_set_led_effect_mode(rotary->rgb_effect_mode);
  }

  switch ((rotary_rgb_behavior_t)rotary->rgb_behavior) {
  case ROTARY_RGB_BEHAVIOR_HUE:
    rotary_adjust_effect_hue((int16_t)direction *
                             (int16_t)(amount * ROTARY_HUE_STEP_UNIT));
    break;
  case ROTARY_RGB_BEHAVIOR_BRIGHTNESS:
    rotary_adjust_led_brightness((int16_t)direction *
                                 (int16_t)(amount * ROTARY_BRIGHTNESS_STEP_UNIT));
    break;
  case ROTARY_RGB_BEHAVIOR_EFFECT_SPEED:
    rotary_adjust_effect_speed((int16_t)direction *
                               (int16_t)(amount * ROTARY_EFFECT_SPEED_STEP_UNIT));
    break;
  case ROTARY_RGB_BEHAVIOR_EFFECT_CYCLE: {
    uint8_t next_effect =
        rotary_cycle_led_effect(settings_get_led_effect_mode(), direction,
                                amount);
    settings_set_led_effect_mode(next_effect);
    break;
  }
  default:
    break;
  }
}

static void rotary_handle_button_action(void) {
  settings_rotary_encoder_t rotary = {0};
  settings_get_rotary_encoder(&rotary);

  switch ((rotary_button_action_t)rotary.button_action) {
  case ROTARY_BUTTON_ACTION_PLAY_PAUSE:
    (void)consumer_hid_play_pause();
    break;
  case ROTARY_BUTTON_ACTION_MUTE:
    (void)consumer_hid_mute();
    break;
  case ROTARY_BUTTON_ACTION_TOGGLE_LED:
    (void)settings_set_led_enabled(!settings_is_led_enabled());
    break;
  case ROTARY_BUTTON_ACTION_CYCLE_LED_EFFECT: {
    uint8_t next_effect =
        rotary_cycle_led_effect(settings_get_led_effect_mode(), +1, 1u);
    (void)settings_set_led_effect_mode(next_effect);
    break;
  }
  case ROTARY_BUTTON_ACTION_CYCLE_ROTARY_ACTION:
    rotary.rotation_action =
        (uint8_t)((rotary.rotation_action + 1u) % ROTARY_ACTION_MAX);
    (void)settings_set_rotary_encoder(&rotary);
    break;
  default:
    break;
  }
}

static void emit_rotation_step(const settings_rotary_encoder_t *rotary_cfg,
                               int8_t direction) {
  settings_rotary_encoder_t rotary = {0};
  if (rotary_cfg == NULL) {
    settings_get_rotary_encoder(&rotary);
  } else {
    rotary = *rotary_cfg;
  }

  if (rotary.invert_direction) {
    direction = (int8_t)-direction;
  }

  switch ((rotary_action_t)rotary.rotation_action) {
  case ROTARY_ACTION_VOLUME:
    for (uint8_t i = 0; i < rotary_step_size(&rotary); i++) {
      if (direction > 0) {
        (void)consumer_hid_volume_up();
      } else {
        (void)consumer_hid_volume_down();
      }
    }
    break;
  case ROTARY_ACTION_LED_BRIGHTNESS:
    rotary_adjust_led_brightness((int16_t)direction *
                                 (int16_t)(rotary_step_size(&rotary) *
                                           ROTARY_BRIGHTNESS_STEP_UNIT));
    break;
  case ROTARY_ACTION_LED_EFFECT_SPEED:
    rotary_adjust_effect_speed((int16_t)direction *
                               (int16_t)(rotary_step_size(&rotary) *
                                         ROTARY_EFFECT_SPEED_STEP_UNIT));
    break;
  case ROTARY_ACTION_LED_EFFECT_CYCLE: {
    uint8_t next_effect =
        rotary_cycle_led_effect(settings_get_led_effect_mode(), direction,
                                rotary_step_size(&rotary));
    (void)settings_set_led_effect_mode(next_effect);
    break;
  }
  case ROTARY_ACTION_RGB_CUSTOMIZER:
    rotary_apply_rgb_customizer(&rotary, direction);
    break;
  default:
    break;
  }

  rotary_show_action_overlay(&rotary);
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
  settings_rotary_encoder_t rotary = {0};
  settings_get_rotary_encoder(&rotary);
  uint8_t ab_state = read_ab_state();
  if (ab_state != last_ab_state) {
    int8_t delta = QUAD_TABLE[(last_ab_state << 2) | ab_state];
    uint8_t threshold = rotary_transition_threshold(rotary.sensitivity);

    if ((uint32_t)(now_ms - last_quad_transition_ms) > ROTARY_QUAD_TIMEOUT_MS) {
      quadrature_accum = 0;
    }

    last_ab_state = ab_state;
    last_quad_transition_ms = now_ms;

    if (delta != 0) {
      quadrature_accum += delta;
      if (quadrature_accum >= (int8_t)threshold) {
        quadrature_accum = 0;
        emit_rotation_step(&rotary, +1);
      } else if (quadrature_accum <= -(int8_t)threshold) {
        quadrature_accum = 0;
        emit_rotation_step(&rotary, -1);
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
      rotary_handle_button_action();
    }
  }

  consumer_hid_task();
}
