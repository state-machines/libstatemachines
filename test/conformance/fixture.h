#ifndef FIXTURE_H
#define FIXTURE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "state_machine.h"
#include "jp.h"

typedef struct {
    void **items;
    size_t count, cap;
} ptr_pool_t;

void ptr_pool_track(ptr_pool_t *pp, void *p);
void ptr_pool_free(ptr_pool_t *pp);

typedef struct {
    bool                       has_payload;
    uint8_t                    payload_byte;
    state_machine_event_id_t   event;

    int                        expect_result;
    state_machine_outcome_t    expect_outcome;
    state_machine_state_id_t   expect_current;
    uint64_t                   expect_generation;
    uint16_t                   expect_transition_index;
    uint16_t                   expect_guard_ordinal;
    state_machine_guard_id_t   expect_guard_id;
    uint16_t                   expect_transitions_tested;

    char                      *expect_log;
    int                        step_num;
} trace_step_t;

typedef struct {
    char                            *fixture_name;
    char                            *spec_version;
    state_machine_state_def_t       *states;
    state_machine_event_def_t       *events;
    state_machine_guard_def_t       *guards;
    state_machine_transition_def_t **per_event_transitions;
    state_machine_state_id_t       **per_transition_sources;
    state_machine_guard_id_t       **per_transition_guards;
    char                           **guard_names;
    ptr_pool_t                       string_pool;
    state_machine_def_t              def;
    trace_step_t                    *trace;
    size_t                           trace_count;
} fixture_t;

typedef struct {
    fixture_t  *fx;
    ptr_pool_t  pool;
} build_ctx_t;

void fixture_destroy(fixture_t *fx);
bool parse_fixture(jp_t *j, build_ctx_t *bc);

#endif /* FIXTURE_H */
