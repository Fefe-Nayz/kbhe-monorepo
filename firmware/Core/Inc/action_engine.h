#ifndef ACTION_ENGINE_H_
#define ACTION_ENGINE_H_

#include "led_matrix.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ACTION_PROFILE_COUNT 4u
#define ACTION_PROGRAM_COUNT 16u
#define ACTION_PROGRAM_MAX_STEPS 32u
#define ACTION_STATE_COUNT 16u
#define ACTION_ENGINE_MAX_INSTANCES 4u
/* A short fixed FIFO absorbs simultaneous physical/fan-out triggers when all
 * runtime instances are busy. It is deliberately allocation-free and large
 * enough to retain one full burst across every macro slot. */
#define ACTION_ENGINE_TRIGGER_QUEUE_CAPACITY ACTION_PROGRAM_COUNT
/* Shared by all running instances so concurrent macros cannot multiply the
 * amount of action/overlay work performed in one input scan. */
#define ACTION_ENGINE_GLOBAL_STEPS_PER_TICK 32u
#define ACTION_ENGINE_MAX_HELD_OUTPUTS 8u
#define ACTION_PROGRAM_VERSION 1u

#define ACTION_PROGRAM_FLAG_CANCEL_ON_RELEASE 0x01u
#define ACTION_PROGRAM_FLAG_RESTART_ON_TRIGGER 0x02u

typedef enum {
  ACTION_OP_NOP = 0,
  ACTION_OP_END = 1,
  ACTION_OP_KEY_DOWN = 2,
  ACTION_OP_KEY_UP = 3,
  ACTION_OP_KEY_TAP = 4,
  ACTION_OP_DELAY_MS = 5,
  ACTION_OP_STATE_SET = 6,
  ACTION_OP_STATE_TOGGLE = 7,
  ACTION_OP_IF_STATE_SKIP = 8,
  ACTION_OP_OVERLAY_SET = 9,
  ACTION_OP_MAX
} action_opcode_t;

/**
 * Compact, endian-stable instruction used by both RAW HID and flash storage.
 * - key opcodes: arg16 is the keycode
 * - delay: arg16 is milliseconds
 * - state set/toggle: arg8 is the state index, arg16 is the requested value
 * - conditional: arg8 bits 0..3 state index, bit 7 expected value; arg16 is
 *   the number of following instructions to skip when the condition is false
 * - overlay: arg8 is overlay id; arg16 0=off, 1=on, >1=pulse duration in ms
 */
typedef struct __attribute__((packed)) {
  uint8_t opcode;
  uint8_t arg8;
  uint16_t arg16;
} action_step_t;

typedef struct __attribute__((packed)) {
  uint8_t version;
  uint8_t flags;
  uint8_t step_count;
  uint8_t reserved;
  action_step_t steps[ACTION_PROGRAM_MAX_STEPS];
} action_program_t;

typedef struct __attribute__((packed)) {
  led_state_overlay_config_t config;
  uint8_t state_index;
  uint8_t active_value;
  uint8_t follows_state;
  uint8_t reserved;
} action_overlay_binding_t;

typedef struct __attribute__((packed)) {
  action_program_t programs[ACTION_PROGRAM_COUNT];
  action_overlay_binding_t overlays[LED_STATE_OVERLAY_COUNT];
  uint16_t initial_state_bits;
  uint16_t reserved;
} action_profile_t;

typedef enum {
  ACTION_VALIDATE_OK = 0,
  ACTION_VALIDATE_BAD_VERSION,
  ACTION_VALIDATE_TOO_MANY_STEPS,
  ACTION_VALIDATE_BAD_OPCODE,
  ACTION_VALIDATE_BAD_ARGUMENT,
  ACTION_VALIDATE_UNBALANCED_OUTPUT,
  ACTION_VALIDATE_MACRO_CYCLE,
  ACTION_VALIDATE_MACRO_DEPTH,
} action_validation_result_t;

void action_engine_init(void);
void action_engine_tick(uint32_t now_ms);
void action_engine_cancel_all(void);

bool action_engine_activate_profile(uint8_t profile_index);
uint8_t action_engine_active_profile(void);

bool action_engine_trigger_program(uint8_t program_index);
void action_engine_release_program_trigger(uint8_t program_index);
/** Number of accepted triggers currently waiting for a runtime instance. */
uint8_t action_engine_pending_trigger_count(void);
/** True when no macro instance is running and no accepted trigger is queued. */
bool action_engine_is_idle(void);
/** Monotonic, saturating count of triggers rejected because the FIFO was full. */
uint32_t action_engine_dropped_trigger_count(void);

action_validation_result_t
action_engine_validate_program(const action_program_t *program);
/** Return the reachable Macro1..Macro16 triggers emitted by a program. */
uint16_t
action_engine_program_macro_dependencies(const action_program_t *program);
/** Validate every program, overlay binding, and the inter-program call graph. */
action_validation_result_t
action_engine_validate_profile(const action_profile_t *profile);
uint32_t action_engine_program_hash(const action_program_t *program);

bool action_engine_get_program(uint8_t profile_index, uint8_t program_index,
                               action_program_t *program_out);
/**
 * Publish one program already validated as part of a complete profile
 * snapshot. Performs no graph validation or persistence and preserves all
 * unrelated live profile/runtime state.
 */
bool action_engine_publish_validated_program(
    uint8_t profile_index, uint8_t program_index,
    const action_program_t *program);
bool action_engine_set_program(uint8_t profile_index, uint8_t program_index,
                               const action_program_t *program,
                               bool persist);
bool action_engine_get_profile(uint8_t profile_index,
                               action_profile_t *profile_out);
const action_profile_t *action_engine_profile_view(uint8_t profile_index);
/** Revision of the live profile view, incremented after every mutation. */
const volatile uint32_t *
action_engine_profile_revision_source(uint8_t profile_index);
bool action_engine_set_profile(uint8_t profile_index,
                               const action_profile_t *profile,
                               bool persist);
/** Copy/reset lifecycle helpers used by the settings profile manager. */
bool action_engine_copy_profile(uint8_t source_profile_index,
                                uint8_t target_profile_index);
bool action_engine_reset_profile(uint8_t profile_index);
void action_engine_reset_all_profiles(void);

bool action_engine_get_overlay_binding(uint8_t profile_index,
                                       uint8_t overlay_id,
                                       action_overlay_binding_t *binding_out);
/** Trusted targeted counterpart to action_engine_set_overlay_binding(). */
bool action_engine_publish_validated_overlay_binding(
    uint8_t profile_index, uint8_t overlay_id,
    const action_overlay_binding_t *binding);
bool action_engine_set_overlay_binding(uint8_t profile_index,
                                       uint8_t overlay_id,
                                       const action_overlay_binding_t *binding,
                                       bool persist);

bool action_engine_get_state(uint8_t state_index);
bool action_engine_set_state(uint8_t state_index, bool value);
bool action_engine_toggle_state(uint8_t state_index);
uint16_t action_engine_state_bits(void);

/** Persistence backend. Implemented by action_store.c. */
bool action_store_load_profile(uint8_t profile_index,
                               action_profile_t *profile_out);
/** Queue profile persistence; completion is drained by action_store_async_task. */
bool action_store_save_profile(uint8_t profile_index,
                               const action_profile_t *profile);
void action_store_async_task(void);

#ifdef __cplusplus
}
#endif

#endif
