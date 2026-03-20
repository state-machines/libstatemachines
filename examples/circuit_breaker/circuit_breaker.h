/*
 * circuit_breaker.h -- v0 reference consumer for libstatemachines.
 *
 * A circuit breaker is the contained state machine that
 * proves the library compiles, dispatches, and surfaces diagnostics.
 *
 * Three states (CLOSED → OPEN → HALF_OPEN → CLOSED), four events, two
 * guards. The schema is static .rodata; the cb_t wrapper struct embeds
 * state_machine_t and owns the failure-count / opened-at-time scalars
 * that the guards read via userdata.
 *
 * Time is injected by the caller before each event (`now_ms` field),
 * because the library is freestanding-clean and may not call time
 * syscalls.
 */

#ifndef CIRCUIT_BREAKER_H
#define CIRCUIT_BREAKER_H

#include "state_machine.h"

/* State IDs (also indices into the schema's states[] array). */
typedef enum {
    CB_CLOSED    = 0,
    CB_OPEN      = 1,
    CB_HALF_OPEN = 2
} cb_state_id_t;

/* Event IDs. */
typedef enum {
    CB_EV_SUCCESS = 0,
    CB_EV_FAILURE = 1,
    CB_EV_TICK    = 2
} cb_event_id_t;

/* Wrapper instance. Embeds state_machine_t; demonstrates the design
 * intent of caller-owned runtime storage. */
typedef struct {
    state_machine_t machine;

    /* Policy (set once at cb_init). */
    uint32_t failure_threshold;
    uint64_t timeout_ms;

    /* Mutable counters; guards read these via userdata. */
    uint32_t failure_count;
    uint64_t opened_at_ms;
    uint64_t now_ms;
} cb_t;

/* Lifecycle. Returns 0 / EINVAL / EPROTO from state_machine_init. */
int cb_init(cb_t *cb, uint32_t failure_threshold, uint64_t timeout_ms);

/* Each call sets cb->now_ms then dispatches. Returns the dispatch
 * errno (0 / EOPNOTSUPP) - same value as state_machine_dispatch.
 * On accept, the wrapper updates failure_count / opened_at_ms. */
int cb_record_success(cb_t *cb, uint64_t now_ms);
int cb_record_failure(cb_t *cb, uint64_t now_ms);
int cb_tick          (cb_t *cb, uint64_t now_ms);

/* Convenience: current leaf state ID. */
cb_state_id_t cb_current(const cb_t *cb);

/* Schema accessor (exposed for diagnostics). */
const state_machine_def_t *cb_schema(void);

#endif /* CIRCUIT_BREAKER_H */
