#include "hid/gamepad_hid.h"

#include "settings.h"
#include "trigger/trigger.h"
#include "tusb.h"
#include "usb_descriptors.h"
#include <string.h>

#define GAMEPAD_AXIS_RANGE 255u
#define GAMEPAD_BUTTON_BIT_COUNT 32u

static gamepad_report_t gamepad_report = {0};
static gamepad_report_t prev_gamepad_report = {0};
static settings_gamepad_t cached_gamepad = SETTINGS_DEFAULT_GAMEPAD;
static settings_gamepad_mapping_t cached_mappings[NUM_KEYS];
static volatile bool gamepad_report_changed = false;
static bool gamepad_enabled = true;

static inline uint8_t gamepad_max_u8(uint8_t a, uint8_t b) {
  return (a > b) ? a : b;
}

static inline int16_t gamepad_clamp_axis_i16(int32_t value) {
  if (value > (int32_t)GAMEPAD_AXIS_RANGE) {
    return (int16_t)GAMEPAD_AXIS_RANGE;
  }
  if (value < -(int32_t)GAMEPAD_AXIS_RANGE) {
    return -(int16_t)GAMEPAD_AXIS_RANGE;
  }
  return (int16_t)value;
}

static uint8_t gamepad_effective_radial_deadzone(
    const settings_gamepad_t *gamepad) {
  uint8_t curve_floor = 0u;

  if (gamepad == NULL) {
    return 0u;
  }

  curve_floor = gamepad->curve[0].y;
  return (gamepad->radial_deadzone > curve_floor) ? gamepad->radial_deadzone
                                                  : curve_floor;
}

static uint16_t gamepad_isqrt_u32(uint32_t value) {
  uint32_t op = value;
  uint32_t res = 0u;
  uint32_t one = 1uL << 30;

  while (one > op) {
    one >>= 2;
  }

  while (one != 0u) {
    if (op >= res + one) {
      op -= res + one;
      res = (res >> 1) + one;
    } else {
      res >>= 1;
    }
    one >>= 2;
  }

  return (uint16_t)res;
}

static int8_t gamepad_to_signed_axis(int16_t value) {
  int32_t clamped = gamepad_clamp_axis_i16(value);
  int32_t magnitude = clamped >= 0 ? clamped : -clamped;
  int32_t scaled = (magnitude * 127 + (GAMEPAD_AXIS_RANGE / 2)) /
                   (int32_t)GAMEPAD_AXIS_RANGE;

  if (scaled > 127) {
    scaled = 127;
  }

  return clamped >= 0 ? (int8_t)scaled : (int8_t)(-scaled);
}

static gamepad_report_t gamepad_neutral_report(void) {
  gamepad_report_t neutral = {0};
  neutral.buttons = 0u;
  neutral.lx = 0;
  neutral.ly = 0;
  neutral.rx = 0;
  neutral.ry = 0;
  neutral.lt = 0u;
  neutral.rt = 0u;
  return neutral;
}

static void gamepad_store_report(const gamepad_report_t *report) {
  if (report == NULL) {
    return;
  }

  if (memcmp(&gamepad_report, report, sizeof(gamepad_report_t)) != 0) {
    memcpy(&gamepad_report, report, sizeof(gamepad_report_t));
    gamepad_report_changed = true;
  }
}

static void gamepad_compose_axis_pair(uint8_t positive, uint8_t negative,
                                      bool reactive_stick, int16_t *out_value) {
  uint8_t pos = positive;
  uint8_t neg = negative;

  if (out_value == NULL) {
    return;
  }

  if (reactive_stick && pos > 0u && neg > 0u) {
    if (pos >= neg) {
      neg = 0u;
    } else {
      pos = 0u;
    }
  }

  *out_value = (int16_t)pos - (int16_t)neg;
}

static void gamepad_apply_stick_shape(int16_t *x, int16_t *y,
                                      uint8_t radial_deadzone,
                                      bool square_mode) {
  int32_t x_val = 0;
  int32_t y_val = 0;
  uint32_t mag_sq = 0u;
  uint16_t magnitude = 0u;

  if (x == NULL || y == NULL) {
    return;
  }

  x_val = *x;
  y_val = *y;

  if (!square_mode) {
    mag_sq = (uint32_t)(x_val * x_val + y_val * y_val);
    if (mag_sq > (uint32_t)(GAMEPAD_AXIS_RANGE * GAMEPAD_AXIS_RANGE)) {
      magnitude = gamepad_isqrt_u32(mag_sq);
      if (magnitude > 0u) {
        x_val = (x_val * (int32_t)GAMEPAD_AXIS_RANGE) / (int32_t)magnitude;
        y_val = (y_val * (int32_t)GAMEPAD_AXIS_RANGE) / (int32_t)magnitude;
      }
    }
  }

  mag_sq = (uint32_t)(x_val * x_val + y_val * y_val);
  if (mag_sq == 0u) {
    *x = 0;
    *y = 0;
    return;
  }

  magnitude = gamepad_isqrt_u32(mag_sq);
  if (radial_deadzone >= GAMEPAD_AXIS_RANGE) {
    *x = 0;
    *y = 0;
    return;
  }

  if (magnitude <= radial_deadzone) {
    *x = 0;
    *y = 0;
    return;
  }

  if (radial_deadzone > 0u && magnitude > 0u) {
    int32_t scaled_magnitude =
        ((int32_t)(magnitude - radial_deadzone) * (int32_t)GAMEPAD_AXIS_RANGE +
         (int32_t)((GAMEPAD_AXIS_RANGE - radial_deadzone) / 2u)) /
        (int32_t)(GAMEPAD_AXIS_RANGE - radial_deadzone);

    if (scaled_magnitude > (int32_t)GAMEPAD_AXIS_RANGE) {
      scaled_magnitude = (int32_t)GAMEPAD_AXIS_RANGE;
    }

    x_val = (x_val * scaled_magnitude) / (int32_t)magnitude;
    y_val = (y_val * scaled_magnitude) / (int32_t)magnitude;
  }

  *x = gamepad_clamp_axis_i16(x_val);
  *y = gamepad_clamp_axis_i16(y_val);
}

void gamepad_hid_init(void) {
  memset(cached_mappings, 0, sizeof(cached_mappings));
  cached_gamepad = (settings_gamepad_t)SETTINGS_DEFAULT_GAMEPAD;
  gamepad_report = gamepad_neutral_report();
  prev_gamepad_report = gamepad_neutral_report();
  gamepad_report_changed = false;
  gamepad_enabled = true;
}

bool gamepad_hid_is_ready(void) { return tud_hid_n_ready(HID_ITF_GAMEPAD); }

void gamepad_hid_reload_settings(void) {
  const settings_t *settings = settings_get();

  if (settings == NULL) {
    cached_gamepad = (settings_gamepad_t)SETTINGS_DEFAULT_GAMEPAD;
    memset(cached_mappings, 0, sizeof(cached_mappings));
    return;
  }

  memcpy(&cached_gamepad, &settings->gamepad, sizeof(cached_gamepad));
  for (uint8_t key = 0; key < NUM_KEYS; key++) {
    cached_mappings[key] = settings->keys[key].gamepad_map;
  }
}

void gamepad_hid_refresh_state(void) {
  gamepad_report_t next_report = gamepad_neutral_report();
  uint8_t positive[GAMEPAD_AXIS_TRIGGER_R + 1u] = {0};
  uint8_t negative[GAMEPAD_AXIS_TRIGGER_R + 1u] = {0};
  uint8_t radial_deadzone = gamepad_effective_radial_deadzone(&cached_gamepad);
  int16_t left_x = 0;
  int16_t left_y = 0;
  int16_t right_x = 0;
  int16_t right_y = 0;

  if (!gamepad_enabled) {
    gamepad_store_report(&next_report);
    return;
  }

  for (uint8_t key = 0; key < NUM_KEYS; key++) {
    const settings_gamepad_mapping_t *mapping = &cached_mappings[key];
    uint16_t distance_01mm = 0u;
    uint8_t analog_value = 0u;

    if (mapping->button > GAMEPAD_BUTTON_NONE &&
        mapping->button <= GAMEPAD_BUTTON_HOME &&
        trigger_get_key_state(key) == PRESSED) {
      uint8_t bit_index = (uint8_t)(mapping->button - 1u);
      if (bit_index < GAMEPAD_BUTTON_BIT_COUNT) {
        next_report.buttons |= (uint32_t)1u << bit_index;
      }
    }

    if (mapping->axis <= GAMEPAD_AXIS_NONE ||
        mapping->axis > GAMEPAD_AXIS_TRIGGER_R) {
      continue;
    }

    distance_01mm = trigger_get_distance_01mm(key);
    analog_value = settings_gamepad_apply_curve(distance_01mm);

    if (mapping->direction == (uint8_t)GAMEPAD_DIR_NEGATIVE) {
      negative[mapping->axis] =
          gamepad_max_u8(negative[mapping->axis], analog_value);
    } else {
      positive[mapping->axis] =
          gamepad_max_u8(positive[mapping->axis], analog_value);
    }
  }

  gamepad_compose_axis_pair(positive[GAMEPAD_AXIS_LEFT_X],
                            negative[GAMEPAD_AXIS_LEFT_X],
                            cached_gamepad.reactive_stick != 0u, &left_x);
  gamepad_compose_axis_pair(positive[GAMEPAD_AXIS_LEFT_Y],
                            negative[GAMEPAD_AXIS_LEFT_Y],
                            cached_gamepad.reactive_stick != 0u, &left_y);
  gamepad_compose_axis_pair(positive[GAMEPAD_AXIS_RIGHT_X],
                            negative[GAMEPAD_AXIS_RIGHT_X],
                            cached_gamepad.reactive_stick != 0u, &right_x);
  gamepad_compose_axis_pair(positive[GAMEPAD_AXIS_RIGHT_Y],
                            negative[GAMEPAD_AXIS_RIGHT_Y],
                            cached_gamepad.reactive_stick != 0u, &right_y);

  gamepad_apply_stick_shape(&left_x, &left_y, radial_deadzone,
                            cached_gamepad.square_mode != 0u);
  gamepad_apply_stick_shape(&right_x, &right_y, radial_deadzone,
                            cached_gamepad.square_mode != 0u);

  next_report.lx = gamepad_to_signed_axis(left_x);
  next_report.ly = gamepad_to_signed_axis(left_y);
  next_report.rx = gamepad_to_signed_axis(right_x);
  next_report.ry = gamepad_to_signed_axis(right_y);
  next_report.lt = gamepad_max_u8(positive[GAMEPAD_AXIS_TRIGGER_L],
                                  negative[GAMEPAD_AXIS_TRIGGER_L]);
  next_report.rt = gamepad_max_u8(positive[GAMEPAD_AXIS_TRIGGER_R],
                                  negative[GAMEPAD_AXIS_TRIGGER_R]);

  gamepad_store_report(&next_report);
}

bool gamepad_hid_send_report_if_changed(void) {
  if (!gamepad_report_changed) {
    return false;
  }

  if (!tud_mounted() || !tud_hid_n_ready(HID_ITF_GAMEPAD)) {
    return false;
  }

  if (memcmp(&gamepad_report, &prev_gamepad_report, sizeof(gamepad_report)) ==
      0) {
    gamepad_report_changed = false;
    return false;
  }

  if (tud_hid_n_report(HID_ITF_GAMEPAD, 0, &gamepad_report,
                       sizeof(gamepad_report))) {
    memcpy(&prev_gamepad_report, &gamepad_report, sizeof(gamepad_report));
    gamepad_report_changed = false;
    return true;
  }

  return false;
}

void gamepad_hid_task(void) { gamepad_hid_send_report_if_changed(); }

void gamepad_hid_set_enabled(bool enabled) {
  if (gamepad_enabled != enabled) {
    gamepad_report_t neutral = gamepad_neutral_report();
    gamepad_enabled = enabled;
    gamepad_store_report(&neutral);
  } else {
    gamepad_enabled = enabled;
  }
}

bool gamepad_hid_is_enabled(void) { return gamepad_enabled; }
