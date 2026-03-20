/*
 * smoke_action.c -- Transition action smoke test.
 */

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>

#include "state_machine.h"

enum { ST_A = 0, ST_B = 1 };
enum { EV_GO = 0 };
enum { ACT_RECORD = 0 };

struct action_log {
    unsigned calls;
    state_machine_event_id_t event;
    state_machine_state_id_t state_seen;
    uint64_t generation_seen;
    int reentry_rc;
    const void *payload_seen;
};

static void
record_action(const state_machine_t *m, state_machine_event_id_t event,
              void *payload)
{
    struct action_log *log = state_machine_userdata(m);

    log->calls++;
    log->event = event;
    log->state_seen = state_machine_current(m);
    log->generation_seen = state_machine_generation(m);
    log->reentry_rc = state_machine_dispatch((state_machine_t *)m, event, payload);
    log->payload_seen = payload;
}

static const state_machine_state_def_t states[] = {
    { .id = ST_A, .parent = STATE_MACHINE_ID_NONE,
      .initial_child = STATE_MACHINE_ID_NONE,
      .depth = 0, .flags = 0, .name = "a" },
    { .id = ST_B, .parent = STATE_MACHINE_ID_NONE,
      .initial_child = STATE_MACHINE_ID_NONE,
      .depth = 0, .flags = 0, .name = "b" },
};

static const state_machine_state_id_t go_src[] = { ST_A };

static const state_machine_transition_def_t go_transitions[] = {
    { .sources = go_src, .guards = NULL, .target = ST_B,
      .source_count = 1, .guard_count = 0,
      .action = ACT_RECORD, .ordinal = 0,
      .flags = STATE_MACHINE_TRANSITION_F_ACTION, ._reserved = 0 },
};

static const state_machine_event_def_t events[] = {
    { .id = EV_GO, .transition_count = 1,
      .transitions = go_transitions, .name = "go" },
};

static const state_machine_action_def_t actions[] = {
    { .fn = record_action, .name = "record" },
};

static const state_machine_def_t machine_def = {
    .magic        = STATE_MACHINE_MAGIC,
    .abi_epoch    = STATE_MACHINE_ABI_EPOCH,
    .abi_revision = STATE_MACHINE_ABI_REVISION,
    .struct_size  = sizeof(state_machine_def_t),
    .flags        = 0,
    .spec_version = STATE_MACHINE_SPEC_VERSION,
    ._reserved    = 0,
    .name         = "action_smoke",
    .states       = states,
    .events       = events,
    .guards       = NULL,
    .actions      = actions,
    .state_count  = 2,
    .initial      = ST_A,
    .event_count  = 1,
    .guard_count  = 0,
    .action_count = 1,
};

int
main(void)
{
    state_machine_t m = STATE_MACHINE_INITIALIZER;
    state_machine_result_t r;
    struct action_log log = { 0 };
    int payload = 42;

    assert(state_machine_init(&m, &machine_def, &log) == 0);
    assert(state_machine_dispatch_ex(&m, EV_GO, &payload, &r) == 0);

    assert(log.calls == 1);
    assert(log.event == EV_GO);
    assert(log.state_seen == ST_B);
    assert(log.generation_seen == 1);
    assert(log.reentry_rc == EBUSY);
    assert(log.payload_seen == &payload);

    assert(r.outcome == STATE_MACHINE_OUTCOME_ACCEPTED);
    assert(r.from == ST_A);
    assert(r.to == ST_B);
    assert(r.action_id == ACT_RECORD);
    assert(r.generation_after == 1);

    assert(state_machine_dispatch_ex(&m, EV_GO, &payload, &r) == EOPNOTSUPP);
    assert(log.calls == 1);
    assert(r.action_id == STATE_MACHINE_ID_NONE);

    printf("smoke_action: OK\n");
    return 0;
}
