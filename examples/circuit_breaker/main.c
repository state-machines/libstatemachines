/*
 * main.c -- Demo driver for the circuit-breaker reference consumer.
 *
 * Walks a deterministic timeline that exercises every accept and
 * reject path, then asserts the visible state. Intended to be run
 * from the project root via `make example`.
 */

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>

#include "circuit_breaker.h"

static const char *
cb_state_name(cb_state_id_t s)
{
    switch (s) {
    case CB_CLOSED:    return "closed";
    case CB_OPEN:      return "open";
    case CB_HALF_OPEN: return "half_open";
    }
    return "?";
}

int main(void)
{
    cb_t cb;
    int  rc;

    rc = cb_init(&cb, /*failure_threshold=*/3, /*timeout_ms=*/1000);
    assert(rc == 0);
    assert(cb_current(&cb) == CB_CLOSED);
    printf("init                          -> %s\n", cb_state_name(cb_current(&cb)));

    /* Two failures: stays closed; counter increments. */
    rc = cb_record_failure(&cb, 100); assert(rc == 0);
    assert(cb_current(&cb) == CB_CLOSED && cb.failure_count == 1);
    rc = cb_record_failure(&cb, 200); assert(rc == 0);
    assert(cb_current(&cb) == CB_CLOSED && cb.failure_count == 2);

    /* A success resets the counter. */
    rc = cb_record_success(&cb, 250); assert(rc == 0);
    assert(cb_current(&cb) == CB_CLOSED && cb.failure_count == 0);
    printf("two failures + success        -> %s, fc=%u\n",
           cb_state_name(cb_current(&cb)), cb.failure_count);

    /* Three failures in a row trip the breaker on the third. */
    rc = cb_record_failure(&cb, 300); assert(rc == 0);
    rc = cb_record_failure(&cb, 400); assert(rc == 0);
    assert(cb_current(&cb) == CB_CLOSED && cb.failure_count == 2);
    rc = cb_record_failure(&cb, 500); assert(rc == 0);
    assert(cb_current(&cb) == CB_OPEN);
    assert(cb.opened_at_ms == 500);
    printf("threshold trip                -> %s @ t=%llu\n",
           cb_state_name(cb_current(&cb)),
           (unsigned long long)cb.opened_at_ms);

    /* Tick before timeout: GUARD_FAILED, stays OPEN. */
    {
        state_machine_result_t r;
        cb.now_ms = 999;
        rc = state_machine_dispatch_ex(&cb.machine, CB_EV_TICK, NULL, &r);
        assert(rc == EOPNOTSUPP);
        assert(r.outcome == STATE_MACHINE_OUTCOME_GUARD_FAILED);
        assert(r.guard_id == 1 /* GU_TIMEOUT_ELAPSED */);
        assert(cb_current(&cb) == CB_OPEN);
        printf("tick before timeout           -> %s (outcome=%s)\n",
               cb_state_name(cb_current(&cb)),
               state_machine_outcome_name(r.outcome));
    }

    /* Tick after timeout elapses: half-opens. */
    rc = cb_tick(&cb, 1500);
    assert(rc == 0);
    assert(cb_current(&cb) == CB_HALF_OPEN);
    printf("tick after timeout            -> %s\n", cb_state_name(cb_current(&cb)));

    /* Probe failure in HALF_OPEN: re-trips. */
    rc = cb_record_failure(&cb, 1600);
    assert(rc == 0);
    assert(cb_current(&cb) == CB_OPEN);
    assert(cb.opened_at_ms == 1600);
    printf("half-open probe failure       -> %s @ t=%llu\n",
           cb_state_name(cb_current(&cb)),
           (unsigned long long)cb.opened_at_ms);

    /* Recover: timeout -> half-open -> success -> closed. */
    rc = cb_tick(&cb, 2700); assert(rc == 0);
    assert(cb_current(&cb) == CB_HALF_OPEN);
    rc = cb_record_success(&cb, 2750); assert(rc == 0);
    assert(cb_current(&cb) == CB_CLOSED);
    assert(cb.failure_count == 0);
    printf("recover via half-open success -> %s\n", cb_state_name(cb_current(&cb)));

    /* No-transition: success while OPEN is rejected. */
    rc = cb_record_failure(&cb, 2800); /* count=1 */
    rc = cb_record_failure(&cb, 2900); /* count=2 */
    rc = cb_record_failure(&cb, 3000); /* trips */
    assert(cb_current(&cb) == CB_OPEN);
    {
        state_machine_result_t r;
        cb.now_ms = 3050;
        rc = state_machine_dispatch_ex(&cb.machine, CB_EV_SUCCESS, NULL, &r);
        assert(rc == EOPNOTSUPP);
        assert(r.outcome == STATE_MACHINE_OUTCOME_NO_TRANSITION);
        assert(cb_current(&cb) == CB_OPEN);
        printf("success while open            -> %s (outcome=%s)\n",
               cb_state_name(cb_current(&cb)),
               state_machine_outcome_name(r.outcome));
    }

    state_machine_destroy(&cb.machine);
    printf("demo: OK\n");
    return 0;
}
