/*
 * smoke_hsm.c -- Hierarchical-schema smoke test for libstatemachines.
 *
 * Tree:
 *     ROOT [0]                     (superstate)
 *       ├── ACTIVE [1]             (superstate; initial_child = RUN)
 *       │     ├── RUN  [2]         (leaf; initial state)
 *       │     └── IDLE [3]         (leaf)
 *       └── DONE [4]               (leaf)
 *
 * Events:
 *   stop:    sources = [ACTIVE]    target = DONE  (source expansion)
 *   reset:   sources = [DONE]      target = ACTIVE (target descent)
 *   pause:   sources = [RUN]       target = IDLE
 *
 * Guards: payload-truthy gate on `pause`. Plus a record_a / record_c_then_false
 * pair on a separate event proves short-circuit + ordering.
 */

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "state_machine.h"

/* States. */
enum { ST_ROOT = 0, ST_ACTIVE = 1, ST_RUN = 2, ST_IDLE = 3, ST_DONE = 4 };

static const state_machine_state_def_t hsm_states[] = {
    { .id = ST_ROOT,   .parent = STATE_MACHINE_ID_NONE,
      .initial_child = ST_ACTIVE, .depth = 0, .flags = 0, .name = "root" },
    { .id = ST_ACTIVE, .parent = ST_ROOT,
      .initial_child = ST_RUN,    .depth = 1, .flags = 0, .name = "active" },
    { .id = ST_RUN,    .parent = ST_ACTIVE,
      .initial_child = STATE_MACHINE_ID_NONE,
      .depth = 2, .flags = 0, .name = "run" },
    { .id = ST_IDLE,   .parent = ST_ACTIVE,
      .initial_child = STATE_MACHINE_ID_NONE,
      .depth = 2, .flags = 0, .name = "idle" },
    { .id = ST_DONE,   .parent = ST_ROOT,
      .initial_child = STATE_MACHINE_ID_NONE,
      .depth = 1, .flags = 0, .name = "done" },
};

/* Guards. */
static char log_buf[16];
static size_t log_len;

static void
log_reset(void) { log_len = 0; log_buf[0] = '\0'; }

static void
log_push(char c)
{
    if (log_len + 1 < sizeof(log_buf)) {
        log_buf[log_len++] = c;
        log_buf[log_len]   = '\0';
    }
}

static bool g_payload_truthy(const state_machine_t *m,
                             state_machine_event_id_t e, const void *p)
{ (void)m; (void)e; return p != NULL; }

static bool g_record_a(const state_machine_t *m,
                       state_machine_event_id_t e, const void *p)
{ (void)m; (void)e; (void)p; log_push('a'); return true; }

static bool g_record_c_false(const state_machine_t *m,
                             state_machine_event_id_t e, const void *p)
{ (void)m; (void)e; (void)p; log_push('c'); return false; }

static bool g_record_b(const state_machine_t *m,
                       state_machine_event_id_t e, const void *p)
{ (void)m; (void)e; (void)p; log_push('b'); return true; }

enum { GU_PAYLOAD_TRUTHY = 0, GU_RECORD_A = 1,
       GU_RECORD_C_FALSE = 2, GU_RECORD_B = 3 };

static const state_machine_guard_def_t hsm_guards[] = {
    { .fn = g_payload_truthy,  .name = "payload_truthy"      },
    { .fn = g_record_a,        .name = "record_a"            },
    { .fn = g_record_c_false,  .name = "record_c_then_false" },
    { .fn = g_record_b,        .name = "record_b"            },
};

/* Events. */
enum { EV_STOP = 0, EV_RESET = 1, EV_PAUSE = 2, EV_TRACE = 3 };

/* stop: sources = [ACTIVE], target = DONE. */
static const state_machine_state_id_t stop_src[]  = { ST_ACTIVE };
static const state_machine_transition_def_t stop_t[] = {
    { .sources = stop_src, .guards = NULL, .target = ST_DONE,
      .source_count = 1, .guard_count = 0, .ordinal = 0,
      .flags = 0, ._reserved = 0 },
};

/* reset: sources = [DONE], target = ACTIVE (descends to RUN). */
static const state_machine_state_id_t reset_src[] = { ST_DONE };
static const state_machine_transition_def_t reset_t[] = {
    { .sources = reset_src, .guards = NULL, .target = ST_ACTIVE,
      .source_count = 1, .guard_count = 0, .ordinal = 0,
      .flags = 0, ._reserved = 0 },
};

/* pause: sources = [RUN], target = IDLE. Guarded by payload_truthy. */
static const state_machine_state_id_t pause_src[]    = { ST_RUN };
static const state_machine_guard_id_t pause_guards[] = { GU_PAYLOAD_TRUTHY };
static const state_machine_transition_def_t pause_t[] = {
    { .sources = pause_src, .guards = pause_guards, .target = ST_IDLE,
      .source_count = 1, .guard_count = 1, .ordinal = 0,
      .flags = 0, ._reserved = 0 },
};

/* trace: source = ACTIVE, guards [a, c_false, b]. Target irrelevant; the
 * transition is meant to fail at guard `c`. b must NOT be invoked. */
static const state_machine_state_id_t trace_src[]    = { ST_ACTIVE };
static const state_machine_guard_id_t trace_guards[] = {
    GU_RECORD_A, GU_RECORD_C_FALSE, GU_RECORD_B
};
static const state_machine_transition_def_t trace_t[] = {
    { .sources = trace_src, .guards = trace_guards, .target = ST_DONE,
      .source_count = 1, .guard_count = 3, .ordinal = 0,
      .flags = 0, ._reserved = 0 },
};

static const state_machine_event_def_t hsm_events[] = {
    { .id = EV_STOP,  .transition_count = 1, .transitions = stop_t,  .name = "stop"  },
    { .id = EV_RESET, .transition_count = 1, .transitions = reset_t, .name = "reset" },
    { .id = EV_PAUSE, .transition_count = 1, .transitions = pause_t, .name = "pause" },
    { .id = EV_TRACE, .transition_count = 1, .transitions = trace_t, .name = "trace" },
};

static const state_machine_def_t hsm = {
    .magic        = STATE_MACHINE_MAGIC,
    .abi_epoch    = STATE_MACHINE_ABI_EPOCH,
    .abi_revision = STATE_MACHINE_ABI_REVISION,
    .struct_size  = sizeof(state_machine_def_t),
    .flags        = 0,
    .spec_version = STATE_MACHINE_SPEC_VERSION,
    ._reserved    = 0,
    .name         = "hsm",
    .states       = hsm_states,
    .events       = hsm_events,
    .guards       = hsm_guards,
    .state_count  = 5,
    .initial      = ST_RUN,
    .event_count  = 4,
    .guard_count  = 4,
};

int main(void) {
    state_machine_t m = STATE_MACHINE_INITIALIZER;
    state_machine_result_t r;
    int rc;
    int dummy = 1;

    rc = state_machine_init(&m, &hsm, NULL);
    assert(rc == 0);
    assert(state_machine_current(&m) == ST_RUN);

    /* is_in walks ancestors. RUN ⊂ ACTIVE ⊂ ROOT. */
    assert(state_machine_is_in(&m, ST_RUN));
    assert(state_machine_is_in(&m, ST_ACTIVE));
    assert(state_machine_is_in(&m, ST_ROOT));
    assert(!state_machine_is_in(&m, ST_IDLE));
    assert(!state_machine_is_in(&m, ST_DONE));

    /* Source expansion: stop has source ACTIVE, current=RUN matches. */
    rc = state_machine_dispatch_ex(&m, EV_STOP, NULL, &r);
    assert(rc == 0);
    assert(r.outcome == STATE_MACHINE_OUTCOME_ACCEPTED);
    assert(r.from == ST_RUN && r.to == ST_DONE);
    assert(state_machine_current(&m) == ST_DONE);

    /* Target descent: reset target=ACTIVE → descends to RUN. */
    rc = state_machine_dispatch_ex(&m, EV_RESET, NULL, &r);
    assert(rc == 0);
    assert(r.from == ST_DONE && r.to == ST_RUN);
    assert(state_machine_current(&m) == ST_RUN);

    /* GUARD_FAILED with payload=NULL. */
    rc = state_machine_dispatch_ex(&m, EV_PAUSE, NULL, &r);
    assert(rc == EOPNOTSUPP);
    assert(r.outcome == STATE_MACHINE_OUTCOME_GUARD_FAILED);
    assert(r.transition_index == 0);
    assert(r.guard_ordinal == 0);
    assert(r.guard_id == GU_PAYLOAD_TRUTHY);
    assert(state_machine_current(&m) == ST_RUN);

    /* ACCEPTED with non-NULL payload. */
    rc = state_machine_dispatch_ex(&m, EV_PAUSE, &dummy, &r);
    assert(rc == 0);
    assert(state_machine_current(&m) == ST_IDLE);

    /* Guard ordering + short-circuit. From IDLE, source=ACTIVE matches.
     * Expected log: "ac"; b must NOT fire. Guard ordinal 1, id 2. */
    log_reset();
    rc = state_machine_dispatch_ex(&m, EV_TRACE, NULL, &r);
    assert(rc == EOPNOTSUPP);
    assert(r.outcome == STATE_MACHINE_OUTCOME_GUARD_FAILED);
    assert(r.guard_ordinal == 1);
    assert(r.guard_id == GU_RECORD_C_FALSE);
    assert(strcmp(log_buf, "ac") == 0);

    /* Same-state self-transition: stop from DONE first requires getting
     * to DONE again; verify generation increment behaviour explicitly. */
    rc = state_machine_dispatch(&m, EV_RESET, NULL);
    assert(rc == EOPNOTSUPP); /* IDLE not in DONE expansion */
    /* Land in DONE then verify reset descends to RUN with gen++. */
    /* (Already tested above; here we verify generation strictly grows.) */
    uint64_t g0 = state_machine_generation(&m);
    rc = state_machine_dispatch(&m, EV_STOP, NULL); /* IDLE ⊂ ACTIVE */
    assert(rc == 0);
    assert(state_machine_generation(&m) == g0 + 1);

    state_machine_destroy(&m);
    printf("smoke_hsm: OK\n");
    return 0;
}
