#include "action_engine.h"
#include "layout/keycodes.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint16_t pressed[32];
static uint8_t pressed_owner[32];
static uint8_t pressed_count;
static uint16_t released[32];
static uint8_t released_owner[32];
static uint8_t released_count;
static bool overlay_active[LED_STATE_OVERLAY_COUNT];
static uint32_t overlay_set_calls;
static uint16_t reentrant_profile_switch_keycode;
static uint8_t reentrant_profile_switch_target;

void layout_press_action(uint16_t keycode) {
  pressed_owner[pressed_count] = 0xFFu;
  pressed[pressed_count++] = keycode;
}
void layout_release_action(uint16_t keycode) {
  released_owner[released_count] = 0xFFu;
  released[released_count++] = keycode;
}
void layout_press_action_owned(uint8_t owner, uint16_t keycode) {
  pressed_owner[pressed_count] = owner;
  pressed[pressed_count++] = keycode;
  if (keycode == reentrant_profile_switch_keycode) {
    reentrant_profile_switch_keycode = 0u;
    assert(action_engine_activate_profile(reentrant_profile_switch_target));
  }
}
void layout_release_action_owned(uint8_t owner, uint16_t keycode) {
  released_owner[released_count] = owner;
  released[released_count++] = keycode;
}

bool led_matrix_configure_state_overlay(
    uint8_t overlay_id, const led_state_overlay_config_t *config) {
  return overlay_id < LED_STATE_OVERLAY_COUNT && config != NULL;
}
bool led_matrix_set_state_overlay_active(uint8_t overlay_id, bool active) {
  if (overlay_id >= LED_STATE_OVERLAY_COUNT) {
    return false;
  }
  overlay_set_calls++;
  overlay_active[overlay_id] = active;
  return true;
}
bool led_matrix_pulse_state_overlay(uint8_t overlay_id, uint16_t duration_ms) {
  (void)duration_ms;
  return led_matrix_set_state_overlay_active(overlay_id, true);
}
void led_matrix_clear_state_overlays(void) {
  memset(overlay_active, 0, sizeof(overlay_active));
}

bool action_store_load_profile(uint8_t profile_index,
                               action_profile_t *profile_out) {
  (void)profile_index;
  (void)profile_out;
  return false;
}
bool action_store_save_profile(uint8_t profile_index,
                               const action_profile_t *profile) {
  (void)profile_index;
  (void)profile;
  return true;
}
bool settings_is_profile_slot_used(uint8_t profile_index) {
  return profile_index == 0u;
}

static void reset_events(void) {
  memset(pressed, 0, sizeof(pressed));
  memset(pressed_owner, 0, sizeof(pressed_owner));
  memset(released, 0, sizeof(released));
  memset(released_owner, 0, sizeof(released_owner));
  pressed_count = 0u;
  released_count = 0u;
}

static action_program_t empty_program(void) {
  action_program_t program;
  memset(&program, 0, sizeof(program));
  program.version = ACTION_PROGRAM_VERSION;
  return program;
}

static void test_tap_spans_reports(void) {
  action_program_t program = empty_program();
  program.step_count = 2u;
  program.steps[0].opcode = ACTION_OP_KEY_TAP;
  program.steps[0].arg16 = 0x04u;
  program.steps[1].opcode = ACTION_OP_END;

  assert(action_engine_set_program(0u, 0u, &program, false));
  reset_events();
  assert(action_engine_trigger_program(0u));
  action_engine_tick(100u);
  assert(pressed_count == 1u && pressed[0] == 0x04u);
  assert(released_count == 0u);
  action_engine_tick(107u);
  assert(released_count == 0u);
  action_engine_tick(108u);
  assert(released_count == 1u && released[0] == 0x04u);
}

static void test_tap_deadline_can_wrap_to_zero(void) {
  action_program_t program = empty_program();
  program.step_count = 2u;
  program.steps[0].opcode = ACTION_OP_KEY_TAP;
  program.steps[0].arg16 = 0x07u;
  program.steps[1].opcode = ACTION_OP_END;

  action_engine_cancel_all();
  assert(action_engine_set_program(0u, 9u, &program, false));
  reset_events();
  assert(action_engine_trigger_program(9u));
  action_engine_tick(UINT32_MAX - 7u); /* deadline wraps exactly to zero */
  assert(pressed_count == 1u && released_count == 0u);
  action_engine_tick(UINT32_MAX);
  assert(released_count == 0u);
  action_engine_tick(0u);
  assert(released_count == 1u && released[0] == 0x07u);
}

static void test_cancel_releases_every_owned_output(void) {
  action_program_t program = empty_program();
  program.flags = ACTION_PROGRAM_FLAG_CANCEL_ON_RELEASE;
  program.step_count = 3u;
  program.steps[0].opcode = ACTION_OP_KEY_DOWN;
  program.steps[0].arg16 = 0xE1u;
  program.steps[1].opcode = ACTION_OP_DELAY_MS;
  program.steps[1].arg16 = 500u;
  program.steps[2].opcode = ACTION_OP_END;

  assert(action_engine_set_program(0u, 1u, &program, false));
  reset_events();
  assert(action_engine_trigger_program(1u));
  action_engine_tick(1000u);
  assert(pressed_count == 1u && released_count == 0u);
  action_engine_release_program_trigger(1u);
  action_engine_tick(1001u);
  assert(released_count == 1u && released[0] == 0xE1u);
}

static void test_multi_toggle_and_state_led(void) {
  action_overlay_binding_t binding;
  action_program_t program = empty_program();
  const volatile uint32_t *revision = NULL;
  uint16_t initial_state_bits_before = 0u;
  uint32_t revision_before = 0u;
  memset(&binding, 0, sizeof(binding));
  binding.config.enabled = 1u;
  binding.config.opacity = 255u;
  binding.follows_state = 1u;
  binding.state_index = 2u;
  binding.active_value = 1u;
  assert(action_engine_set_overlay_binding(0u, 0u, &binding, false));

  program.step_count = 4u;
  program.steps[0].opcode = ACTION_OP_STATE_TOGGLE;
  program.steps[0].arg8 = 2u;
  program.steps[1].opcode = ACTION_OP_STATE_TOGGLE;
  program.steps[1].arg8 = 3u;
  program.steps[2].opcode = ACTION_OP_KEY_TAP;
  program.steps[2].arg16 = 0xF010u;
  program.steps[3].opcode = ACTION_OP_END;
  assert(action_engine_set_program(0u, 2u, &program, false));
  revision = action_engine_profile_revision_source(0u);
  assert(revision != NULL);
  initial_state_bits_before =
      action_engine_profile_view(0u)->initial_state_bits;
  revision_before = *revision;

  assert(action_engine_trigger_program(2u));
  action_engine_tick(2000u);
  assert(action_engine_get_state(2u));
  assert(action_engine_get_state(3u));
  /* Macro mode changes are deliberately session-only. They must neither
   * rewrite the next-boot defaults nor restart an in-flight profile snapshot
   * through its revision source. */
  assert(action_engine_profile_view(0u)->initial_state_bits ==
         initial_state_bits_before);
  assert(*revision == revision_before);
  assert(overlay_active[0]);
  assert(pressed_count > 0u && pressed[pressed_count - 1u] == 0xF010u);
}

static void test_concurrent_programs_have_distinct_output_owners(void) {
  action_program_t first = empty_program();
  action_program_t second = empty_program();

  first.flags = ACTION_PROGRAM_FLAG_CANCEL_ON_RELEASE;
  first.step_count = 3u;
  first.steps[0].opcode = ACTION_OP_KEY_DOWN;
  first.steps[0].arg16 = 0x04u;
  first.steps[1].opcode = ACTION_OP_DELAY_MS;
  first.steps[1].arg16 = 500u;
  first.steps[2].opcode = ACTION_OP_END;
  second = first;

  assert(action_engine_set_program(0u, 3u, &first, false));
  assert(action_engine_set_program(0u, 4u, &second, false));
  action_engine_cancel_all();
  reset_events();
  assert(action_engine_trigger_program(3u));
  assert(action_engine_trigger_program(4u));
  action_engine_tick(3000u);
  assert(pressed_count == 2u);
  assert(pressed[0] == 0x04u && pressed[1] == 0x04u);
  assert(pressed_owner[0] != pressed_owner[1]);

  action_engine_release_program_trigger(3u);
  action_engine_release_program_trigger(4u);
  action_engine_tick(3001u);
  assert(released_count == 2u);
  assert(released_owner[0] != released_owner[1]);
}

static void test_same_program_waits_for_every_trigger_release(void) {
  action_program_t program = empty_program();

  program.flags = ACTION_PROGRAM_FLAG_CANCEL_ON_RELEASE;
  program.step_count = 3u;
  program.steps[0].opcode = ACTION_OP_KEY_DOWN;
  program.steps[0].arg16 = 0x05u;
  program.steps[1].opcode = ACTION_OP_DELAY_MS;
  program.steps[1].arg16 = 500u;
  program.steps[2].opcode = ACTION_OP_END;
  assert(action_engine_set_program(0u, 5u, &program, false));

  action_engine_cancel_all();
  reset_events();
  assert(action_engine_trigger_program(5u));
  assert(action_engine_trigger_program(5u));
  action_engine_tick(4000u);
  assert(pressed_count == 1u && released_count == 0u);

  /* Releases are intentionally attributed in the opposite conceptual order;
   * the source-less compatible API uses a reference count, so neither edge
   * alone may cancel output still owned by the other key. */
  action_engine_release_program_trigger(5u);
  action_engine_tick(4001u);
  assert(released_count == 0u);
  action_engine_release_program_trigger(5u);
  action_engine_tick(4002u);
  assert(released_count == 1u && released[0] == 0x05u);
}

static void test_tap_of_held_key_keeps_persistent_ownership(void) {
  action_program_t program = empty_program();

  program.flags = ACTION_PROGRAM_FLAG_CANCEL_ON_RELEASE;
  program.step_count = 4u;
  program.steps[0].opcode = ACTION_OP_KEY_DOWN;
  program.steps[0].arg16 = 0x06u;
  program.steps[1].opcode = ACTION_OP_KEY_TAP;
  program.steps[1].arg16 = 0x06u;
  program.steps[2].opcode = ACTION_OP_DELAY_MS;
  program.steps[2].arg16 = 500u;
  program.steps[3].opcode = ACTION_OP_END;
  assert(action_engine_set_program(0u, 6u, &program, false));

  action_engine_cancel_all();
  reset_events();
  assert(action_engine_trigger_program(6u));
  action_engine_tick(5000u);
  assert(pressed_count == 2u && released_count == 0u);
  assert(pressed_owner[0] == pressed_owner[1]);

  action_engine_tick(5008u);
  assert(released_count == 1u && released[0] == 0x06u);
  action_engine_release_program_trigger(6u);
  action_engine_tick(5009u);
  assert(released_count == 2u && released[1] == 0x06u);
}

static void test_tap_capacity_matches_runtime_binding_limit(void) {
  action_program_t program = empty_program();

  program.step_count = ACTION_ENGINE_MAX_HELD_OUTPUTS + 2u;
  for (uint8_t i = 0u; i < ACTION_ENGINE_MAX_HELD_OUTPUTS; i++) {
    program.steps[i].opcode = ACTION_OP_KEY_DOWN;
    program.steps[i].arg16 = (uint16_t)(0x20u + i);
  }
  program.steps[ACTION_ENGINE_MAX_HELD_OUTPUTS].opcode = ACTION_OP_KEY_TAP;
  program.steps[ACTION_ENGINE_MAX_HELD_OUTPUTS].arg16 = 0x20u;
  program.steps[ACTION_ENGINE_MAX_HELD_OUTPUTS + 1u].opcode = ACTION_OP_END;
  assert(action_engine_validate_program(&program) == ACTION_VALIDATE_OK);

  program.steps[ACTION_ENGINE_MAX_HELD_OUTPUTS].arg16 = 0x40u;
  assert(action_engine_validate_program(&program) ==
         ACTION_VALIDATE_UNBALANCED_OUTPUT);
}

static void run_reentrant_profile_switch(uint8_t program_index,
                                         uint16_t profile_keycode,
                                         uint8_t target_profile) {
  action_program_t program = empty_program();

  assert(action_engine_activate_profile(0u));
  program.step_count = 2u;
  program.steps[0].opcode = ACTION_OP_KEY_DOWN;
  program.steps[0].arg16 = profile_keycode;
  program.steps[1].opcode = ACTION_OP_END;
  assert(action_engine_set_program(0u, program_index, &program, false));

  action_engine_cancel_all();
  reset_events();
  reentrant_profile_switch_keycode = profile_keycode;
  reentrant_profile_switch_target = target_profile;
  assert(action_engine_trigger_program(program_index));
  action_engine_tick(6000u);
  assert(action_engine_active_profile() == target_profile);
  assert(pressed_count == 1u && pressed[0] == profile_keycode);
  assert(released_count == 1u && released[0] == profile_keycode);
  assert(pressed_owner[0] == released_owner[0]);

  action_engine_release_program_trigger(program_index);
  assert(action_engine_activate_profile(0u));
}

static void test_reentrant_profile_next_and_set_roll_back_output(void) {
  run_reentrant_profile_switch(7u, 0xF021u, 1u); /* Profile Next */
  run_reentrant_profile_switch(8u, 0xF024u, 2u); /* Profile Set 3 */
}

static void test_validation_rejects_unsafe_programs(void) {
  action_program_t program = empty_program();
  program.step_count = 1u;
  program.steps[0].opcode = ACTION_OP_STATE_TOGGLE;
  program.steps[0].arg8 = ACTION_STATE_COUNT;
  assert(action_engine_validate_program(&program) ==
         ACTION_VALIDATE_BAD_ARGUMENT);

  program.steps[0].opcode = ACTION_OP_MAX;
  assert(action_engine_validate_program(&program) ==
         ACTION_VALIDATE_BAD_OPCODE);

  program = empty_program();
  program.step_count = ACTION_ENGINE_MAX_HELD_OUTPUTS * 2u + 2u;
  for (uint8_t i = 0u; i <= ACTION_ENGINE_MAX_HELD_OUTPUTS; i++) {
    program.steps[i * 2u].opcode = ACTION_OP_KEY_DOWN;
    program.steps[i * 2u].arg16 = (uint16_t)(0x10u + i);
    /* A release for an unrelated usage must not reduce the distinct held
     * output ledger used by runtime capacity validation. */
    program.steps[i * 2u + 1u].opcode = ACTION_OP_KEY_UP;
    program.steps[i * 2u + 1u].arg16 = (uint16_t)(0x80u + i);
  }
  assert(action_engine_validate_program(&program) ==
         ACTION_VALIDATE_UNBALANCED_OUTPUT);

  program = empty_program();
  program.step_count = 1u;
  program.flags = 0x80u;
  program.steps[0].opcode = ACTION_OP_END;
  assert(action_engine_validate_program(&program) ==
         ACTION_VALIDATE_BAD_ARGUMENT);

  program.flags = 0u;
  program.steps[0].opcode = ACTION_OP_IF_STATE_SKIP;
  program.steps[0].arg8 = 0x10u; /* reserved condition flag */
  assert(action_engine_validate_program(&program) ==
         ACTION_VALIDATE_BAD_ARGUMENT);

  /* The false branch skips KEY_UP, so the serialized order looks safe while
   * one reachable runtime path would retain nine distinct outputs. */
  program = empty_program();
  program.step_count = 12u;
  program.steps[0].opcode = ACTION_OP_KEY_DOWN;
  program.steps[0].arg16 = 0x10u;
  program.steps[1].opcode = ACTION_OP_IF_STATE_SKIP;
  program.steps[1].arg8 = 0u;
  program.steps[1].arg16 = 1u;
  program.steps[2].opcode = ACTION_OP_KEY_UP;
  program.steps[2].arg16 = 0x10u;
  for (uint8_t i = 0u; i < ACTION_ENGINE_MAX_HELD_OUTPUTS; i++) {
    program.steps[3u + i].opcode = ACTION_OP_KEY_DOWN;
    program.steps[3u + i].arg16 = (uint16_t)(0x20u + i);
  }
  program.steps[11].opcode = ACTION_OP_END;
  assert(action_engine_validate_program(&program) ==
         ACTION_VALIDATE_UNBALANCED_OUTPUT);

  /* Divergent branches remain expressive when a later KEY_UP makes their
   * may-held states converge before the pool reaches capacity. */
  program = empty_program();
  program.step_count = 12u;
  program.steps[0].opcode = ACTION_OP_IF_STATE_SKIP;
  program.steps[0].arg8 = 0u;
  program.steps[0].arg16 = 1u;
  program.steps[1].opcode = ACTION_OP_KEY_DOWN;
  program.steps[1].arg16 = 0x10u;
  program.steps[2].opcode = ACTION_OP_KEY_UP;
  program.steps[2].arg16 = 0x10u;
  for (uint8_t i = 0u; i < ACTION_ENGINE_MAX_HELD_OUTPUTS; i++) {
    program.steps[3u + i].opcode = ACTION_OP_KEY_DOWN;
    program.steps[3u + i].arg16 = (uint16_t)(0x20u + i);
  }
  program.steps[11].opcode = ACTION_OP_END;
  assert(action_engine_validate_program(&program) == ACTION_VALIDATE_OK);

  /* END is a real terminal: bytes after it remain structurally checked but
   * cannot consume runtime output bindings. */
  program = empty_program();
  program.step_count = ACTION_ENGINE_MAX_HELD_OUTPUTS + 2u;
  program.steps[0].opcode = ACTION_OP_END;
  for (uint8_t i = 0u; i <= ACTION_ENGINE_MAX_HELD_OUTPUTS; i++) {
    program.steps[1u + i].opcode = ACTION_OP_KEY_DOWN;
    program.steps[1u + i].arg16 = (uint16_t)(0x40u + i);
  }
  assert(action_engine_validate_program(&program) == ACTION_VALIDATE_OK);
}

static void test_macro_call_graph_rejects_direct_and_mutual_cycles(void) {
  action_program_t program = empty_program();
  action_profile_t profile;

  assert(action_engine_reset_profile(0u));
  assert(action_engine_get_profile(0u, &profile));
  program.step_count = 2u;
  program.steps[0].opcode = ACTION_OP_KEY_TAP;
  program.steps[0].arg16 = CUSTOM_MACRO_1;
  program.steps[1].opcode = ACTION_OP_END;
  profile.programs[0] = program;
  assert(action_engine_validate_profile(&profile) ==
         ACTION_VALIDATE_MACRO_CYCLE);
  assert(!action_engine_set_program(0u, 0u, &program, false));

  program.steps[0].arg16 = CUSTOM_MACRO_2;
  assert(action_engine_set_program(0u, 0u, &program, false));
  program.steps[0].arg16 = CUSTOM_MACRO_1;
  assert(!action_engine_set_program(0u, 1u, &program, false));

  /* Structurally valid bytes after END are unreachable and cannot recurse. */
  program.steps[0].opcode = ACTION_OP_END;
  program.steps[0].arg16 = 0u;
  program.steps[1].opcode = ACTION_OP_KEY_TAP;
  program.steps[1].arg16 = CUSTOM_MACRO_2;
  assert(action_engine_program_macro_dependencies(&program) == 0u);
  assert(action_engine_set_program(0u, 1u, &program, false));
}

static action_program_t macro_call_program(uint8_t target_program) {
  action_program_t program = empty_program();
  program.step_count = 2u;
  program.steps[0].opcode = ACTION_OP_KEY_TAP;
  program.steps[0].arg16 =
      (uint16_t)((uint16_t)CUSTOM_MACRO_1 + target_program);
  program.steps[1].opcode = ACTION_OP_END;
  return program;
}

static void test_macro_call_graph_enforces_runtime_instance_depth(void) {
  action_profile_t profile;

  assert(action_engine_reset_profile(0u));
  assert(action_engine_get_profile(0u, &profile));
  for (uint8_t program = 0u; program < ACTION_ENGINE_MAX_INSTANCES - 1u;
       program++) {
    profile.programs[program] = macro_call_program((uint8_t)(program + 1u));
  }
  /* Four nested calls exactly fill the runtime pool. */
  assert(action_engine_validate_profile(&profile) == ACTION_VALIDATE_OK);

  assert(action_engine_reset_profile(0u));
  for (uint8_t program = 0u; program < ACTION_ENGINE_MAX_INSTANCES - 1u;
       program++) {
    assert(action_engine_set_program(0u, program, &profile.programs[program],
                                     false));
  }

  /* A fifth synchronous nesting level cannot be deferred without changing
   * parent/child lifetime semantics, so both whole-profile and incremental
   * validation reject it. */
  profile.programs[ACTION_ENGINE_MAX_INSTANCES - 1u] =
      macro_call_program(ACTION_ENGINE_MAX_INSTANCES);
  assert(action_engine_validate_profile(&profile) ==
         ACTION_VALIDATE_MACRO_DEPTH);
  assert(!action_engine_set_program(
      0u, ACTION_ENGINE_MAX_INSTANCES - 1u,
      &profile.programs[ACTION_ENGINE_MAX_INSTANCES - 1u], false));

  /* Shared fan-out is acyclic and consumes only the longest branch depth. */
  assert(action_engine_reset_profile(0u));
  assert(action_engine_get_profile(0u, &profile));
  profile.programs[0].step_count = 3u;
  profile.programs[0].steps[0].opcode = ACTION_OP_KEY_TAP;
  profile.programs[0].steps[0].arg16 = CUSTOM_MACRO_2;
  profile.programs[0].steps[1].opcode = ACTION_OP_KEY_TAP;
  profile.programs[0].steps[1].arg16 = CUSTOM_MACRO_3;
  profile.programs[0].steps[2].opcode = ACTION_OP_END;
  profile.programs[1] = macro_call_program(3u);
  profile.programs[2] = macro_call_program(3u);
  assert(action_engine_validate_profile(&profile) == ACTION_VALIDATE_OK);
}

static action_program_t delayed_key_program(uint16_t keycode) {
  action_program_t program = empty_program();
  program.step_count = 3u;
  program.steps[0].opcode = ACTION_OP_KEY_DOWN;
  program.steps[0].arg16 = keycode;
  program.steps[1].opcode = ACTION_OP_DELAY_MS;
  program.steps[1].arg16 = 500u;
  program.steps[2].opcode = ACTION_OP_END;
  return program;
}

static void test_fifth_trigger_waits_and_runs_in_fifo_order(void) {
  uint16_t owner_mask = 0u;

  assert(action_engine_reset_profile(0u));
  for (uint8_t program_index = 0u;
       program_index <= ACTION_ENGINE_MAX_INSTANCES; program_index++) {
    action_program_t program = delayed_key_program(
        (uint16_t)(0x20u + program_index));
    if (program_index == 0u) {
      program.step_count = 1u;
      program.steps[0].opcode = ACTION_OP_END;
    }
    assert(action_engine_set_program(0u, program_index, &program, false));
  }

  action_engine_cancel_all();
  reset_events();
  for (uint8_t program_index = 0u;
       program_index <= ACTION_ENGINE_MAX_INSTANCES; program_index++) {
    assert(action_engine_trigger_program(program_index));
  }
  assert(action_engine_pending_trigger_count() == 1u);

  /* Program 0 frees one owner and the accepted fifth trigger is transferred
   * from the FIFO at the end of the same scan. It executes on the next scan. */
  action_engine_tick(6400u);
  assert(action_engine_pending_trigger_count() == 0u);
  assert(pressed_count == ACTION_ENGINE_MAX_INSTANCES - 1u);
  action_engine_tick(6401u);
  assert(pressed_count == ACTION_ENGINE_MAX_INSTANCES);
  assert(pressed[pressed_count - 1u] ==
         (uint16_t)(0x20u + ACTION_ENGINE_MAX_INSTANCES));

  for (uint8_t event = 0u; event < pressed_count; event++) {
    assert(pressed_owner[event] < ACTION_ENGINE_MAX_INSTANCES);
    owner_mask |= (uint16_t)(1u << pressed_owner[event]);
  }
  assert(owner_mask ==
         (uint16_t)((1u << ACTION_ENGINE_MAX_INSTANCES) - 1u));
  action_engine_cancel_all();
  assert(action_engine_pending_trigger_count() == 0u);
}

static void test_trigger_fifo_reports_real_saturation(void) {
  uint32_t drops_before = 0u;

  assert(action_engine_reset_profile(0u));
  for (uint8_t program_index = 0u; program_index < ACTION_PROGRAM_COUNT;
       program_index++) {
    action_program_t program = empty_program();
    program.step_count = 2u;
    program.steps[0].opcode = ACTION_OP_DELAY_MS;
    program.steps[0].arg16 = 500u;
    program.steps[1].opcode = ACTION_OP_END;
    assert(action_engine_set_program(0u, program_index, &program, false));
  }

  action_engine_cancel_all();
  for (uint8_t program_index = 0u;
       program_index < ACTION_ENGINE_MAX_INSTANCES; program_index++) {
    assert(action_engine_trigger_program(program_index));
  }
  drops_before = action_engine_dropped_trigger_count();
  for (uint8_t queued = 0u;
       queued < ACTION_ENGINE_TRIGGER_QUEUE_CAPACITY; queued++) {
    uint8_t program_index = (uint8_t)(
        ACTION_ENGINE_MAX_INSTANCES +
        (queued % (ACTION_PROGRAM_COUNT - ACTION_ENGINE_MAX_INSTANCES)));
    assert(action_engine_trigger_program(program_index));
  }
  assert(action_engine_pending_trigger_count() ==
         ACTION_ENGINE_TRIGGER_QUEUE_CAPACITY);

  assert(!action_engine_trigger_program(ACTION_ENGINE_MAX_INSTANCES));
  assert(action_engine_dropped_trigger_count() == drops_before + 1u);
  action_engine_cancel_all();
  assert(action_engine_pending_trigger_count() == 0u);
}

static void test_tick_has_one_global_step_budget_and_rotates_fairly(void) {
  action_overlay_binding_t binding = {0};

  assert(action_engine_reset_profile(0u));
  assert(action_engine_activate_profile(0u));
  binding.follows_state = 1u;
  binding.state_index = 0u;
  binding.active_value = 1u;
  assert(action_engine_set_overlay_binding(0u, 0u, &binding, false));

  for (uint8_t program_index = 0u; program_index < ACTION_PROGRAM_COUNT;
       program_index++) {
    action_program_t program = empty_program();
    program.step_count = ACTION_PROGRAM_MAX_STEPS;
    for (uint8_t step = 0u; step < program.step_count; step++) {
      program.steps[step].opcode = ACTION_OP_STATE_TOGGLE;
      program.steps[step].arg8 = 0u;
    }
    assert(action_engine_set_program(0u, program_index, &program, false));
    assert(action_engine_trigger_program(program_index));
  }

  overlay_set_calls = 0u;
  action_engine_tick(6450u);
  assert(overlay_set_calls == ACTION_ENGINE_GLOBAL_STEPS_PER_TICK);
  action_engine_tick(6451u);
  assert(overlay_set_calls == 2u * ACTION_ENGINE_GLOBAL_STEPS_PER_TICK);

  /* Four active instances consume 8 steps per visit; the global budget stays
   * fixed even while the remaining accepted triggers wait in the FIFO. */
  action_engine_tick(6452u);
  action_engine_tick(6453u);
  assert(overlay_set_calls == 4u * ACTION_ENGINE_GLOBAL_STEPS_PER_TICK);
  action_engine_cancel_all();
  for (uint8_t program_index = 0u; program_index < ACTION_PROGRAM_COUNT;
       program_index++) {
    action_engine_release_program_trigger(program_index);
  }
}

static void test_trusted_publication_is_targeted(void) {
  const volatile uint32_t *revision = NULL;
  action_program_t toggle = empty_program();
  action_program_t replacement = empty_program();
  action_overlay_binding_t overlay = {0};
  uint16_t runtime_before = 0u;
  uint16_t initial_before = 0u;
  uint32_t revision_before = 0u;

  assert(action_engine_reset_profile(0u));
  assert(action_engine_activate_profile(0u));
  assert(action_engine_set_state(4u, true));
  toggle.step_count = 2u;
  toggle.steps[0].opcode = ACTION_OP_STATE_TOGGLE;
  toggle.steps[0].arg8 = 4u;
  toggle.steps[1].opcode = ACTION_OP_END;
  assert(action_engine_set_program(0u, 13u, &toggle, false));
  assert(action_engine_trigger_program(13u));
  action_engine_tick(6500u);
  action_engine_release_program_trigger(13u);
  assert(!action_engine_get_state(4u));
  assert((action_engine_profile_view(0u)->initial_state_bits &
          (uint16_t)(1u << 4u)) != 0u);

  replacement.step_count = 1u;
  replacement.steps[0].opcode = ACTION_OP_END;
  revision = action_engine_profile_revision_source(0u);
  assert(revision != NULL);
  runtime_before = action_engine_state_bits();
  initial_before = action_engine_profile_view(0u)->initial_state_bits;
  revision_before = *revision;
  assert(action_engine_publish_validated_program(0u, 14u, &replacement));
  assert(action_engine_state_bits() == runtime_before);
  assert(action_engine_profile_view(0u)->initial_state_bits == initial_before);
  assert(*revision == revision_before + 1u);

  overlay.state_index = 4u;
  overlay.active_value = 1u;
  revision_before = *revision;
  assert(action_engine_publish_validated_overlay_binding(0u, 0u, &overlay));
  assert(action_engine_state_bits() == runtime_before);
  assert(action_engine_profile_view(0u)->initial_state_bits == initial_before);
  assert(*revision == revision_before + 1u);

  replacement.version = 0u;
  revision_before = *revision;
  assert(!action_engine_publish_validated_program(0u, 14u, &replacement));
  assert(*revision == revision_before);
}

static void test_live_profile_revision_tracks_every_publication(void) {
  const volatile uint32_t *revision =
      action_engine_profile_revision_source(0u);
  action_program_t program = empty_program();
  action_overlay_binding_t overlay;
  action_profile_t profile;
  uint32_t before = 0u;

  assert(revision != NULL);
  assert(action_engine_profile_revision_source(ACTION_PROFILE_COUNT) == NULL);
  program.step_count = 1u;
  program.steps[0].opcode = ACTION_OP_END;
  before = *revision;
  assert(action_engine_set_program(0u, 2u, &program, false));
  assert(*revision == before + 1u);

  assert(action_engine_get_overlay_binding(0u, 0u, &overlay));
  before = *revision;
  assert(action_engine_set_overlay_binding(0u, 0u, &overlay, false));
  assert(*revision == before + 1u);

  assert(action_engine_get_profile(0u, &profile));
  profile.initial_state_bits ^= 1u;
  before = *revision;
  assert(action_engine_set_profile(0u, &profile, false));
  assert(*revision == before + 1u);

  before = *revision;
  assert(action_engine_set_state(1u, !action_engine_get_state(1u)));
  assert(*revision == before + 1u);
  before = *revision;
  assert(action_engine_set_state(1u, action_engine_get_state(1u)));
  assert(*revision == before);

  /* A runtime-only opcode can diverge from the stored initial bit. A direct
   * API set equal to the current runtime value still publishes that new boot
   * default and therefore must advance the revision. */
  program.step_count = 2u;
  program.steps[0].opcode = ACTION_OP_STATE_TOGGLE;
  program.steps[0].arg8 = 1u;
  program.steps[1].opcode = ACTION_OP_END;
  assert(action_engine_set_program(0u, 15u, &program, false));
  before = *revision;
  assert(action_engine_trigger_program(15u));
  action_engine_tick(7000u);
  assert(*revision == before);
  before = *revision;
  assert(action_engine_set_state(1u, action_engine_get_state(1u)));
  assert(*revision == before + 1u);
}

static void test_idle_state_includes_running_and_queued_work(void) {
  action_program_t program = empty_program();

  action_engine_cancel_all();
  assert(action_engine_is_idle());

  program.step_count = 2u;
  program.steps[0].opcode = ACTION_OP_DELAY_MS;
  program.steps[0].arg16 = 10u;
  program.steps[1].opcode = ACTION_OP_END;
  assert(action_engine_set_program(0u, 12u, &program, false));
  assert(action_engine_trigger_program(12u));
  assert(!action_engine_is_idle());

  action_engine_tick(8000u);
  assert(!action_engine_is_idle());
  action_engine_tick(8010u);
  assert(action_engine_is_idle());
  action_engine_release_program_trigger(12u);
}

int main(void) {
  action_engine_init();
  assert(action_engine_activate_profile(0u));
  test_tap_spans_reports();
  test_tap_deadline_can_wrap_to_zero();
  test_cancel_releases_every_owned_output();
  test_multi_toggle_and_state_led();
  test_concurrent_programs_have_distinct_output_owners();
  test_same_program_waits_for_every_trigger_release();
  test_tap_of_held_key_keeps_persistent_ownership();
  test_tap_capacity_matches_runtime_binding_limit();
  test_reentrant_profile_next_and_set_roll_back_output();
  test_validation_rejects_unsafe_programs();
  test_macro_call_graph_rejects_direct_and_mutual_cycles();
  test_macro_call_graph_enforces_runtime_instance_depth();
  test_fifth_trigger_waits_and_runs_in_fifo_order();
  test_trigger_fifo_reports_real_saturation();
  test_tick_has_one_global_step_budget_and_rotates_fairly();
  test_trusted_publication_is_targeted();
  test_live_profile_revision_tracks_every_publication();
  test_idle_state_includes_running_and_queued_work();
  puts("action_engine_test: ok");
  return 0;
}
