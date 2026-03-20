#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "fixture.h"
#include "guards.h"
#include "jp.h"

void
ptr_pool_track(ptr_pool_t *pp, void *p)
{
    if (pp->count == pp->cap) {
        size_t nc = pp->cap ? pp->cap * 2 : 16;
        void **ni = realloc(pp->items, nc * sizeof(*ni));
        if (ni == NULL) { free(p); return; }
        pp->items = ni; pp->cap = nc;
    }
    pp->items[pp->count++] = p;
}

void
ptr_pool_free(ptr_pool_t *pp)
{
    size_t i;
    if (pp->items == NULL) return;
    for (i = 0; i < pp->count; i++)
        free(pp->items[i]);
    free(pp->items);
}

#define FREE_JAGGED(arr_, count_) do {                      \
    size_t fj_i_;                                           \
    if (arr_) {                                             \
        for (fj_i_ = 0; fj_i_ < (count_); fj_i_++)        \
            free((arr_)[fj_i_]);                            \
        free(arr_);                                         \
        (arr_) = NULL;                                      \
    }                                                       \
} while (0)

void
fixture_destroy(fixture_t *fx)
{
    size_t i;

    free(fx->fixture_name);
    free(fx->spec_version);

    FREE_JAGGED(fx->per_event_transitions, fx->def.event_count);
    FREE_JAGGED(fx->guard_names, fx->def.guard_count);

    free(fx->states);
    free(fx->events);
    free(fx->guards);

    ptr_pool_free(&fx->string_pool);

    if (fx->trace) {
        for (i = 0; i < fx->trace_count; i++)
            free(fx->trace[i].expect_log);
        free(fx->trace);
    }
}

static state_machine_state_id_t
id_or_none(jp_t *j)
{
    if (jp_at_null(j))
        return STATE_MACHINE_ID_NONE;
    return (state_machine_state_id_t)jp_uint(j);
}

static bool
parse_states(jp_t *j, build_ctx_t *bc)
{
    fixture_t *fx = bc->fx;
    size_t cap = 8, n = 0;
    state_machine_state_def_t *arr = malloc(cap * sizeof(*arr));

    JP_ARR_BEGIN(j, arr, done)
        state_machine_state_def_t s = {0};
        if (!jp_consume(j, '{')) goto fail;
        JP_OBJ_EACH(j, k, fail)
            if (strcmp(k, "id") == 0) {
                s.id = (state_machine_state_id_t)jp_uint(j);
            } else if (strcmp(k, "parent") == 0) {
                s.parent = id_or_none(j);
            } else if (strcmp(k, "initial_child") == 0) {
                s.initial_child = id_or_none(j);
            } else if (strcmp(k, "depth") == 0) {
                s.depth = (uint8_t)jp_uint(j);
            } else if (strcmp(k, "name") == 0) {
                char *nm = jp_string_dup(j);
                ptr_pool_track(&fx->string_pool, nm);
                s.name = nm;
            } else {
                jp_skip_value(j);
            }
        JP_OBJ_DONE(j, k, fail)
        JP_ARR_GROW(j, arr, n, cap);
        arr[n++] = s;
    JP_ARR_DONE(j, fail)
done:
    fx->states = arr;
    fx->def.states = arr;
    fx->def.state_count = (uint16_t)n;
    return true;
fail:
    free(arr);
    return false;
}

static bool
parse_guard_names(jp_t *j, build_ctx_t *bc)
{
    fixture_t *fx = bc->fx;
    size_t cap = 8, n = 0;
    char **arr = malloc(cap * sizeof(*arr));
    state_machine_guard_def_t *gd;

    JP_ARR_BEGIN(j, arr, done)
        char *name = jp_string_dup(j);
        if (!name) goto fail;
        if (n == cap) {
            char **na;
            cap *= 2;
            na = realloc(arr, cap * sizeof(*arr));
            if (na == NULL) { jp_fail(j, "oom"); free(name); goto fail; }
            arr = na;
        }
        arr[n++] = name;
    JP_ARR_DONE(j, fail)
done:
    fx->guard_names = arr;
    fx->def.guard_count = (uint16_t)n;
    if (n == 0) {
        fx->def.guards = NULL;
        return true;
    }
    gd = malloc(n * sizeof(*gd));
    if (gd == NULL) { jp_fail(j, "oom"); return false; }
    for (size_t i = 0; i < n; i++) {
        state_machine_guard_fn fn = resolve_canonical_guard(arr[i]);
        if (fn == NULL) {
            jp_fail(j, "unknown canonical guard \"%s\"", arr[i]);
            free(gd);
            return false;
        }
        gd[i].fn = fn;
        gd[i].name = arr[i];
    }
    fx->guards = gd;
    fx->def.guards = gd;
    return true;
fail:
    if (arr) {
        for (size_t i = 0; i < n; i++) free(arr[i]);
        free(arr);
    }
    return false;
}

static state_machine_guard_id_t
resolve_guard_index(jp_t *j, fixture_t *fx, const char *name)
{
    uint16_t i;
    for (i = 0; i < fx->def.guard_count; i++) {
        if (strcmp(fx->guard_names[i], name) == 0)
            return i;
    }
    jp_fail(j, "guard name \"%s\" not in machine.guards", name);
    return STATE_MACHINE_ID_NONE;
}

static bool
parse_transitions(jp_t *j, build_ctx_t *bc,
                  state_machine_transition_def_t **out_arr,
                  uint16_t *out_count)
{
    fixture_t *fx = bc->fx;
    size_t cap = 4, n = 0;
    state_machine_transition_def_t *arr = malloc(cap * sizeof(*arr));

    JP_ARR_BEGIN(j, arr, done)
        state_machine_transition_def_t t = {0};
        if (!jp_consume(j, '{')) goto fail;
        JP_OBJ_EACH(j, k, fail)
            if (strcmp(k, "ordinal") == 0) {
                t.ordinal = (uint16_t)jp_uint(j);
            } else if (strcmp(k, "target") == 0) {
                t.target = (state_machine_state_id_t)jp_uint(j);
            } else if (strcmp(k, "sources") == 0) {
                size_t scap = 4, sn = 0;
                state_machine_state_id_t *src = malloc(scap * sizeof(*src));
                if (!src) { jp_fail(j, "oom"); free(k); goto fail; }
                if (!jp_consume(j, '[')) { free(src); free(k); goto fail; }
                if (jp_peek(j, ']')) { j->p++; }
                else {
                    for (;;) {
                        if (sn == scap) {
                            state_machine_state_id_t *ns;
                            scap *= 2;
                            ns = realloc(src, scap * sizeof(*src));
                            if (!ns) { jp_fail(j, "oom"); free(src); free(k); goto fail; }
                            src = ns;
                        }
                        src[sn++] = (state_machine_state_id_t)jp_uint(j);
                        if (jp_peek(j, ',')) { j->p++; continue; }
                        if (!jp_consume(j, ']')) { free(src); free(k); goto fail; }
                        break;
                    }
                }
                ptr_pool_track(&bc->pool, src);
                t.sources = src;
                t.source_count = (uint16_t)sn;
            } else if (strcmp(k, "guards") == 0) {
                size_t gcap = 4, gn = 0;
                state_machine_guard_id_t *gids = malloc(gcap * sizeof(*gids));
                if (!gids) { jp_fail(j, "oom"); free(k); goto fail; }
                if (!jp_consume(j, '[')) { free(gids); free(k); goto fail; }
                if (jp_peek(j, ']')) { j->p++; }
                else {
                    for (;;) {
                        char *gn_name = jp_string_dup(j);
                        if (!gn_name) { free(gids); free(k); goto fail; }
                        if (gn == gcap) {
                            state_machine_guard_id_t *ng;
                            gcap *= 2;
                            ng = realloc(gids, gcap * sizeof(*gids));
                            if (!ng) { jp_fail(j, "oom"); free(gn_name); free(gids); free(k); goto fail; }
                            gids = ng;
                        }
                        gids[gn++] = resolve_guard_index(j, fx, gn_name);
                        free(gn_name);
                        if (j->failed) { free(gids); free(k); goto fail; }
                        if (jp_peek(j, ',')) { j->p++; continue; }
                        if (!jp_consume(j, ']')) { free(gids); free(k); goto fail; }
                        break;
                    }
                }
                if (gn == 0) {
                    free(gids);
                    t.guards = NULL;
                } else {
                    ptr_pool_track(&bc->pool, gids);
                    t.guards = gids;
                }
                t.guard_count = (uint16_t)gn;
            } else {
                jp_skip_value(j);
            }
        JP_OBJ_DONE(j, k, fail)
        JP_ARR_GROW(j, arr, n, cap);
        arr[n++] = t;
    JP_ARR_DONE(j, fail)
done:
    *out_arr = arr;
    *out_count = (uint16_t)n;
    return true;
fail:
    free(arr);
    return false;
}

static bool
parse_events(jp_t *j, build_ctx_t *bc)
{
    fixture_t *fx = bc->fx;
    size_t cap = 8, n = 0;
    state_machine_event_def_t *arr = malloc(cap * sizeof(*arr));

    JP_ARR_BEGIN(j, arr, done)
        state_machine_event_def_t ev = {0};
        state_machine_transition_def_t *trans = NULL;
        uint16_t trans_count = 0;

        if (!jp_consume(j, '{')) goto fail;
        JP_OBJ_EACH(j, k, fail)
            if (strcmp(k, "id") == 0) {
                ev.id = (state_machine_event_id_t)jp_uint(j);
            } else if (strcmp(k, "name") == 0) {
                char *nm = jp_string_dup(j);
                ptr_pool_track(&fx->string_pool, nm);
                ev.name = nm;
            } else if (strcmp(k, "transitions") == 0) {
                if (!parse_transitions(j, bc, &trans, &trans_count)) {
                    free(k); goto fail;
                }
            } else {
                jp_skip_value(j);
            }
        JP_OBJ_DONE(j, k, fail)
        ev.transitions = trans;
        ev.transition_count = trans_count;
        ptr_pool_track(&bc->pool, trans);
        JP_ARR_GROW(j, arr, n, cap);
        arr[n++] = ev;
    JP_ARR_DONE(j, fail)
done:
    fx->events = arr;
    fx->def.events = arr;
    fx->def.event_count = (uint16_t)n;
    return true;
fail:
    free(arr);
    return false;
}

static bool
parse_machine(jp_t *j, build_ctx_t *bc)
{
    fixture_t *fx = bc->fx;
    const char *machine_start;

    fx->def.magic        = STATE_MACHINE_MAGIC;
    fx->def.abi_epoch    = STATE_MACHINE_ABI_EPOCH;
    fx->def.abi_revision = STATE_MACHINE_ABI_REVISION;
    fx->def.struct_size  = sizeof(state_machine_def_t);
    fx->def.flags        = 0;
    fx->def.spec_version = STATE_MACHINE_SPEC_VERSION;
    fx->def._reserved    = 0;

    if (!jp_consume(j, '{')) return false;
    machine_start = j->p;

    /* Pass 1: parse "guards" so transitions can resolve guard names
     * regardless of JSON key ordering. */
    JP_OBJ_EACH_R(j, k)
        if (strcmp(k, "guards") == 0) {
            if (!parse_guard_names(j, bc)) { free(k); return false; }
        } else {
            jp_skip_value(j);
        }
    JP_OBJ_DONE_R(j, k)

    /* Pass 2: parse everything else. */
    j->p = machine_start;
    JP_OBJ_EACH_R(j, k)
        if (strcmp(k, "states") == 0) {
            if (!parse_states(j, bc)) { free(k); return false; }
        } else if (strcmp(k, "events") == 0) {
            if (!parse_events(j, bc)) { free(k); return false; }
        } else if (strcmp(k, "guards") == 0) {
            jp_skip_value(j);
        } else if (strcmp(k, "initial") == 0) {
            fx->def.initial = (state_machine_state_id_t)jp_uint(j);
        } else if (strcmp(k, "name") == 0) {
            char *nm = jp_string_dup(j);
            ptr_pool_track(&fx->string_pool, nm);
            fx->def.name = nm;
        } else {
            jp_skip_value(j);
        }
    JP_OBJ_DONE_R(j, k)
    return true;
}

static int
parse_result_field(jp_t *j)
{
    if (j->failed) return -1;
    if (jp_peek(j, '"')) {
        char *s = jp_string_dup(j);
        int rc = -1;
        if (!s) return -1;
        if      (strcmp(s, "0") == 0)          rc = 0;
        else if (strcmp(s, "EINVAL") == 0)     rc = EINVAL;
        else if (strcmp(s, "EPROTO") == 0)     rc = EPROTO;
        else if (strcmp(s, "EOPNOTSUPP") == 0) rc = EOPNOTSUPP;
        else if (strcmp(s, "EBUSY") == 0)      rc = EBUSY;
        else jp_fail(j, "unknown result name \"%s\"", s);
        free(s);
        return rc;
    }
    return (int)jp_int(j);
}

static state_machine_outcome_t
parse_outcome_field(jp_t *j)
{
    char *s = jp_string_dup(j);
    state_machine_outcome_t o = STATE_MACHINE_OUTCOME_ACCEPTED;
    if (!s) return o;
    if      (strcmp(s, "ACCEPTED") == 0)       o = STATE_MACHINE_OUTCOME_ACCEPTED;
    else if (strcmp(s, "INVALID_ARG") == 0)    o = STATE_MACHINE_OUTCOME_INVALID_ARG;
    else if (strcmp(s, "NO_TRANSITION") == 0)  o = STATE_MACHINE_OUTCOME_NO_TRANSITION;
    else if (strcmp(s, "GUARD_FAILED") == 0)   o = STATE_MACHINE_OUTCOME_GUARD_FAILED;
    else if (strcmp(s, "REENTRY") == 0)        o = STATE_MACHINE_OUTCOME_REENTRY;
    else jp_fail(j, "unknown outcome \"%s\"", s);
    free(s);
    return o;
}

static bool
parse_dispatch(jp_t *j, trace_step_t *step)
{
    if (!jp_consume(j, '{')) return false;
    JP_OBJ_EACH_R(j, k)
        if (strcmp(k, "event") == 0) {
            step->event = (state_machine_event_id_t)jp_uint(j);
        } else if (strcmp(k, "payload") == 0) {
            if (jp_at_null(j)) {
                step->has_payload = false;
            } else {
                step->has_payload = true;
                step->payload_byte = (uint8_t)jp_uint(j);
            }
        } else {
            jp_skip_value(j);
        }
    JP_OBJ_DONE_R(j, k)
    return true;
}

static bool
parse_expect(jp_t *j, trace_step_t *step)
{
    step->expect_result            = 0;
    step->expect_outcome           = STATE_MACHINE_OUTCOME_ACCEPTED;
    step->expect_current           = 0;
    step->expect_generation        = 0;
    step->expect_transition_index  = UINT16_MAX;
    step->expect_guard_ordinal     = UINT16_MAX;
    step->expect_guard_id          = STATE_MACHINE_ID_NONE;
    step->expect_transitions_tested = 0;
    step->expect_log               = NULL;

    if (!jp_consume(j, '{')) return false;
    JP_OBJ_EACH_R(j, k)
        if      (strcmp(k, "result") == 0)             step->expect_result = parse_result_field(j);
        else if (strcmp(k, "outcome") == 0)            step->expect_outcome = parse_outcome_field(j);
        else if (strcmp(k, "current") == 0)            step->expect_current = (state_machine_state_id_t)jp_uint(j);
        else if (strcmp(k, "generation") == 0)         step->expect_generation = jp_uint(j);
        else if (strcmp(k, "transition_index") == 0)   step->expect_transition_index = (uint16_t)jp_uint(j);
        else if (strcmp(k, "guard_ordinal") == 0)      step->expect_guard_ordinal = (uint16_t)jp_uint(j);
        else if (strcmp(k, "guard_id") == 0)           step->expect_guard_id = (state_machine_guard_id_t)jp_uint(j);
        else if (strcmp(k, "transitions_tested") == 0) step->expect_transitions_tested = (uint16_t)jp_uint(j);
        else if (strcmp(k, "log") == 0)                step->expect_log = jp_string_dup(j);
        else                                           jp_skip_value(j);
    JP_OBJ_DONE_R(j, k)
    return true;
}

static bool
parse_trace(jp_t *j, fixture_t *fx)
{
    size_t cap = 8, n = 0;
    trace_step_t *arr = calloc(cap, sizeof(*arr));

    JP_ARR_BEGIN(j, arr, done)
        trace_step_t step = {0};
        if (!jp_consume(j, '{')) goto fail;
        JP_OBJ_EACH(j, k, fail)
            if (strcmp(k, "dispatch") == 0) {
                if (!parse_dispatch(j, &step)) { free(k); goto fail; }
            } else if (strcmp(k, "expect") == 0) {
                if (!parse_expect(j, &step)) { free(k); goto fail; }
            } else if (strcmp(k, "step") == 0) {
                step.step_num = (int)jp_int(j);
            } else {
                jp_skip_value(j);
            }
        JP_OBJ_DONE(j, k, fail)
        JP_ARR_GROW(j, arr, n, cap);
        arr[n++] = step;
    JP_ARR_DONE(j, fail)
done:
    fx->trace = arr;
    fx->trace_count = n;
    return true;
fail:
    if (arr) {
        for (size_t i = 0; i < n; i++) free(arr[i].expect_log);
        free(arr);
    }
    return false;
}

bool
parse_fixture(jp_t *j, build_ctx_t *bc)
{
    fixture_t *fx = bc->fx;

    if (!jp_consume(j, '{')) return false;
    JP_OBJ_EACH_R(j, k)
        if (strcmp(k, "machine") == 0) {
            if (!parse_machine(j, bc)) { free(k); return false; }
        } else if (strcmp(k, "trace") == 0) {
            if (!parse_trace(j, fx)) { free(k); return false; }
        } else if (strcmp(k, "name") == 0) {
            fx->fixture_name = jp_string_dup(j);
        } else if (strcmp(k, "spec_version") == 0) {
            fx->spec_version = jp_string_dup(j);
        } else {
            jp_skip_value(j);
        }
    JP_OBJ_DONE_R(j, k)
    return true;
}
