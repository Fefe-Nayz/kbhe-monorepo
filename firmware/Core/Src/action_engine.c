#include "action_engine.h"

#include "layout/keycodes.h"
#include "layout/layout.h"
#include "settings.h"

#include <string.h>

#define ACTION_TAP_HOLD_MS 8u
#define ACTION_STEPS_PER_TICK 8u

#if defined(__GNUC__) && !defined(__clang__)
#define ACTION_ENGINE_COLD_NOINLINE \
  __attribute__((noinline, optimize("Os")))
#else
#define ACTION_ENGINE_COLD_NOINLINE
#endif

typedef struct {
  bool active;
  bool cancel_requested;
  uint8_t program_index;
  uint8_t pc;
  uint8_t held_count;
  uint32_t execution_generation;
  uint16_t held_keycodes[ACTION_ENGINE_MAX_HELD_OUTPUTS];
  uint16_t pending_tap_keycode;
  bool pending_tap_extra_reference;
  bool waiting;
  uint32_t wait_until_ms;
} action_instance_t;

static action_profile_t action_profiles[ACTION_PROFILE_COUNT];
static volatile uint32_t
    action_profile_revision_counters[ACTION_PROFILE_COUNT];
static action_profile_t profile_update_scratch;
static action_instance_t action_instances[ACTION_ENGINE_MAX_INSTANCES];
static uint8_t
    pending_trigger_programs[ACTION_ENGINE_TRIGGER_QUEUE_CAPACITY];
static uint8_t pending_trigger_head = 0u;
static uint8_t pending_trigger_tail = 0u;
static uint8_t pending_trigger_count = 0u;
static uint8_t active_instance_count = 0u;
static uint32_t dropped_trigger_count = 0u;
/* Physical keys and nested programs can acquire the same program slot. Keep
 * trigger ownership independently from execution lifetime: a program may
 * finish before its trigger is released, or fail to allocate an instance. */
static uint8_t program_trigger_references[ACTION_PROGRAM_COUNT];
/* Identifies the definition generation that owns each slot's current trigger
 * references. Layout-held releases carry this token so a stale physical key
 * cannot cancel a new execution after a profile switch or live macro edit. */
static uint32_t program_trigger_generations[ACTION_PROGRAM_COUNT];
static uint32_t next_execution_generation = 1u;
static uint8_t active_profile_index = 0u;
static uint8_t action_tick_cursor = 0u;
static uint16_t runtime_state_bits = 0u;

_Static_assert(ACTION_ENGINE_MAX_INSTANCES > 0u,
               "the action engine requires at least one runtime instance");
_Static_assert(ACTION_ENGINE_MAX_INSTANCES <= ACTION_PROGRAM_COUNT,
               "runtime instances cannot exceed the macro slot count");
_Static_assert(ACTION_ENGINE_TRIGGER_QUEUE_CAPACITY > 0u &&
                   ACTION_ENGINE_TRIGGER_QUEUE_CAPACITY <= UINT8_MAX,
               "the trigger FIFO counters must fit in one byte");
_Static_assert(ACTION_ENGINE_MAX_INSTANCES == LAYOUT_ACTION_OWNER_COUNT,
               "layout must provide one output owner per action instance");

static bool deadline_reached(uint32_t now_ms, uint32_t deadline_ms) {
  return (int32_t)(now_ms - deadline_ms) >= 0;
}

static ACTION_ENGINE_COLD_NOINLINE void
action_program_set_empty(action_program_t *program) {
  memset(program, 0, sizeof(*program));
  program->version = ACTION_PROGRAM_VERSION;
  program->step_count = 1u;
  program->steps[0].opcode = (uint8_t)ACTION_OP_END;
}

static ACTION_ENGINE_COLD_NOINLINE void
action_profile_set_defaults(action_profile_t *profile) {
  memset(profile, 0, sizeof(*profile));
  for (uint8_t slot = 0u; slot < ACTION_PROGRAM_COUNT; slot++) {
    action_program_set_empty(&profile->programs[slot]);
  }
  for (uint8_t overlay = 0u; overlay < LED_STATE_OVERLAY_COUNT; overlay++) {
    profile->overlays[overlay].state_index = overlay;
    profile->overlays[overlay].active_value = 1u;
  }
}

static bool
action_program_runtime_shape_is_sane(const action_program_t *program) {
  return program != NULL && program->version == ACTION_PROGRAM_VERSION &&
         program->step_count > 0u &&
         program->step_count <= ACTION_PROGRAM_MAX_STEPS &&
         (program->flags &
          (uint8_t)~(ACTION_PROGRAM_FLAG_CANCEL_ON_RELEASE |
                     ACTION_PROGRAM_FLAG_RESTART_ON_TRIGGER)) == 0u &&
         program->reserved == 0u;
}

static ACTION_ENGINE_COLD_NOINLINE void
action_engine_sync_state_overlays(void) {
  action_profile_t *profile = &action_profiles[active_profile_index];
  for (uint8_t overlay = 0u; overlay < LED_STATE_OVERLAY_COUNT; overlay++) {
    const action_overlay_binding_t *binding = &profile->overlays[overlay];
    bool value = false;
    if (!binding->follows_state || binding->state_index >= ACTION_STATE_COUNT) {
      continue;
    }
    value = (runtime_state_bits & (uint16_t)(1u << binding->state_index)) != 0u;
    (void)led_matrix_set_state_overlay_active(
        overlay, value == (binding->active_value != 0u));
  }
}

static ACTION_ENGINE_COLD_NOINLINE void
action_engine_configure_state_overlays(void) {
  const action_profile_t *profile = &action_profiles[active_profile_index];
  for (uint8_t overlay = 0u; overlay < LED_STATE_OVERLAY_COUNT; overlay++) {
    (void)led_matrix_configure_state_overlay(
        overlay, &profile->overlays[overlay].config);
  }
  action_engine_sync_state_overlays();
}

bool action_engine_set_runtime_state(uint8_t state_index, bool value) {
  uint16_t state_mask = 0u;
  if (state_index >= ACTION_STATE_COUNT) {
    return false;
  }
  state_mask = (uint16_t)(1u << state_index);
  if (value) {
    runtime_state_bits |= state_mask;
  } else {
    runtime_state_bits &= (uint16_t)~state_mask;
  }
  action_engine_sync_state_overlays();
  return true;
}

static int8_t action_instance_find_for_program(uint8_t program_index) {
  for (uint8_t i = 0u; i < ACTION_ENGINE_MAX_INSTANCES; i++) {
    if (action_instances[i].active &&
        action_instances[i].program_index == program_index) {
      return (int8_t)i;
    }
  }
  return -1;
}

static int8_t action_instance_allocate(void) {
  if (active_instance_count >= ACTION_ENGINE_MAX_INSTANCES) {
    return -1;
  }
  for (uint8_t i = 0u; i < ACTION_ENGINE_MAX_INSTANCES; i++) {
    if (!action_instances[i].active) {
      return (int8_t)i;
    }
  }
  return -1;
}

static ACTION_ENGINE_COLD_NOINLINE void action_trigger_queue_clear(void) {
  pending_trigger_head = 0u;
  pending_trigger_tail = 0u;
  pending_trigger_count = 0u;
}

static ACTION_ENGINE_COLD_NOINLINE void
action_engine_advance_program_trigger_generation(
    uint8_t program_index) {
  program_trigger_references[program_index] = 0u;
  program_trigger_generations[program_index]++;
  if (program_trigger_generations[program_index] == 0u) {
    program_trigger_generations[program_index] = 1u;
  }
}

static ACTION_ENGINE_COLD_NOINLINE void
action_engine_advance_all_trigger_generations(void) {
  for (uint8_t i = 0u; i < ACTION_PROGRAM_COUNT; i++) {
    action_engine_advance_program_trigger_generation(i);
  }
}

static bool action_trigger_queue_push(uint8_t program_index) {
  if (pending_trigger_count >= ACTION_ENGINE_TRIGGER_QUEUE_CAPACITY) {
    if (dropped_trigger_count != UINT32_MAX) {
      dropped_trigger_count++;
    }
    return false;
  }
  pending_trigger_programs[pending_trigger_tail] = program_index;
  pending_trigger_tail++;
  if (pending_trigger_tail >= ACTION_ENGINE_TRIGGER_QUEUE_CAPACITY) {
    pending_trigger_tail = 0u;
  }
  pending_trigger_count++;
  return true;
}

static bool action_trigger_queue_pop(uint8_t *program_index_out) {
  if (program_index_out == NULL || pending_trigger_count == 0u) {
    return false;
  }
  *program_index_out = pending_trigger_programs[pending_trigger_head];
  pending_trigger_head++;
  if (pending_trigger_head >= ACTION_ENGINE_TRIGGER_QUEUE_CAPACITY) {
    pending_trigger_head = 0u;
  }
  pending_trigger_count--;
  return true;
}

static void action_instance_start(uint8_t slot, uint8_t program_index) {
  action_instance_t *instance = &action_instances[slot];
  memset(instance, 0, sizeof(*instance));
  instance->active = true;
  instance->program_index = program_index;
  instance->execution_generation = next_execution_generation++;
  active_instance_count++;
  if (next_execution_generation == 0u) {
    next_execution_generation = 1u;
  }
}

static int8_t action_instance_held_index(const action_instance_t *instance,
                                         uint16_t keycode) {
  for (uint8_t i = 0u; i < instance->held_count; i++) {
    if (instance->held_keycodes[i] == keycode) {
      return (int8_t)i;
    }
  }
  return -1;
}

static bool action_instance_press(action_instance_t *instance,
                                  uint16_t keycode) {
  uint8_t owner = (uint8_t)(instance - action_instances);
  uint32_t generation = instance->execution_generation;

  if (keycode == 0u) {
    return true;
  }
  if (action_instance_held_index(instance, keycode) >= 0) {
    return true;
  }
  if (instance->held_count >= ACTION_ENGINE_MAX_HELD_OUTPUTS) {
    return false;
  }
  layout_press_action_owned(owner, keycode);
  if (!instance->active || instance->execution_generation != generation) {
    /* Internal keycodes may synchronously switch profiles or restart this
     * program. Roll back the ledger acquisition instead of writing held state
     * into a cancelled/reused instance. */
    layout_release_action_owned(owner, keycode);
    return false;
  }
  instance->held_keycodes[instance->held_count++] = keycode;
  return true;
}

static void action_instance_release(action_instance_t *instance,
                                    uint16_t keycode) {
  int8_t held_index = action_instance_held_index(instance, keycode);
  if (held_index < 0) {
    return;
  }
  layout_release_action_owned((uint8_t)(instance - action_instances), keycode);
  instance->held_count--;
  instance->held_keycodes[(uint8_t)held_index] =
      instance->held_keycodes[instance->held_count];
}

static void action_instance_release_pending_tap(action_instance_t *instance) {
  if (instance->pending_tap_keycode == 0u) {
    return;
  }
  if (instance->pending_tap_extra_reference) {
    layout_release_action_owned((uint8_t)(instance - action_instances),
                                instance->pending_tap_keycode);
  } else {
    action_instance_release(instance, instance->pending_tap_keycode);
  }
  instance->pending_tap_keycode = 0u;
  instance->pending_tap_extra_reference = false;
}

static void action_instance_finish(action_instance_t *instance) {
  if (!instance->active) {
    return;
  }
  action_instance_release_pending_tap(instance);
  while (instance->held_count > 0u) {
    action_instance_release(instance,
                            instance->held_keycodes[instance->held_count - 1u]);
  }
  active_instance_count--;
  memset(instance, 0, sizeof(*instance));
}

/* Drain at most one fixed pool's worth of entries per scan. This keeps the
 * overloaded 8 kHz path bounded even if cancelled/stale FIFO entries must be
 * skipped; valid entries retain strict FIFO order across scans. */
static void action_trigger_queue_drain(void) {
  uint8_t attempts = ACTION_ENGINE_MAX_INSTANCES;

  while (attempts-- > 0u && pending_trigger_count > 0u &&
         active_instance_count < ACTION_ENGINE_MAX_INSTANCES) {
    const action_program_t *program = NULL;
    uint8_t program_index = 0u;
    int8_t existing = -1;
    int8_t slot = -1;

    if (!action_trigger_queue_pop(&program_index)) {
      return;
    }
    program = &action_profiles[active_profile_index].programs[program_index];
    if (!action_program_runtime_shape_is_sane(program)) {
      continue;
    }

    existing = action_instance_find_for_program(program_index);
    if (existing >= 0) {
      if ((program->flags & ACTION_PROGRAM_FLAG_RESTART_ON_TRIGGER) == 0u) {
        continue;
      }
      action_instance_finish(&action_instances[(uint8_t)existing]);
      slot = existing;
    } else {
      /* A released momentary macro must not begin later merely because it was
       * waiting for capacity. Non-cancelling macros remain trigger events and
       * are intentionally executed even if their source was already released. */
      if ((program->flags & ACTION_PROGRAM_FLAG_CANCEL_ON_RELEASE) != 0u &&
          program_trigger_references[program_index] == 0u) {
        continue;
      }
      slot = action_instance_allocate();
      if (slot < 0) {
        /* The active-count guard above makes this unreachable without state
         * corruption. Preserve the event instead of silently losing it. */
        pending_trigger_head = pending_trigger_head == 0u
                                   ? ACTION_ENGINE_TRIGGER_QUEUE_CAPACITY - 1u
                                   : (uint8_t)(pending_trigger_head - 1u);
        pending_trigger_programs[pending_trigger_head] = program_index;
        pending_trigger_count++;
        return;
      }
    }
    action_instance_start((uint8_t)slot, program_index);
  }
}

static bool action_program_condition_is_true(const action_step_t *step) {
  uint8_t state_index = step->arg8 & 0x0Fu;
  bool expected = (step->arg8 & 0x80u) != 0u;
  bool actual = action_engine_get_state(state_index);
  return actual == expected;
}

static bool action_instance_execute_step(action_instance_t *instance,
                                         const action_program_t *program,
                                         uint32_t now_ms) {
  const action_step_t *step = NULL;
  uint32_t execution_generation = instance->execution_generation;

  if (instance->pc >= program->step_count) {
    action_instance_finish(instance);
    return false;
  }

  step = &program->steps[instance->pc++];
  switch ((action_opcode_t)step->opcode) {
  case ACTION_OP_NOP:
    return true;
  case ACTION_OP_END:
    action_instance_finish(instance);
    return false;
  case ACTION_OP_KEY_DOWN:
    if (!action_instance_press(instance, step->arg16)) {
      if (instance->active &&
          instance->execution_generation == execution_generation) {
        action_instance_finish(instance);
      }
      return false;
    }
    return true;
  case ACTION_OP_KEY_UP:
    action_instance_release(instance, step->arg16);
    return true;
  case ACTION_OP_KEY_TAP:
    if (step->arg16 == 0u) {
      return true;
    }
    if (action_instance_held_index(instance, step->arg16) >= 0) {
      /* A tap of an already-held usage owns one extra output reference. Its
       * timed release must not remove the persistent KEY_DOWN ownership. */
      layout_press_action_owned((uint8_t)(instance - action_instances),
                                step->arg16);
      if (!instance->active ||
          instance->execution_generation != execution_generation) {
        layout_release_action_owned(
            (uint8_t)(instance - action_instances), step->arg16);
        return false;
      }
      instance->pending_tap_extra_reference = true;
    } else {
      if (!action_instance_press(instance, step->arg16)) {
        if (instance->active &&
            instance->execution_generation == execution_generation) {
          action_instance_finish(instance);
        }
        return false;
      }
      instance->pending_tap_extra_reference = false;
    }
    instance->pending_tap_keycode = step->arg16;
    instance->wait_until_ms = now_ms + ACTION_TAP_HOLD_MS;
    instance->waiting = true;
    return false;
  case ACTION_OP_DELAY_MS:
    instance->wait_until_ms = now_ms + step->arg16;
    instance->waiting = true;
    return false;
  case ACTION_OP_STATE_SET:
    (void)action_engine_set_runtime_state(step->arg8, step->arg16 != 0u);
    return true;
  case ACTION_OP_STATE_TOGGLE:
    (void)action_engine_set_runtime_state(
        step->arg8, !action_engine_get_state(step->arg8));
    return true;
  case ACTION_OP_IF_STATE_SKIP:
    if (!action_program_condition_is_true(step)) {
      uint16_t target = (uint16_t)instance->pc + step->arg16;
      instance->pc = (target > program->step_count)
                         ? program->step_count
                         : (uint8_t)target;
    }
    return true;
  case ACTION_OP_OVERLAY_SET:
    if (step->arg16 == 0u) {
      (void)led_matrix_set_state_overlay_active(step->arg8, false);
    } else if (step->arg16 == 1u) {
      (void)led_matrix_set_state_overlay_active(step->arg8, true);
    } else {
      (void)led_matrix_pulse_state_overlay(step->arg8, step->arg16);
    }
    return true;
  case ACTION_OP_MAX:
  default:
    action_instance_finish(instance);
    return false;
  }
}

ACTION_ENGINE_COLD_NOINLINE void action_engine_init(void) {
  memset(action_instances, 0, sizeof(action_instances));
  action_trigger_queue_clear();
  memset(program_trigger_references, 0,
         sizeof(program_trigger_references));
  for (uint8_t i = 0u; i < ACTION_PROGRAM_COUNT; i++) {
    program_trigger_generations[i] = 1u;
  }
  active_instance_count = 0u;
  dropped_trigger_count = 0u;
  next_execution_generation = 1u;
  action_tick_cursor = 0u;
  for (uint8_t profile = 0u; profile < ACTION_PROFILE_COUNT; profile++) {
    action_profile_set_defaults(&action_profiles[profile]);
    /* The aggregate settings used-mask owns slot lifecycle. Do not load an
     * old document for a deleted slot: a later create must not inherit stale
     * macros/actions from that document. */
    if (settings_is_profile_slot_used(profile)) {
      (void)action_store_load_profile(profile, &action_profiles[profile]);
    }
    action_profile_revision_counters[profile] = 0u;
  }
  active_profile_index = 0u;
  runtime_state_bits = action_profiles[0].initial_state_bits;
  action_engine_configure_state_overlays();
}

void action_engine_tick(uint32_t now_ms) {
  uint8_t global_budget = ACTION_ENGINE_GLOBAL_STEPS_PER_TICK;
  uint8_t next_cursor =
      (uint8_t)((action_tick_cursor + 1u) % ACTION_ENGINE_MAX_INSTANCES);

  for (uint8_t offset = 0u; offset < ACTION_ENGINE_MAX_INSTANCES; offset++) {
    uint8_t i = (uint8_t)((action_tick_cursor + offset) %
                          ACTION_ENGINE_MAX_INSTANCES);
    action_instance_t *instance = &action_instances[i];
    const action_program_t *program = NULL;

    if (!instance->active) {
      continue;
    }
    program = &action_profiles[active_profile_index]
                   .programs[instance->program_index];

    if (instance->cancel_requested) {
      action_instance_finish(instance);
      continue;
    }
    if (instance->waiting &&
        !deadline_reached(now_ms, instance->wait_until_ms)) {
      continue;
    }
    if (instance->pending_tap_keycode != 0u) {
      action_instance_release_pending_tap(instance);
    }
    instance->waiting = false;
    instance->wait_until_ms = 0u;

    for (uint8_t budget = 0u;
         budget < ACTION_STEPS_PER_TICK && global_budget > 0u &&
         instance->active;
         budget++) {
      global_budget--;
      next_cursor =
          (uint8_t)((i + 1u) % ACTION_ENGINE_MAX_INSTANCES);
      if (!action_instance_execute_step(instance, program, now_ms)) {
        break;
      }
    }
  }
  action_tick_cursor = next_cursor;
  action_trigger_queue_drain();
}

ACTION_ENGINE_COLD_NOINLINE void action_engine_cancel_all(void) {
  for (uint8_t i = 0u; i < ACTION_ENGINE_MAX_INSTANCES; i++) {
    if (action_instances[i].active) {
      action_instance_finish(&action_instances[i]);
    }
  }
  action_engine_advance_all_trigger_generations();
  action_trigger_queue_clear();
  action_tick_cursor = 0u;
}

ACTION_ENGINE_COLD_NOINLINE bool
action_engine_activate_profile(uint8_t profile_index) {
  if (profile_index >= ACTION_PROFILE_COUNT) {
    return false;
  }
  /* Finish old-profile execution, then invalidate its tracked trigger tokens.
   * A physical key held across the switch will eventually release using its
   * old token and therefore cannot cancel a new execution of the same slot. */
  for (uint8_t i = 0u; i < ACTION_ENGINE_MAX_INSTANCES; i++) {
    if (action_instances[i].active) {
      action_instance_finish(&action_instances[i]);
    }
  }
  action_engine_advance_all_trigger_generations();
  action_trigger_queue_clear();
  led_matrix_clear_state_overlays();
  active_profile_index = profile_index;
  action_tick_cursor = 0u;
  runtime_state_bits = action_profiles[profile_index].initial_state_bits;
  action_engine_configure_state_overlays();
  return true;
}

uint8_t action_engine_active_profile(void) { return active_profile_index; }

bool action_engine_trigger_program_tracked(uint8_t program_index,
                                           uint32_t *trigger_token_out) {
  const action_program_t *program = NULL;
  int8_t existing = -1;
  int8_t slot = -1;

  if (trigger_token_out != NULL) {
    *trigger_token_out = 0u;
  }
  if (program_index >= ACTION_PROGRAM_COUNT) {
    return false;
  }
  if (program_trigger_references[program_index] == 0xFFu) {
    return false;
  }
  /* Count every balanced trigger edge, including one whose program is invalid
   * or cannot currently allocate. Its later release must not steal ownership
   * from another key that legitimately holds the same macro slot. */
  program_trigger_references[program_index]++;
  if (trigger_token_out != NULL) {
    *trigger_token_out = program_trigger_generations[program_index];
  }
  program = &action_profiles[active_profile_index].programs[program_index];
  /* Programs are fully validated when loaded or published. Keep the physical
   * key/nested-macro hot path constant-time while retaining cheap corruption
   * guards that make every subsequent array access safe. */
  if (!action_program_runtime_shape_is_sane(program)) {
    return false;
  }

  existing = action_instance_find_for_program(program_index);
  if (existing >= 0) {
    if ((program->flags & ACTION_PROGRAM_FLAG_RESTART_ON_TRIGGER) == 0u) {
      return true;
    }
    action_instance_finish(&action_instances[(uint8_t)existing]);
  }

  /* Do not let a new physical edge overtake triggers that were already
   * accepted while the pool was full. */
  if (pending_trigger_count > 0u) {
    return action_trigger_queue_push(program_index);
  }

  slot = action_instance_allocate();
  if (slot < 0) {
    return action_trigger_queue_push(program_index);
  }
  action_instance_start((uint8_t)slot, program_index);
  return true;
}

static ACTION_ENGINE_COLD_NOINLINE void
action_trigger_queue_remove_program(uint8_t program_index) {
  uint8_t retained[ACTION_ENGINE_TRIGGER_QUEUE_CAPACITY] = {0};
  uint8_t retained_count = 0u;
  uint8_t original_count = pending_trigger_count;

  for (uint8_t i = 0u; i < original_count; i++) {
    uint8_t queued_program = 0u;
    if (!action_trigger_queue_pop(&queued_program)) {
      break;
    }
    if (queued_program != program_index) {
      retained[retained_count++] = queued_program;
    }
  }
  action_trigger_queue_clear();
  for (uint8_t i = 0u; i < retained_count; i++) {
    (void)action_trigger_queue_push(retained[i]);
  }
}

bool action_engine_trigger_program(uint8_t program_index) {
  return action_engine_trigger_program_tracked(program_index, NULL);
}

void action_engine_release_program_trigger(uint8_t program_index) {
  int8_t instance_index = -1;
  const action_program_t *program = NULL;
  if (program_index >= ACTION_PROGRAM_COUNT ||
      program_trigger_references[program_index] == 0u) {
    return;
  }
  program_trigger_references[program_index]--;
  if (program_trigger_references[program_index] != 0u) {
    return;
  }

  instance_index = action_instance_find_for_program(program_index);
  if (instance_index < 0) {
    return;
  }
  program = &action_profiles[active_profile_index].programs[program_index];
  if ((program->flags & ACTION_PROGRAM_FLAG_CANCEL_ON_RELEASE) != 0u) {
    action_instances[(uint8_t)instance_index].cancel_requested = true;
  }
}

void action_engine_release_program_trigger_tracked(uint8_t program_index,
                                                   uint32_t trigger_token) {
  if (program_index >= ACTION_PROGRAM_COUNT || trigger_token == 0u ||
      trigger_token != program_trigger_generations[program_index]) {
    return;
  }
  action_engine_release_program_trigger(program_index);
}

uint8_t action_engine_pending_trigger_count(void) {
  return pending_trigger_count;
}

uint8_t action_engine_active_instance_count(void) {
  return active_instance_count;
}

bool action_engine_is_idle(void) {
  return active_instance_count == 0u && pending_trigger_count == 0u;
}

uint32_t action_engine_dropped_trigger_count(void) {
  return dropped_trigger_count;
}

/* Validation, profile publication and persistence helpers are control-plane
 * operations reached only by boot/profile changes or explicit HID commands.
 * Keep the per-scan executor above at -O3, while compiling this cold block for
 * size so configuration features do not consume the realtime image budget. */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC push_options
#pragma GCC optimize("Os")
#endif

action_validation_result_t
action_engine_validate_program(const action_program_t *program) {
  uint16_t output_keycodes[ACTION_PROGRAM_MAX_STEPS] = {0};
  uint32_t must_held_masks[ACTION_PROGRAM_MAX_STEPS] = {0};
  uint32_t may_held_masks[ACTION_PROGRAM_MAX_STEPS] = {0};
  uint8_t max_held_counts[ACTION_PROGRAM_MAX_STEPS] = {0};
  uint64_t reachable_steps = 0u;
  uint8_t output_keycode_count = 0u;

  if (program == NULL || program->version != ACTION_PROGRAM_VERSION) {
    return ACTION_VALIDATE_BAD_VERSION;
  }
  if (program->step_count == 0u ||
      program->step_count > ACTION_PROGRAM_MAX_STEPS) {
    return ACTION_VALIDATE_TOO_MANY_STEPS;
  }
  if ((program->flags &
       (uint8_t)~(ACTION_PROGRAM_FLAG_CANCEL_ON_RELEASE |
                  ACTION_PROGRAM_FLAG_RESTART_ON_TRIGGER)) != 0u ||
      program->reserved != 0u) {
    return ACTION_VALIDATE_BAD_ARGUMENT;
  }

  for (uint8_t i = 0u; i < program->step_count; i++) {
    const action_step_t *step = &program->steps[i];
    if (step->opcode >= (uint8_t)ACTION_OP_MAX) {
      return ACTION_VALIDATE_BAD_OPCODE;
    }
    switch ((action_opcode_t)step->opcode) {
    case ACTION_OP_KEY_DOWN:
    case ACTION_OP_KEY_UP:
    case ACTION_OP_KEY_TAP:
      if (step->arg16 != 0u) {
        bool known_keycode = false;
        for (uint8_t key = 0u; key < output_keycode_count; key++) {
          if (output_keycodes[key] == step->arg16) {
            known_keycode = true;
            break;
          }
        }
        if (!known_keycode) {
          output_keycodes[output_keycode_count++] = step->arg16;
        }
      }
      break;
    case ACTION_OP_STATE_SET:
    case ACTION_OP_STATE_TOGGLE:
      if (step->arg8 >= ACTION_STATE_COUNT) {
        return ACTION_VALIDATE_BAD_ARGUMENT;
      }
      break;
    case ACTION_OP_IF_STATE_SKIP:
      if ((step->arg8 & 0x70u) != 0u ||
          (step->arg8 & 0x0Fu) >= ACTION_STATE_COUNT ||
          (uint16_t)i + 1u + step->arg16 > program->step_count) {
        return ACTION_VALIDATE_BAD_ARGUMENT;
      }
      break;
    case ACTION_OP_OVERLAY_SET:
      if (step->arg8 >= LED_STATE_OVERLAY_COUNT) {
        return ACTION_VALIDATE_BAD_ARGUMENT;
      }
      break;
    case ACTION_OP_NOP:
    case ACTION_OP_END:
    case ACTION_OP_DELAY_MS:
    case ACTION_OP_MAX:
    default:
      break;
    }
  }

  /* Validate the reachable control-flow graph, not the serialized instruction
   * order. IF_STATE_SKIP has two forward successors, while END and the
   * implicit end-of-program release every owned output immediately. At merges,
   * must-held is intersected, may-held is unioned, and max-held keeps the worst
   * predecessor. This bounded abstraction accepts safe divergent branches
   * without enumerating exponentially many concrete held-key sets. */
  reachable_steps = 1u;
  for (uint8_t i = 0u; i < program->step_count; i++) {
    const action_step_t *step = &program->steps[i];
    uint32_t must_held_mask = must_held_masks[i];
    uint32_t may_held_mask = may_held_masks[i];
    uint8_t max_held_count = max_held_counts[i];
    uint32_t output_bit = 0u;
    uint8_t next_steps[2] = {(uint8_t)(i + 1u), 0u};
    uint8_t next_count = 1u;

    if ((reachable_steps & (UINT64_C(1) << i)) == 0u) {
      continue;
    }
    if ((action_opcode_t)step->opcode == ACTION_OP_END) {
      continue;
    }

    if (step->arg16 != 0u &&
        ((action_opcode_t)step->opcode == ACTION_OP_KEY_DOWN ||
         (action_opcode_t)step->opcode == ACTION_OP_KEY_UP ||
         (action_opcode_t)step->opcode == ACTION_OP_KEY_TAP)) {
      for (uint8_t key = 0u; key < output_keycode_count; key++) {
        if (output_keycodes[key] == step->arg16) {
          output_bit = UINT32_C(1) << key;
          break;
        }
      }
    }

    if ((action_opcode_t)step->opcode == ACTION_OP_KEY_DOWN &&
        output_bit != 0u && (must_held_mask & output_bit) == 0u) {
      if (max_held_count >= ACTION_ENGINE_MAX_HELD_OUTPUTS) {
        return ACTION_VALIDATE_UNBALANCED_OUTPUT;
      }
      max_held_count++;
      must_held_mask |= output_bit;
      may_held_mask |= output_bit;
    } else if ((action_opcode_t)step->opcode == ACTION_OP_KEY_UP) {
      if ((must_held_mask & output_bit) != 0u && max_held_count > 0u) {
        max_held_count--;
      }
      must_held_mask &= ~output_bit;
      may_held_mask &= ~output_bit;
    } else if ((action_opcode_t)step->opcode == ACTION_OP_KEY_TAP &&
               output_bit != 0u &&
               (must_held_mask & output_bit) == 0u) {
      /* A new tap temporarily occupies the same per-instance output-binding
       * pool as persistent KEY_DOWN entries.  Tapping an already-held usage
       * only adds a reference and therefore remains valid at capacity. */
      if (max_held_count >= ACTION_ENGINE_MAX_HELD_OUTPUTS) {
        return ACTION_VALIDATE_UNBALANCED_OUTPUT;
      }
    }

    /* KEY_UP of a merely possible usage cannot lower the conservative maximum,
     * but the number of possible usages is still a tighter safe upper bound. */
    {
      uint32_t count_mask = may_held_mask;
      uint8_t may_held_count = 0u;
      while (count_mask != 0u) {
        count_mask &= count_mask - 1u;
        may_held_count++;
      }
      if (max_held_count > may_held_count) {
        max_held_count = may_held_count;
      }
    }

    if ((action_opcode_t)step->opcode == ACTION_OP_IF_STATE_SKIP) {
      next_steps[1] = (uint8_t)((uint16_t)i + 1u + step->arg16);
      next_count = 2u;
    }

    for (uint8_t edge = 0u; edge < next_count; edge++) {
      uint8_t target = next_steps[edge];
      uint64_t target_bit = 0u;

      if (target >= program->step_count ||
          (action_opcode_t)program->steps[target].opcode == ACTION_OP_END) {
        continue;
      }
      target_bit = UINT64_C(1) << target;
      if ((reachable_steps & target_bit) == 0u) {
        must_held_masks[target] = must_held_mask;
        may_held_masks[target] = may_held_mask;
        max_held_counts[target] = max_held_count;
        reachable_steps |= target_bit;
      } else {
        uint32_t joined_may = may_held_masks[target] | may_held_mask;
        uint32_t count_mask = joined_may;
        uint8_t joined_may_count = 0u;

        must_held_masks[target] &= must_held_mask;
        may_held_masks[target] = joined_may;
        if (max_held_count > max_held_counts[target]) {
          max_held_counts[target] = max_held_count;
        }
        while (count_mask != 0u) {
          count_mask &= count_mask - 1u;
          joined_may_count++;
        }
        if (max_held_counts[target] > joined_may_count) {
          max_held_counts[target] = joined_may_count;
        }
      }
    }
  }

  return ACTION_VALIDATE_OK;
}

uint16_t
action_engine_program_macro_dependencies(const action_program_t *program) {
  uint64_t reachable_steps = UINT64_C(1);
  uint16_t dependencies = 0u;

  if (program == NULL || program->step_count == 0u ||
      program->step_count > ACTION_PROGRAM_MAX_STEPS) {
    return 0u;
  }

  for (uint8_t i = 0u; i < program->step_count; i++) {
    const action_step_t *step = &program->steps[i];
    uint8_t next = (uint8_t)(i + 1u);
    if ((reachable_steps & (UINT64_C(1) << i)) == 0u) {
      continue;
    }
    if (((action_opcode_t)step->opcode == ACTION_OP_KEY_DOWN ||
         (action_opcode_t)step->opcode == ACTION_OP_KEY_TAP) &&
        step->arg16 >= (uint16_t)CUSTOM_MACRO_1 &&
        step->arg16 <= (uint16_t)CUSTOM_MACRO_16) {
      dependencies |= (uint16_t)(1u <<
                                 (step->arg16 - (uint16_t)CUSTOM_MACRO_1));
    }
    if ((action_opcode_t)step->opcode == ACTION_OP_END) {
      continue;
    }
    if (next < program->step_count) {
      reachable_steps |= UINT64_C(1) << next;
    }
    if ((action_opcode_t)step->opcode == ACTION_OP_IF_STATE_SKIP) {
      uint16_t skipped = (uint16_t)i + 1u + step->arg16;
      if (skipped < program->step_count) {
        reachable_steps |= UINT64_C(1) << skipped;
      }
    }
  }
  return dependencies;
}

static action_validation_result_t
action_engine_validate_macro_graph(const action_profile_t *profile) {
  uint16_t dependencies[ACTION_PROGRAM_COUNT] = {0};
  uint16_t pending = UINT16_MAX;
  uint8_t depths[ACTION_PROGRAM_COUNT] = {0};
  bool depth_exceeded = false;

  for (uint8_t program = 0u; program < ACTION_PROGRAM_COUNT; program++) {
    dependencies[program] =
        action_engine_program_macro_dependencies(&profile->programs[program]);
  }

  /* Resolve sinks first. A node becomes ready only after every program it can
   * invoke has a known depth. Lack of progress therefore proves a cycle. The
   * root program consumes one runtime instance, so any path longer than the
   * fixed runtime instance pool is invalid. */
  while (pending != 0u) {
    bool made_progress = false;
    for (uint8_t program = 0u; program < ACTION_PROGRAM_COUNT; program++) {
      uint16_t program_bit = (uint16_t)(1u << program);
      uint8_t depth = 1u;
      if ((pending & program_bit) == 0u ||
          (dependencies[program] & pending) != 0u) {
        continue;
      }
      for (uint8_t target = 0u; target < ACTION_PROGRAM_COUNT; target++) {
        if ((dependencies[program] & (uint16_t)(1u << target)) != 0u &&
            depth <= depths[target]) {
          depth = (uint8_t)(depths[target] + 1u);
        }
      }
      depths[program] = depth;
      pending &= (uint16_t)~program_bit;
      depth_exceeded =
          depth_exceeded || depth > ACTION_ENGINE_MAX_INSTANCES;
      made_progress = true;
    }
    if (!made_progress) {
      return ACTION_VALIDATE_MACRO_CYCLE;
    }
  }
  return depth_exceeded ? ACTION_VALIDATE_MACRO_DEPTH : ACTION_VALIDATE_OK;
}

action_validation_result_t
action_engine_validate_profile(const action_profile_t *profile) {
  if (profile == NULL) {
    return ACTION_VALIDATE_BAD_ARGUMENT;
  }
  for (uint8_t program = 0u; program < ACTION_PROGRAM_COUNT; program++) {
    action_validation_result_t result =
        action_engine_validate_program(&profile->programs[program]);
    if (result != ACTION_VALIDATE_OK) {
      return result;
    }
  }
  {
    action_validation_result_t graph_result =
        action_engine_validate_macro_graph(profile);
    if (graph_result != ACTION_VALIDATE_OK) {
      return graph_result;
    }
  }
  for (uint8_t overlay = 0u; overlay < LED_STATE_OVERLAY_COUNT; overlay++) {
    const action_overlay_binding_t *binding = &profile->overlays[overlay];
    if (binding->state_index >= ACTION_STATE_COUNT ||
        binding->active_value > 1u || binding->follows_state > 1u ||
        binding->config.blend_mode >= (uint8_t)LED_OVERLAY_BLEND_MAX) {
      return ACTION_VALIDATE_BAD_ARGUMENT;
    }
  }
  return ACTION_VALIDATE_OK;
}

uint32_t action_engine_program_hash(const action_program_t *program) {
  const uint8_t *bytes = (const uint8_t *)program;
  uint32_t hash = 2166136261u;
  if (program == NULL) {
    return 0u;
  }
  for (uint32_t i = 0u; i < sizeof(*program); i++) {
    hash ^= bytes[i];
    hash *= 16777619u;
  }
  return hash;
}

bool action_engine_get_program(uint8_t profile_index, uint8_t program_index,
                               action_program_t *program_out) {
  if (profile_index >= ACTION_PROFILE_COUNT ||
      program_index >= ACTION_PROGRAM_COUNT || program_out == NULL) {
    return false;
  }
  memcpy(program_out, &action_profiles[profile_index].programs[program_index],
         sizeof(*program_out));
  return true;
}

bool action_engine_publish_validated_program(
    uint8_t profile_index, uint8_t program_index,
    const action_program_t *program) {
  if (profile_index >= ACTION_PROFILE_COUNT ||
      program_index >= ACTION_PROGRAM_COUNT ||
      !action_program_runtime_shape_is_sane(program)) {
    return false;
  }
  if (profile_index == active_profile_index) {
    int8_t running = action_instance_find_for_program(program_index);
    if (running >= 0) {
      action_instance_finish(&action_instances[(uint8_t)running]);
    }
    /* An accepted press belongs to the definition that was live when it was
     * queued. Do not let an edit make that old event execute new actions. */
    action_trigger_queue_remove_program(program_index);
    action_engine_advance_program_trigger_generation(program_index);
  }
  /* The caller owns proof that the complete snapshot (including its macro
   * graph) was validated. Publish only the durable slot so concurrent runtime
   * state changes and unrelated profile fields cannot be rolled back. */
  memmove(&action_profiles[profile_index].programs[program_index], program,
          sizeof(*program));
  action_profile_revision_counters[profile_index]++;
  return true;
}

bool action_engine_set_program(uint8_t profile_index, uint8_t program_index,
                               const action_program_t *program, bool persist) {
  if (profile_index >= ACTION_PROFILE_COUNT ||
      program_index >= ACTION_PROGRAM_COUNT ||
      action_engine_validate_program(program) != ACTION_VALIDATE_OK) {
    return false;
  }
  memcpy(&profile_update_scratch, &action_profiles[profile_index],
         sizeof(profile_update_scratch));
  memcpy(&profile_update_scratch.programs[program_index], program,
         sizeof(*program));
  if (action_engine_validate_macro_graph(&profile_update_scratch) !=
      ACTION_VALIDATE_OK) {
    return false;
  }
  if (persist &&
      !action_store_save_profile(profile_index, &profile_update_scratch)) {
    return false;
  }
  return action_engine_publish_validated_program(
      profile_index, program_index,
      &profile_update_scratch.programs[program_index]);
}

bool action_engine_get_profile(uint8_t profile_index,
                               action_profile_t *profile_out) {
  if (profile_index >= ACTION_PROFILE_COUNT || profile_out == NULL) {
    return false;
  }
  memcpy(profile_out, &action_profiles[profile_index], sizeof(*profile_out));
  return true;
}

const action_profile_t *action_engine_profile_view(uint8_t profile_index) {
  if (profile_index >= ACTION_PROFILE_COUNT) {
    return NULL;
  }
  return &action_profiles[profile_index];
}

const volatile uint32_t *
action_engine_profile_revision_source(uint8_t profile_index) {
  if (profile_index >= ACTION_PROFILE_COUNT) {
    return NULL;
  }
  return &action_profile_revision_counters[profile_index];
}

bool action_engine_set_profile(uint8_t profile_index,
                               const action_profile_t *profile,
                               bool persist) {
  action_profile_t *sanitized = &profile_update_scratch;
  if (profile_index >= ACTION_PROFILE_COUNT || profile == NULL) {
    return false;
  }
  memmove(sanitized, profile, sizeof(*sanitized));
  for (uint8_t overlay = 0u; overlay < LED_STATE_OVERLAY_COUNT; overlay++) {
    action_overlay_binding_t *binding = &sanitized->overlays[overlay];
    binding->follows_state = binding->follows_state ? 1u : 0u;
    binding->active_value = binding->active_value ? 1u : 0u;
  }
  if (action_engine_validate_profile(sanitized) != ACTION_VALIDATE_OK) {
    return false;
  }

  if (persist && !action_store_save_profile(profile_index, sanitized)) {
    return false;
  }
  if (profile_index == active_profile_index) {
    /* As with profile activation, invalidate releases owned by the replaced
     * runtime after cancelling its executions. */
    for (uint8_t i = 0u; i < ACTION_ENGINE_MAX_INSTANCES; i++) {
      if (action_instances[i].active) {
        action_instance_finish(&action_instances[i]);
      }
    }
    action_engine_advance_all_trigger_generations();
    action_trigger_queue_clear();
    led_matrix_clear_state_overlays();
  }
  memcpy(&action_profiles[profile_index], sanitized, sizeof(*sanitized));
  action_profile_revision_counters[profile_index]++;
  if (profile_index == active_profile_index) {
    runtime_state_bits = sanitized->initial_state_bits;
    action_engine_configure_state_overlays();
  }
  return true;
}

bool action_engine_copy_profile(uint8_t source_profile_index,
                                uint8_t target_profile_index) {
  if (source_profile_index >= ACTION_PROFILE_COUNT ||
      target_profile_index >= ACTION_PROFILE_COUNT) {
    return false;
  }
  if (source_profile_index == target_profile_index) {
    return true;
  }
  memcpy(&profile_update_scratch, &action_profiles[source_profile_index],
         sizeof(profile_update_scratch));
  return action_engine_set_profile(target_profile_index,
                                   &profile_update_scratch, false);
}

bool action_engine_reset_profile(uint8_t profile_index) {
  if (profile_index >= ACTION_PROFILE_COUNT) {
    return false;
  }
  action_profile_set_defaults(&profile_update_scratch);
  return action_engine_set_profile(profile_index, &profile_update_scratch,
                                   false);
}

void action_engine_reset_all_profiles(void) {
  for (uint8_t profile_index = 0u; profile_index < ACTION_PROFILE_COUNT;
       profile_index++) {
    (void)action_engine_reset_profile(profile_index);
  }
}

bool action_engine_get_overlay_binding(uint8_t profile_index,
                                       uint8_t overlay_id,
                                       action_overlay_binding_t *binding_out) {
  if (profile_index >= ACTION_PROFILE_COUNT ||
      overlay_id >= LED_STATE_OVERLAY_COUNT || binding_out == NULL) {
    return false;
  }
  memcpy(binding_out, &action_profiles[profile_index].overlays[overlay_id],
         sizeof(*binding_out));
  return true;
}

bool action_engine_publish_validated_overlay_binding(
    uint8_t profile_index, uint8_t overlay_id,
    const action_overlay_binding_t *binding) {
  if (profile_index >= ACTION_PROFILE_COUNT ||
      overlay_id >= LED_STATE_OVERLAY_COUNT || binding == NULL ||
      binding->state_index >= ACTION_STATE_COUNT || binding->active_value > 1u ||
      binding->follows_state > 1u ||
      binding->config.blend_mode >= (uint8_t)LED_OVERLAY_BLEND_MAX) {
    return false;
  }
  memmove(&action_profiles[profile_index].overlays[overlay_id], binding,
          sizeof(*binding));
  action_profile_revision_counters[profile_index]++;
  if (profile_index == active_profile_index) {
    (void)led_matrix_configure_state_overlay(overlay_id, &binding->config);
    action_engine_sync_state_overlays();
  }
  return true;
}

bool action_engine_set_overlay_binding(uint8_t profile_index,
                                       uint8_t overlay_id,
                                       const action_overlay_binding_t *binding,
                                       bool persist) {
  action_overlay_binding_t sanitized = {0};
  if (profile_index >= ACTION_PROFILE_COUNT ||
      overlay_id >= LED_STATE_OVERLAY_COUNT || binding == NULL) {
    return false;
  }
  memcpy(&sanitized, binding, sizeof(sanitized));
  sanitized.follows_state = sanitized.follows_state ? 1u : 0u;
  sanitized.active_value = sanitized.active_value ? 1u : 0u;
  if (sanitized.state_index >= ACTION_STATE_COUNT) {
    return false;
  }
  if (sanitized.config.blend_mode >= (uint8_t)LED_OVERLAY_BLEND_MAX) {
    return false;
  }
  memcpy(&profile_update_scratch, &action_profiles[profile_index],
         sizeof(profile_update_scratch));
  memcpy(&profile_update_scratch.overlays[overlay_id], &sanitized,
         sizeof(sanitized));
  if (persist &&
      !action_store_save_profile(profile_index, &profile_update_scratch)) {
    return false;
  }
  return action_engine_publish_validated_overlay_binding(
      profile_index, overlay_id,
      &profile_update_scratch.overlays[overlay_id]);
}

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC pop_options
#endif

bool action_engine_get_state(uint8_t state_index) {
  if (state_index >= ACTION_STATE_COUNT) {
    return false;
  }
  return (runtime_state_bits & (uint16_t)(1u << state_index)) != 0u;
}

bool action_engine_set_state(uint8_t state_index, bool value) {
  action_profile_t *profile = NULL;
  uint16_t state_mask = 0u;
  bool previous_initial = false;
  if (state_index >= ACTION_STATE_COUNT) {
    return false;
  }
  profile = &action_profiles[active_profile_index];
  state_mask = (uint16_t)(1u << state_index);
  previous_initial = (profile->initial_state_bits & state_mask) != 0u;
  if (value) {
    profile->initial_state_bits |= state_mask;
  } else {
    profile->initial_state_bits &= (uint16_t)~state_mask;
  }
  if (previous_initial != value) {
    action_profile_revision_counters[active_profile_index]++;
  }
  return action_engine_set_runtime_state(state_index, value);
}

bool action_engine_toggle_state(uint8_t state_index) {
  if (state_index >= ACTION_STATE_COUNT) {
    return false;
  }
  return action_engine_set_state(state_index,
                                 !action_engine_get_state(state_index));
}

uint16_t action_engine_state_bits(void) { return runtime_state_bits; }

uint16_t action_engine_initial_state_bits(void) {
  return action_profiles[active_profile_index].initial_state_bits;
}

#undef ACTION_ENGINE_COLD_NOINLINE
