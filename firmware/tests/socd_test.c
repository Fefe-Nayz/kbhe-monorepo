#include "trigger/socd.h"

#include "analog/analog.h"
#include "board_config.h"
#include "layout/layout.h"
#include "settings.h"
#include "trigger/trigger.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static settings_key_t layer_keys[NUM_KEYS];
static key_state_e physical_state[NUM_KEYS];
static bool output_state[NUM_KEYS];
static int16_t travel_distance[NUM_KEYS];
static uint32_t fake_tick;

uint32_t HAL_GetTick(void) { return fake_tick; }

uint8_t layout_get_active_layer_top(void) { return 0u; }

bool settings_get_key_for_layer(uint8_t key_index, uint8_t layer_index,
                                settings_key_t *key) {
  if (key_index >= NUM_KEYS || layer_index != 0u || key == NULL) {
    return false;
  }
  *key = layer_keys[key_index];
  return true;
}

key_state_e trigger_get_key_state(uint8_t key) {
  return key < NUM_KEYS ? physical_state[key] : RELEASED;
}

void trigger_socd_set_key_output(uint8_t key, bool pressed) {
  if (key < NUM_KEYS && physical_state[key] == PRESSED) {
    output_state[key] = pressed;
  }
}

void trigger_get_chatter_guard(bool *enabled, uint8_t *duration_ms) {
  if (enabled != NULL) {
    *enabled = false;
  }
  if (duration_ms != NULL) {
    *duration_ms = 0u;
  }
}

int16_t analog_read_travel_distance_value(uint8_t key) {
  return key < NUM_KEYS ? travel_distance[key] : 0;
}

static void configure_pair(settings_socd_resolution_t resolution) {
  layer_keys[0].socd_pair = 1u;
  layer_keys[1].socd_pair = 0u;
  settings_key_set_socd_resolution(&layer_keys[0], resolution);
  settings_key_set_socd_resolution(&layer_keys[1], resolution);
}

static void reset_fixture(settings_socd_resolution_t resolution) {
  memset(layer_keys, 0, sizeof(layer_keys));
  memset(physical_state, 0, sizeof(physical_state));
  memset(output_state, 0, sizeof(output_state));
  memset(travel_distance, 0, sizeof(travel_distance));
  for (uint8_t key = 0u; key < NUM_KEYS; key++) {
    layer_keys[key].socd_pair = SETTINGS_SOCD_PAIR_NONE;
  }
  configure_pair(resolution);
  fake_tick = 1u;
  socd_init();
}

static void press_key(uint8_t key) {
  physical_state[key] = PRESSED;
  output_state[key] = true;
  socd_on_press(key);
}

static void release_key(uint8_t key) {
  output_state[key] = false;
  physical_state[key] = RELEASED;
  socd_on_release(key);
}

static void test_last_input_reload_reconciles_already_held_pair(void) {
  reset_fixture(SETTINGS_SOCD_RESOLUTION_LAST_INPUT_WINS);
  press_key(0u);
  press_key(1u);
  assert(!output_state[0] && output_state[1]);

  /* Removing the pair restores the old loser. */
  layer_keys[0].socd_pair = SETTINGS_SOCD_PAIR_NONE;
  layer_keys[1].socd_pair = SETTINGS_SOCD_PAIR_NONE;
  socd_load_settings();
  assert(output_state[0] && output_state[1]);

  /* Re-adding it while both are still held must immediately choose the
   * physically newer input instead of leaving both host usages down. */
  configure_pair(SETTINGS_SOCD_RESOLUTION_LAST_INPUT_WINS);
  socd_load_settings();
  assert(!output_state[0] && output_state[1]);
}

static void test_neutral_reload_suppresses_both_held_outputs(void) {
  reset_fixture(SETTINGS_SOCD_RESOLUTION_LAST_INPUT_WINS);
  press_key(0u);
  press_key(1u);
  configure_pair(SETTINGS_SOCD_RESOLUTION_NEUTRAL);
  socd_load_settings();
  assert(!output_state[0] && !output_state[1]);

  release_key(1u);
  assert(output_state[0]);
}

static void test_most_pressed_reapplies_winner_after_partner_repress(void) {
  reset_fixture(SETTINGS_SOCD_RESOLUTION_MOST_PRESSED_WINS);
  travel_distance[0] = 2000;
  travel_distance[1] = 1000;
  press_key(0u);
  press_key(1u);
  socd_task();
  assert(output_state[0] && !output_state[1]);

  release_key(1u);
  travel_distance[1] = 1950; /* inside winner hysteresis */
  press_key(1u);
  assert(output_state[0] && output_state[1]);
  socd_task();
  assert(output_state[0] && !output_state[1]);
}

static void test_public_edges_reject_out_of_range_key(void) {
  reset_fixture(SETTINGS_SOCD_RESOLUTION_LAST_INPUT_WINS);
  socd_on_press(NUM_KEYS);
  socd_on_release(NUM_KEYS);
}

int main(void) {
  test_last_input_reload_reconciles_already_held_pair();
  test_neutral_reload_suppresses_both_held_outputs();
  test_most_pressed_reapplies_winner_after_partner_repress();
  test_public_edges_reject_out_of_range_key();
  puts("socd_test: ok");
  return 0;
}
