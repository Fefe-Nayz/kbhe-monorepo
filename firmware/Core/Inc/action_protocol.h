#ifndef ACTION_PROTOCOL_H_
#define ACTION_PROTOCOL_H_

#include <stdbool.h>
#include <stdint.h>

#define CMD_GET_ACTION_CAPABILITIES 0x90u
#define CMD_GET_ACTION_PROGRAM_META 0x91u
#define CMD_GET_ACTION_PROGRAM_CHUNK 0x92u
#define CMD_BEGIN_SET_ACTION_PROGRAM 0x93u
#define CMD_SET_ACTION_PROGRAM_CHUNK 0x94u
#define CMD_COMMIT_ACTION_PROGRAM 0x95u
#define CMD_ABORT_ACTION_PROGRAM 0x96u
#define CMD_GET_ACTION_OVERLAY 0x97u
#define CMD_SET_ACTION_OVERLAY 0x98u
#define CMD_GET_ACTION_STATES 0x99u
#define CMD_SET_ACTION_STATE 0x9Au
#define CMD_COMMIT_PROFILE_DOCUMENT 0x9Bu
#define CMD_GET_PROFILE_DOCUMENT_META 0x9Cu

/** Returns true when command_id belongs to the action protocol range. */
bool action_protocol_handle(uint8_t command_id, const uint8_t *input,
                            uint8_t *output);

/** A mutating command may defer its response until its async Flash commit. */
bool action_protocol_response_is_deferred(void);

/** Build a deferred response once durable; false means still in progress. */
bool action_protocol_poll_deferred_response(uint8_t *output);

#endif
