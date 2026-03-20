/*
 * state_machine.h - Runtime API for libstatemachines.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Public header. Defines the runtime instance, dispatch result,
 * outcome enum, and full public function surface.
 *
 * Schema-side types (state_def, transition_def, event_def, guard_def,
 * def_t) live in state_machine_schema.h, which this header includes.
 */

#ifndef STATE_MACHINE_H
#define STATE_MACHINE_H

#include "state_machine_schema.h"

#ifdef __cplusplus
extern "C" {
#endif

// Diagnostic outcome and result.

typedef enum {
    STATE_MACHINE_OUTCOME_ACCEPTED      = 0,
    STATE_MACHINE_OUTCOME_INVALID_ARG   = 1,
    STATE_MACHINE_OUTCOME_NO_TRANSITION = 2,
    STATE_MACHINE_OUTCOME_GUARD_FAILED  = 3,
    STATE_MACHINE_OUTCOME_REENTRY       = 4
} state_machine_outcome_t;

typedef struct {
    state_machine_outcome_t  outcome;
    state_machine_state_id_t from;
    state_machine_state_id_t to;
    state_machine_event_id_t event;
    uint16_t                 transition_index;
    uint16_t                 guard_ordinal;
    state_machine_guard_id_t guard_id;
    state_machine_action_id_t action_id;
    uint16_t                 transitions_tested;
    uint64_t                 generation_after;
} state_machine_result_t;

// Runtime instance.
//
// Fields are exposed only to allow embedding in caller structures. Direct
// field access from consumer code is forbidden; use the accessors below.
// The struct's sizeof and field offsets are part of the ABI contract.

struct state_machine {
    const state_machine_def_t *def;
    void                      *userdata;
    uint64_t                   generation;
    state_machine_state_id_t   current;
    uint16_t                   _dispatch_flag;
    uint16_t                   _reserved[2];
};

/*
 * Zero-initializer suitable for static storage.
 *
 *     state_machine_t my_machine = STATE_MACHINE_INITIALIZER;
 */
#define STATE_MACHINE_INITIALIZER { 0 }

// Public API.

/* Lifecycle. */
int   state_machine_init   (state_machine_t *m,
                            const state_machine_def_t *def,
                            void *userdata);
void  state_machine_destroy(state_machine_t *m);

/* Dispatch. */
int   state_machine_dispatch   (state_machine_t *m,
                                state_machine_event_id_t event,
                                void *payload);
int   state_machine_dispatch_ex(state_machine_t *m,
                                state_machine_event_id_t event,
                                void *payload,
                                state_machine_result_t *out_result);

/* Instance accessors.
 *
 * Precondition for all four: `m != NULL` and initialized via
 * state_machine_init(). Passing NULL or an uninitialized instance is
 * undefined behavior - these are pure reads and intentionally perform no
 * NULL check (cf. init/dispatch/destroy, which do). MUST NOT run
 * concurrently with init/destroy/dispatch on the same instance.
 */
state_machine_state_id_t state_machine_current   (const state_machine_t *m);
uint64_t                 state_machine_generation(const state_machine_t *m);
void                    *state_machine_userdata  (const state_machine_t *m);
const state_machine_def_t *state_machine_def     (const state_machine_t *m);
bool                     state_machine_is_in     (const state_machine_t *m,
                                                  state_machine_state_id_t s);

/* Schema accessors. */
const char *state_machine_state_name(const state_machine_def_t *d,
                                     state_machine_state_id_t s);
const char *state_machine_event_name(const state_machine_def_t *d,
                                     state_machine_event_id_t e);

/* ABI introspection. */
uint32_t state_machine_abi_version(void);

/* Outcome rendering. */
const char *state_machine_outcome_name(state_machine_outcome_t o);

// Static assertions for runtime structs.
//
// Universal field-ordering invariants are asserted unconditionally.
// Exact sizes and pointer-field offsets are asserted on LP64/LLP64,
// matching the dominant target set.
//
// The state_machine_outcome_t enum's storage size is implementation-
// defined in C, but every conforming compiler we target picks a
// 4-byte representation for an enum with values 0..4; we assert that
// here so result_t's layout is locked in.

STATE_MACHINE_CTASSERT(sizeof(state_machine_outcome_t) == 4,
                       outcome_enum_size);

/* state_machine_t - universal field ordering. */
STATE_MACHINE_CTASSERT(offsetof(state_machine_t, def) == 0,
                       machine_def_first);

/* state_machine_result_t - universal field ordering. */
STATE_MACHINE_CTASSERT(offsetof(state_machine_result_t, outcome) == 0,
                       result_outcome_first);
STATE_MACHINE_CTASSERT(offsetof(state_machine_result_t, from) == 4,
                       result_from_offset);
STATE_MACHINE_CTASSERT(offsetof(state_machine_result_t, to) == 6,
                       result_to_offset);
STATE_MACHINE_CTASSERT(offsetof(state_machine_result_t, event) == 8,
                       result_event_offset);
STATE_MACHINE_CTASSERT(offsetof(state_machine_result_t, transition_index) == 10,
                       result_transition_index_offset);
STATE_MACHINE_CTASSERT(offsetof(state_machine_result_t, guard_ordinal) == 12,
                       result_guard_ordinal_offset);
STATE_MACHINE_CTASSERT(offsetof(state_machine_result_t, guard_id) == 14,
                       result_guard_id_offset);
STATE_MACHINE_CTASSERT(offsetof(state_machine_result_t, action_id) == 16,
                       result_action_id_offset);
STATE_MACHINE_CTASSERT(offsetof(state_machine_result_t, transitions_tested) == 18,
                       result_transitions_tested_offset);

/*
 * LP64 / LLP64 - exact sizes and pointer-field offsets.
 */
#if defined(__LP64__) || defined(_LP64) || defined(_WIN64)

/* state_machine_t: 2*ptr + u64 + 4*u16 → 32 bytes. */
STATE_MACHINE_CTASSERT(offsetof(state_machine_t, userdata) == 8,
                       machine_userdata_offset_lp64);
STATE_MACHINE_CTASSERT(offsetof(state_machine_t, generation) == 16,
                       machine_generation_offset_lp64);
STATE_MACHINE_CTASSERT(offsetof(state_machine_t, current) == 24,
                       machine_current_offset_lp64);
STATE_MACHINE_CTASSERT(offsetof(state_machine_t, _dispatch_flag) == 26,
                       machine_dispatch_flag_offset_lp64);
STATE_MACHINE_CTASSERT(offsetof(state_machine_t, _reserved) == 28,
                       machine_reserved_offset_lp64);
STATE_MACHINE_CTASSERT(sizeof(state_machine_t) == 32,
                       machine_size_lp64);

/*
 * state_machine_result_t: u32 enum + 8*u16 + 4B pad + u64 → 32 bytes.
 * The enum at offset 0 is 4 bytes; eight u16 fields pack to offset 20;
 * generation_after (u64) requires 8-byte alignment, so 4 bytes of pad
 * land between transitions_tested and generation_after.
 */
STATE_MACHINE_CTASSERT(offsetof(state_machine_result_t, generation_after) == 24,
                       result_generation_after_offset_lp64);
STATE_MACHINE_CTASSERT(sizeof(state_machine_result_t) == 32,
                       result_size_lp64);

#endif /* LP64 / LLP64 */

#ifdef __cplusplus
}
#endif

#endif /* STATE_MACHINE_H */
