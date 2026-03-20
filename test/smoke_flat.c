/*
 * smoke_flat.c -- Flat-schema smoke test for libstatemachines.
 *
 * Mirrors test/conformance/airlock.json: 2 states, 2 events, no guards.
 * Exercises ACCEPTED, NO_TRANSITION, INVALID_ARG (event OOB), and
 * INVALID_ARG (m == NULL, out untouched). Asserts every result field.
 */

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "state_machine.h"

static const state_machine_state_def_t airlock_states[] = {
    { .id = 0, .parent = STATE_MACHINE_ID_NONE,
      .initial_child = STATE_MACHINE_ID_NONE,
      .depth = 0, .flags = 0, .name = "pressurized" },
    { .id = 1, .parent = STATE_MACHINE_ID_NONE,
      .initial_child = STATE_MACHINE_ID_NONE,
      .depth = 0, .flags = 0, .name = "vacuum" },
};

static const state_machine_state_id_t depressurize_src[] = { 0 };
static const state_machine_state_id_t repressurize_src[] = { 1 };

static const state_machine_transition_def_t depressurize_t[] = {
    { .sources = depressurize_src, .guards = NULL, .target = 1,
      .source_count = 1, .guard_count = 0, .ordinal = 0,
      .flags = 0, ._reserved = 0 },
};
static const state_machine_transition_def_t repressurize_t[] = {
    { .sources = repressurize_src, .guards = NULL, .target = 0,
      .source_count = 1, .guard_count = 0, .ordinal = 0,
      .flags = 0, ._reserved = 0 },
};

static const state_machine_event_def_t airlock_events[] = {
    { .id = 0, .transition_count = 1,
      .transitions = depressurize_t, .name = "depressurize" },
    { .id = 1, .transition_count = 1,
      .transitions = repressurize_t, .name = "repressurize" },
};

static const state_machine_def_t airlock = {
    .magic        = STATE_MACHINE_MAGIC,
    .abi_epoch    = STATE_MACHINE_ABI_EPOCH,
    .abi_revision = STATE_MACHINE_ABI_REVISION,
    .struct_size  = sizeof(state_machine_def_t),
    .flags        = 0,
    .spec_version = STATE_MACHINE_SPEC_VERSION,
    ._reserved    = 0,
    .name         = "airlock",
    .states       = airlock_states,
    .events       = airlock_events,
    .guards       = NULL,
    .state_count  = 2,
    .initial      = 0,
    .event_count  = 2,
    .guard_count  = 0,
};

int main(void) {
    state_machine_t m = STATE_MACHINE_INITIALIZER;
    state_machine_result_t r;
    int rc;

    rc = state_machine_init(&m, &airlock, NULL);
    assert(rc == 0);
    assert(state_machine_current(&m) == 0);
    assert(state_machine_generation(&m) == 0);
    assert(state_machine_userdata(&m) == NULL);
    assert(state_machine_def(&m) == &airlock);   /* def accessor */
    assert(state_machine_is_in(&m, 0));
    assert(!state_machine_is_in(&m, 1));
    assert(!state_machine_is_in(&m, STATE_MACHINE_ID_NONE));
    assert(!state_machine_is_in(&m, 99));

    /* ACCEPTED depressurize: 0 -> 1. */
    rc = state_machine_dispatch_ex(&m, 0, NULL, &r);
    assert(rc == 0);
    assert(r.outcome == STATE_MACHINE_OUTCOME_ACCEPTED);
    assert(r.from == 0 && r.to == 1);
    assert(r.event == 0);
    assert(r.transition_index == 0);
    assert(r.guard_ordinal == UINT16_MAX);
    assert(r.guard_id == STATE_MACHINE_ID_NONE);
    assert(r.action_id == STATE_MACHINE_ID_NONE);
    assert(r.transitions_tested == 1);
    assert(r.generation_after == 1);
    assert(state_machine_current(&m) == 1);
    assert(state_machine_generation(&m) == 1);

    /* NO_TRANSITION: depressurize while at vacuum. */
    rc = state_machine_dispatch_ex(&m, 0, NULL, &r);
    assert(rc == EOPNOTSUPP);
    assert(r.outcome == STATE_MACHINE_OUTCOME_NO_TRANSITION);
    assert(r.from == 1 && r.to == 1);
    assert(r.transition_index == UINT16_MAX);
    assert(r.action_id == STATE_MACHINE_ID_NONE);
    assert(r.transitions_tested == 1);  /* one source-match attempted, failed */
    assert(r.generation_after == 1);    /* unchanged */

    /* ACCEPTED repressurize: 1 -> 0. */
    rc = state_machine_dispatch(&m, 1, NULL);
    assert(rc == 0);
    assert(state_machine_current(&m) == 0);
    assert(state_machine_generation(&m) == 2);

    /* INVALID_ARG: event OOB. m != NULL → out IS filled. */
    rc = state_machine_dispatch_ex(&m, 99, NULL, &r);
    assert(rc == EINVAL);
    assert(r.outcome == STATE_MACHINE_OUTCOME_INVALID_ARG);
    assert(r.from == 0 && r.to == 0);
    assert(r.event == 99);
    assert(r.action_id == STATE_MACHINE_ID_NONE);
    assert(r.transitions_tested == 0);
    assert(r.generation_after == 2);

    /* INVALID_ARG: m == NULL. out_result MUST be left untouched. */
    state_machine_result_t sentinel;
    memset(&sentinel, 0xAB, sizeof(sentinel));
    state_machine_result_t before = sentinel;
    rc = state_machine_dispatch_ex(NULL, 0, NULL, &sentinel);
    assert(rc == EINVAL);
    assert(memcmp(&sentinel, &before, sizeof(sentinel)) == 0);

    /* Schema-side accessors. */
    assert(strcmp(state_machine_state_name(&airlock, 0), "pressurized") == 0);
    assert(strcmp(state_machine_event_name(&airlock, 1), "repressurize") == 0);
    assert(state_machine_state_name(&airlock, 99) == NULL);
    assert(state_machine_event_name(NULL, 0) == NULL);

    /* ABI introspection. */
    assert(state_machine_abi_version()
           == (((uint32_t)STATE_MACHINE_ABI_EPOCH << 16)
               | (uint32_t)STATE_MACHINE_ABI_REVISION));
    assert(strcmp(state_machine_outcome_name(STATE_MACHINE_OUTCOME_ACCEPTED),
                  "ACCEPTED") == 0);

    state_machine_destroy(&m);
    assert(state_machine_current(&m) == 0);
    assert(state_machine_generation(&m) == 0);

    printf("smoke_flat: OK\n");
    return 0;
}
