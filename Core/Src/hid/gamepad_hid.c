#include "hid/gamepad_hid.h"

#include "settings.h"
#include "trigger/trigger.h"
#include "usb_descriptors.h"
#include <string.h>

#define GAMEPAD_AXIS_RANGE 255u
#define GAMEPAD_BUTTON_BIT_COUNT 32u
#define GAMEPAD_CUSTOM_DIGITAL_VALUE 255u

enum {
  KBHE_GAMEPAD_BUTTON_NONE = GAMEPAD_BUTTON_NONE,
  KBHE_GAMEPAD_BUTTON_A = GAMEPAD_BUTTON_A,
  KBHE_GAMEPAD_BUTTON_B = GAMEPAD_BUTTON_B,
  KBHE_GAMEPAD_BUTTON_X = GAMEPAD_BUTTON_X,
  KBHE_GAMEPAD_BUTTON_Y = GAMEPAD_BUTTON_Y,
  KBHE_GAMEPAD_BUTTON_L1 = GAMEPAD_BUTTON_L1,
  KBHE_GAMEPAD_BUTTON_R1 = GAMEPAD_BUTTON_R1,
  KBHE_GAMEPAD_BUTTON_L2 = GAMEPAD_BUTTON_L2,
  KBHE_GAMEPAD_BUTTON_R2 = GAMEPAD_BUTTON_R2,
  KBHE_GAMEPAD_BUTTON_SELECT = GAMEPAD_BUTTON_SELECT,
  KBHE_GAMEPAD_BUTTON_START = GAMEPAD_BUTTON_START,
  KBHE_GAMEPAD_BUTTON_L3 = GAMEPAD_BUTTON_L3,
  KBHE_GAMEPAD_BUTTON_R3 = GAMEPAD_BUTTON_R3,
  KBHE_GAMEPAD_BUTTON_DPAD_UP = GAMEPAD_BUTTON_DPAD_UP,
  KBHE_GAMEPAD_BUTTON_DPAD_DOWN = GAMEPAD_BUTTON_DPAD_DOWN,
  KBHE_GAMEPAD_BUTTON_DPAD_LEFT = GAMEPAD_BUTTON_DPAD_LEFT,
  KBHE_GAMEPAD_BUTTON_DPAD_RIGHT = GAMEPAD_BUTTON_DPAD_RIGHT,
  KBHE_GAMEPAD_BUTTON_HOME = GAMEPAD_BUTTON_HOME,
};

#include "tusb.h"

static gamepad_report_t gamepad_report = {0};
static settings_gamepad_t cached_gamepad = SETTINGS_DEFAULT_GAMEPAD;
static settings_gamepad_mapping_t cached_mappings[NUM_KEYS];
static volatile bool gamepad_report_changed = false;
static bool gamepad_enabled = true;
static uint8_t custom_button_counts[KBHE_GAMEPAD_BUTTON_HOME + 1u] = {0};
static uint8_t custom_axis_counts[GAMEPAD_AXIS_TRIGGER_R + 1u][2] = {{0}};

typedef struct __attribute__((packed)) {
  uint16_t lx;
  uint16_t ly;
  uint16_t rx;
  uint16_t ry;
  uint8_t lt;
  uint8_t rt;
  uint8_t tail[4];
} gamepad_hid_usb_report_t;

static gamepad_hid_usb_report_t prev_gamepad_hid_report = {0};

static inline uint8_t gamepad_max_u8(uint8_t a, uint8_t b) {
  return (a > b) ? a : b;
}

static inline bool gamepad_is_valid_button_id(uint8_t button) {
  return button > KBHE_GAMEPAD_BUTTON_NONE && button <= KBHE_GAMEPAD_BUTTON_HOME;
}

static inline bool gamepad_is_valid_axis_id(uint8_t axis) {
  return axis > (uint8_t)GAMEPAD_AXIS_NONE &&
         axis <= (uint8_t)GAMEPAD_AXIS_TRIGGER_R;
}

static inline uint8_t gamepad_direction_to_index(uint8_t direction) {
  return direction == (uint8_t)GAMEPAD_DIR_NEGATIVE ? 1u : 0u;
}

static uint32_t gamepad_get_custom_buttons_mask(void) {
  uint32_t buttons = 0u;

  for (uint8_t button = KBHE_GAMEPAD_BUTTON_A;
       button <= KBHE_GAMEPAD_BUTTON_HOME; button++) {
    if (custom_button_counts[button] == 0u) {
      continue;
    }

    buttons |= (uint32_t)1u << (uint8_t)(button - 1u);
  }

  return buttons;
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

static uint16_t gamepad_to_unsigned_axis(int8_t value) {
  int32_t scaled = 32768 + ((int32_t)value * 258);

  if (scaled < 0) {
    scaled = 0;
  } else if (scaled > 65535) {
    scaled = 65535;
  }

  return (uint16_t)scaled;
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

static gamepad_hid_usb_report_t gamepad_neutral_hid_report(void) {
  gamepad_hid_usb_report_t neutral = {0};
  neutral.lx = 32768u;
  neutral.ly = 32768u;
  neutral.rx = 32768u;
  neutral.ry = 32768u;
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

  *x = gamepad_clamp_axis_i16(x_val);
  *y = gamepad_clamp_axis_i16(y_val);
}

static bool gamepad_button_is_pressed(uint32_t buttons, uint8_t button) {
  uint8_t bit_index = 0u;

  if (button <= KBHE_GAMEPAD_BUTTON_NONE) {
    return false;
  }

  bit_index = (uint8_t)button - 1u;
  if (bit_index >= GAMEPAD_BUTTON_BIT_COUNT) {
    return false;
  }

  return (buttons & ((uint32_t)1u << bit_index)) != 0u;
}

static uint16_t gamepad_buttons_to_hid_buttons(uint32_t buttons) {
  uint16_t mapped = 0u;

  if (gamepad_button_is_pressed(buttons, KBHE_GAMEPAD_BUTTON_A)) {
    mapped |= (uint16_t)(1u << 0);
  }
  if (gamepad_button_is_pressed(buttons, KBHE_GAMEPAD_BUTTON_B)) {
    mapped |= (uint16_t)(1u << 1);
  }
  if (gamepad_button_is_pressed(buttons, KBHE_GAMEPAD_BUTTON_X)) {
    mapped |= (uint16_t)(1u << 2);
  }
  if (gamepad_button_is_pressed(buttons, KBHE_GAMEPAD_BUTTON_Y)) {
    mapped |= (uint16_t)(1u << 3);
  }
  if (gamepad_button_is_pressed(buttons, KBHE_GAMEPAD_BUTTON_L1)) {
    mapped |= (uint16_t)(1u << 4);
  }
  if (gamepad_button_is_pressed(buttons, KBHE_GAMEPAD_BUTTON_R1)) {
    mapped |= (uint16_t)(1u << 5);
  }
  if (gamepad_button_is_pressed(buttons, KBHE_GAMEPAD_BUTTON_SELECT)) {
    mapped |= (uint16_t)(1u << 6);
  }
  if (gamepad_button_is_pressed(buttons, KBHE_GAMEPAD_BUTTON_START)) {
    mapped |= (uint16_t)(1u << 7);
  }
  if (gamepad_button_is_pressed(buttons, KBHE_GAMEPAD_BUTTON_L3)) {
    mapped |= (uint16_t)(1u << 8);
  }
  if (gamepad_button_is_pressed(buttons, KBHE_GAMEPAD_BUTTON_R3)) {
    mapped |= (uint16_t)(1u << 9);
  }

  return mapped;
}

static uint8_t gamepad_buttons_to_hid_hat(uint32_t buttons) {
  bool up = gamepad_button_is_pressed(buttons, KBHE_GAMEPAD_BUTTON_DPAD_UP);
  bool down = gamepad_button_is_pressed(buttons, KBHE_GAMEPAD_BUTTON_DPAD_DOWN);
  bool left = gamepad_button_is_pressed(buttons, KBHE_GAMEPAD_BUTTON_DPAD_LEFT);
  bool right =
      gamepad_button_is_pressed(buttons, KBHE_GAMEPAD_BUTTON_DPAD_RIGHT);

  if (up && right) {
    return 2u;
  }
  if (right && down) {
    return 4u;
  }
  if (down && left) {
    return 6u;
  }
  if (left && up) {
    return 8u;
  }
  if (up) {
    return 1u;
  }
  if (right) {
    return 3u;
  }
  if (down) {
    return 5u;
  }
  if (left) {
    return 7u;
  }

  return 0u;
}

static void gamepad_build_hid_report(const gamepad_report_t *source,
                                     gamepad_hid_usb_report_t *target) {
  uint16_t buttons = 0u;
  uint8_t hat = 0u;
  uint32_t tail = 0u;

  if (source == NULL || target == NULL) {
    return;
  }

  *target = gamepad_neutral_hid_report();
  buttons = gamepad_buttons_to_hid_buttons(source->buttons);
  hat = gamepad_buttons_to_hid_hat(source->buttons);

  target->lx = gamepad_to_unsigned_axis(source->lx);
  target->ly = gamepad_to_unsigned_axis(source->ly);
  target->rx = gamepad_to_unsigned_axis(source->rx);
  target->ry = gamepad_to_unsigned_axis(source->ry);
  target->lt = source->lt;
  target->rt = source->rt;

  if (gamepad_button_is_pressed(source->buttons, KBHE_GAMEPAD_BUTTON_L2)) {
    target->lt = 255u;
  }
  if (gamepad_button_is_pressed(source->buttons, KBHE_GAMEPAD_BUTTON_R2)) {
    target->rt = 255u;
  }

  tail = (uint32_t)(buttons & 0x03FFu);
  tail |= ((uint32_t)(hat & 0x0Fu) << 10);
  target->tail[0] = (uint8_t)(tail & 0xFFu);
  target->tail[1] = (uint8_t)((tail >> 8) & 0xFFu);
  target->tail[2] = 0u;
  target->tail[3] = 0u;
}

void gamepad_hid_init(void) {
  memset(cached_mappings, 0, sizeof(cached_mappings));
  cached_gamepad = (settings_gamepad_t)SETTINGS_DEFAULT_GAMEPAD;
  gamepad_report = gamepad_neutral_report();
  prev_gamepad_hid_report = gamepad_neutral_hid_report();
  gamepad_report_changed = false;
  gamepad_enabled = true;
  gamepad_hid_custom_clear();
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

  next_report.buttons |= gamepad_get_custom_buttons_mask();

  for (uint8_t axis = (uint8_t)GAMEPAD_AXIS_LEFT_X;
       axis <= (uint8_t)GAMEPAD_AXIS_TRIGGER_R; axis++) {
    if (custom_axis_counts[axis][0] > 0u) {
      positive[axis] =
          gamepad_max_u8(positive[axis], GAMEPAD_CUSTOM_DIGITAL_VALUE);
    }
    if (custom_axis_counts[axis][1] > 0u) {
      negative[axis] =
          gamepad_max_u8(negative[axis], GAMEPAD_CUSTOM_DIGITAL_VALUE);
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

  gamepad_apply_stick_shape(&left_x, &left_y, cached_gamepad.square_mode != 0u);
  gamepad_apply_stick_shape(&right_x, &right_y,
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
  gamepad_hid_usb_report_t hid_report = gamepad_neutral_hid_report();

  if (settings_get_gamepad_api_mode() != GAMEPAD_API_HID) {
    return false;
  }

  if (!gamepad_report_changed) {
    return false;
  }

  if (!tud_mounted() || !tud_hid_n_ready(HID_ITF_GAMEPAD)) {
    return false;
  }

  gamepad_build_hid_report(&gamepad_report, &hid_report);

  if (memcmp(&hid_report, &prev_gamepad_hid_report, sizeof(hid_report)) == 0) {
    gamepad_report_changed = false;
    return false;
  }

  if (tud_hid_n_report(HID_ITF_GAMEPAD, 0, &hid_report, sizeof(hid_report))) {
    memcpy(&prev_gamepad_hid_report, &hid_report, sizeof(hid_report));
    gamepad_report_changed = false;
    return true;
  }

  return false;
}

void gamepad_hid_task(void) { gamepad_hid_send_report_if_changed(); }

const gamepad_report_t *gamepad_hid_get_report(void) { return &gamepad_report; }

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

void gamepad_hid_custom_button_press(uint8_t button) {
  if (!gamepad_is_valid_button_id(button)) {
    return;
  }

  if (custom_button_counts[button] < 0xFFu) {
    custom_button_counts[button]++;
  }
}

void gamepad_hid_custom_button_release(uint8_t button) {
  if (!gamepad_is_valid_button_id(button)) {
    return;
  }

  if (custom_button_counts[button] > 0u) {
    custom_button_counts[button]--;
  }
}

void gamepad_hid_custom_axis_press(uint8_t axis, uint8_t direction) {
  uint8_t direction_index = 0u;

  if (!gamepad_is_valid_axis_id(axis)) {
    return;
  }

  direction_index = gamepad_direction_to_index(direction);
  if (custom_axis_counts[axis][direction_index] < 0xFFu) {
    custom_axis_counts[axis][direction_index]++;
  }
}

void gamepad_hid_custom_axis_release(uint8_t axis, uint8_t direction) {
  uint8_t direction_index = 0u;

  if (!gamepad_is_valid_axis_id(axis)) {
    return;
  }

  direction_index = gamepad_direction_to_index(direction);
  if (custom_axis_counts[axis][direction_index] > 0u) {
    custom_axis_counts[axis][direction_index]--;
  }
}

void gamepad_hid_custom_clear(void) {
  memset(custom_button_counts, 0, sizeof(custom_button_counts));
  memset(custom_axis_counts, 0, sizeof(custom_axis_counts));
}
