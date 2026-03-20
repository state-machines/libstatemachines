/*
 * circuit_breaker.c -- v0 reference consumer.
 *
 * Static schema in .rodata; guards read mutable counters via
 * state_machine_userdata(); wrapper functions dispatch, then update
 * counters on accept based on (from, to).
 */

#include "circuit_breaker.h"

#include <errno.h>

// Guards.

static bool
g_failure_threshold_exceeded(const state_machine_t *m,
                             state_machine_event_id_t event,
                             const void *payload)
{
    const cb_t *cb = (const cb_t *)state_machine_userdata(m);

    (void)event;
    (void)payload;

    /* Trip on the Nth failure: pre-increment count + this event >= threshold. */
    return (cb->failure_count + 1u) >= cb->failure_threshold;
}

static bool
g_timeout_elapsed(const state_machine_t *m,
                  state_machine_event_id_t event,
                  const void *payload)
{
    const cb_t *cb = (const cb_t *)state_machine_userdata(m);

    (void)event;
    (void)payload;

    return (cb->now_ms - cb->opened_at_ms) >= cb->timeout_ms;
}

enum {
    GU_FAILURE_THRESHOLD_EXCEEDED = 0,
    GU_TIMEOUT_ELAPSED            = 1
};

static const state_machine_guard_def_t cb_guards[] = {
    { .fn = g_failure_threshold_exceeded, .name = "failure_threshold_exceeded" },
    { .fn = g_timeout_elapsed,            .name = "timeout_elapsed"            }
};

// States (flat - no hierarchy in this example).

static const state_machine_state_def_t cb_states[] = {
    { .id = CB_CLOSED,    .parent = STATE_MACHINE_ID_NONE,
      .initial_child = STATE_MACHINE_ID_NONE,
      .depth = 0, .flags = 0, .name = "closed" },
    { .id = CB_OPEN,      .parent = STATE_MACHINE_ID_NONE,
      .initial_child = STATE_MACHINE_ID_NONE,
      .depth = 0, .flags = 0, .name = "open" },
    { .id = CB_HALF_OPEN, .parent = STATE_MACHINE_ID_NONE,
      .initial_child = STATE_MACHINE_ID_NONE,
      .depth = 0, .flags = 0, .name = "half_open" }
};

// Transitions.
//
// Declaration order is selection order. For ev_failure we list
// the threshold-trip first so a passing guard wins; otherwise the
// unguarded CLOSED -> CLOSED self-loop catches the event below the
// threshold. The selected `transition_index` therefore reveals which
// branch fired.

/* ev_success */
static const state_machine_state_id_t success_src_closed[]    = { CB_CLOSED };
static const state_machine_state_id_t success_src_half_open[] = { CB_HALF_OPEN };

static const state_machine_transition_def_t success_t[] = {
    /* CLOSED -> CLOSED (resets failure_count in the wrapper). */
    { .sources = success_src_closed,    .guards = NULL, .target = CB_CLOSED,
      .source_count = 1, .guard_count = 0, .ordinal = 0,
      .flags = 0, ._reserved = 0 },
    /* HALF_OPEN -> CLOSED. */
    { .sources = success_src_half_open, .guards = NULL, .target = CB_CLOSED,
      .source_count = 1, .guard_count = 0, .ordinal = 1,
      .flags = 0, ._reserved = 0 }
};

/* ev_failure */
static const state_machine_state_id_t failure_src_closed[]    = { CB_CLOSED };
static const state_machine_state_id_t failure_src_half_open[] = { CB_HALF_OPEN };
static const state_machine_guard_id_t failure_threshold_g[]   = {
    GU_FAILURE_THRESHOLD_EXCEEDED
};

static const state_machine_transition_def_t failure_t[] = {
    /* CLOSED -> OPEN, guarded by threshold (selected when threshold exceeded). */
    { .sources = failure_src_closed,    .guards = failure_threshold_g,
      .target = CB_OPEN,
      .source_count = 1, .guard_count = 1, .ordinal = 0,
      .flags = 0, ._reserved = 0 },
    /* CLOSED -> CLOSED (taken below threshold; wrapper increments count). */
    { .sources = failure_src_closed,    .guards = NULL,
      .target = CB_CLOSED,
      .source_count = 1, .guard_count = 0, .ordinal = 1,
      .flags = 0, ._reserved = 0 },
    /* HALF_OPEN -> OPEN (single failed probe re-opens the circuit). */
    { .sources = failure_src_half_open, .guards = NULL,
      .target = CB_OPEN,
      .source_count = 1, .guard_count = 0, .ordinal = 2,
      .flags = 0, ._reserved = 0 }
};

/* ev_tick */
static const state_machine_state_id_t tick_src_open[]   = { CB_OPEN };
static const state_machine_guard_id_t tick_timeout_g[]  = { GU_TIMEOUT_ELAPSED };

static const state_machine_transition_def_t tick_t[] = {
    /* OPEN -> HALF_OPEN, guarded by timeout. */
    { .sources = tick_src_open, .guards = tick_timeout_g,
      .target = CB_HALF_OPEN,
      .source_count = 1, .guard_count = 1, .ordinal = 0,
      .flags = 0, ._reserved = 0 }
};

// Events.

static const state_machine_event_def_t cb_events[] = {
    { .id = CB_EV_SUCCESS, .transition_count = 2,
      .transitions = success_t, .name = "success" },
    { .id = CB_EV_FAILURE, .transition_count = 3,
      .transitions = failure_t, .name = "failure" },
    { .id = CB_EV_TICK,    .transition_count = 1,
      .transitions = tick_t,    .name = "tick" }
};

// Schema.

static const state_machine_def_t cb_def = {
    .magic        = STATE_MACHINE_MAGIC,
    .abi_epoch    = STATE_MACHINE_ABI_EPOCH,
    .abi_revision = STATE_MACHINE_ABI_REVISION,
    .struct_size  = sizeof(state_machine_def_t),
    .flags        = 0,
    .spec_version = STATE_MACHINE_SPEC_VERSION,
    ._reserved    = 0,
    .name         = "circuit_breaker",
    .states       = cb_states,
    .events       = cb_events,
    .guards       = cb_guards,
    .state_count  = 3,
    .initial      = CB_CLOSED,
    .event_count  = 3,
    .guard_count  = 2
};

const state_machine_def_t *
cb_schema(void)
{
    return &cb_def;
}

// Wrapper API.

int
cb_init(cb_t *cb, uint32_t failure_threshold, uint64_t timeout_ms)
{
    int rc;

    if (cb == NULL)
        return EINVAL;
    if (failure_threshold == 0)
        return EINVAL;

    cb->failure_threshold = failure_threshold;
    cb->timeout_ms        = timeout_ms;
    cb->failure_count     = 0;
    cb->opened_at_ms      = 0;
    cb->now_ms            = 0;

    /* userdata = cb so guards can reach the counters. */
    rc = state_machine_init(&cb->machine, &cb_def, cb);
    return rc;
}

cb_state_id_t
cb_current(const cb_t *cb)
{
    return (cb_state_id_t)state_machine_current(&cb->machine);
}

int
cb_record_success(cb_t *cb, uint64_t now_ms)
{
    state_machine_result_t r;
    int rc;

    cb->now_ms = now_ms;
    rc = state_machine_dispatch_ex(&cb->machine, CB_EV_SUCCESS, NULL, &r);

    if (r.outcome == STATE_MACHINE_OUTCOME_ACCEPTED) {
        /* Any success path lands us in CLOSED; reset failure count. */
        cb->failure_count = 0;
    }
    return rc;
}

int
cb_record_failure(cb_t *cb, uint64_t now_ms)
{
    state_machine_result_t r;
    int rc;

    cb->now_ms = now_ms;
    rc = state_machine_dispatch_ex(&cb->machine, CB_EV_FAILURE, NULL, &r);

    if (r.outcome == STATE_MACHINE_OUTCOME_ACCEPTED) {
        if (r.to == CB_OPEN) {
            cb->opened_at_ms  = now_ms;
            cb->failure_count = 0;
        } else {
            /* CLOSED -> CLOSED self-loop. */
            cb->failure_count += 1u;
        }
    }
    return rc;
}

int
cb_tick(cb_t *cb, uint64_t now_ms)
{
    state_machine_result_t r;
    int rc;

    cb->now_ms = now_ms;
    rc = state_machine_dispatch_ex(&cb->machine, CB_EV_TICK, NULL, &r);
    /* No counter side-effects on tick: guard already gates the transition. */
    return rc;
}
