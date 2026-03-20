/*
 * state_machine.c - Runtime implementation.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Provides the runtime half of the public API: schema validation,
 * transition selection and application, same-context reentry detection,
 * and dispatch diagnostics. Error conditions are reported as positive
 * errno values.
 */

#include "state_machine.h"

/* Restore *m to the zero state without depending on libc memset, so the
 * runtime stays usable in freestanding environments. */
static void
sm_zero_machine(state_machine_t *m)
{
    m->def            = NULL;
    m->userdata       = NULL;
    m->generation     = 0;
    m->current        = 0;
    m->_dispatch_flag = 0;
    m->_reserved[0]   = 0;
    m->_reserved[1]   = 0;
}

/* Walk the parent chain from `s` to root, bounded by MAX_DEPTH.
 * Writes the chain length to *out_depth; returns false on cycle or
 * out-of-range parent reference. */
static bool
sm_walk_parents(const state_machine_def_t *def,
                state_machine_state_id_t s,
                uint8_t *out_depth)
{
    state_machine_state_id_t cur = s;
    unsigned d = 0;

    while (def->states[cur].parent != STATE_MACHINE_ID_NONE) {
        if (d >= STATE_MACHINE_MAX_DEPTH)
            return false;
        cur = def->states[cur].parent;
        if (cur >= def->state_count)
            return false;
        d++;
    }
    *out_depth = (uint8_t)d;
    return true;
}

/* Return true if `desc` is `anc` or is an ancestor-descendant of `anc`.
 * Walk bounded by MAX_DEPTH (defensive cap; validation precludes cycles). */
static bool
sm_is_descendant(const state_machine_def_t *def,
                 state_machine_state_id_t desc,
                 state_machine_state_id_t anc)
{
    state_machine_state_id_t cur = desc;
    unsigned d;

    for (d = 0; d <= STATE_MACHINE_MAX_DEPTH; d++) {
        if (cur == anc)
            return true;
        if (def->states[cur].parent == STATE_MACHINE_ID_NONE)
            return false;
        cur = def->states[cur].parent;
    }
    return false;
}

/* Verify every .fn in a guard or action table is non-NULL.
 * May `return EPROTO`; only for use inside sm_validate_def. */
#define SM_VALIDATE_FN_TABLE(count_, arr_) do {                       \
    uint16_t sm_fi_;                                                   \
    if ((count_) > 0) {                                                \
        if ((arr_) == NULL) return EPROTO;                             \
        for (sm_fi_ = 0; sm_fi_ < (uint16_t)(count_); sm_fi_++)       \
            if ((arr_)[sm_fi_].fn == NULL) return EPROTO;              \
    }                                                                  \
} while (0)

/* Verify every element of an id-array is < limit.
 * May `return EPROTO`; only for use inside sm_validate_def. */
#define SM_CHECK_IDS_IN_RANGE(arr_, count_, limit_) do {              \
    uint16_t sm_ii_;                                                   \
    for (sm_ii_ = 0; sm_ii_ < (uint16_t)(count_); sm_ii_++)           \
        if ((arr_)[sm_ii_] >= (limit_)) return EPROTO;                 \
} while (0)

/* Full schema validation. Linear in schema size. */
static int
sm_validate_def(const state_machine_def_t *def)
{
    uint16_t i, j;

    if (def == NULL)
        return EINVAL;

    if (def->magic != STATE_MACHINE_MAGIC)
        return EPROTO;
    if (def->abi_epoch != STATE_MACHINE_ABI_EPOCH)
        return EPROTO;
    if (def->struct_size != sizeof(state_machine_def_t))
        return EPROTO;

    if (def->flags != 0 || def->_reserved != 0)
        return EPROTO;

    if (def->state_count == 0 || def->states == NULL)
        return EPROTO;

    if (def->event_count > 0 && def->events == NULL)
        return EPROTO;

    SM_VALIDATE_FN_TABLE(def->guard_count,  def->guards);
    SM_VALIDATE_FN_TABLE(def->action_count, def->actions);

    for (i = 0; i < def->state_count; i++) {
        const state_machine_state_def_t *s = &def->states[i];
        uint8_t actual_depth;

        if (s->id != i)
            return EPROTO;
        if (s->flags != 0)
            return EPROTO;
        if (s->parent != STATE_MACHINE_ID_NONE && s->parent >= def->state_count)
            return EPROTO;
        if (s->initial_child != STATE_MACHINE_ID_NONE) {
            if (s->initial_child >= def->state_count)
                return EPROTO;
            if (def->states[s->initial_child].parent != i)
                return EPROTO;
        }
        if (!sm_walk_parents(def, i, &actual_depth))
            return EPROTO;
        if (s->depth != actual_depth)
            return EPROTO;
    }

    /* Every superstate must declare an initial_child. */
    for (j = 0; j < def->state_count; j++) {
        state_machine_state_id_t p = def->states[j].parent;
        if (p != STATE_MACHINE_ID_NONE) {
            if (def->states[p].initial_child == STATE_MACHINE_ID_NONE)
                return EPROTO;
        }
    }

    /* Initial state in range and is a leaf. */
    if (def->initial >= def->state_count)
        return EPROTO;
    for (j = 0; j < def->state_count; j++) {
        if (def->states[j].parent == def->initial)
            return EPROTO;
    }

    /* Events and transitions. */
    for (i = 0; i < def->event_count; i++) {
        const state_machine_event_def_t *ev = &def->events[i];
        uint16_t ti;

        if (ev->id != i)
            return EPROTO;
        if (ev->transitions == NULL || ev->transition_count == 0)
            return EPROTO;

        for (ti = 0; ti < ev->transition_count; ti++) {
            const state_machine_transition_def_t *t = &ev->transitions[ti];

            if (t->sources == NULL || t->source_count == 0)
                return EPROTO;
            SM_CHECK_IDS_IN_RANGE(t->sources, t->source_count, def->state_count);
            if (t->target >= def->state_count)
                return EPROTO;
            if ((t->flags & (uint8_t)~STATE_MACHINE_TRANSITION_F_ACTION) != 0)
                return EPROTO;
            if ((t->flags & STATE_MACHINE_TRANSITION_F_ACTION) != 0) {
                if (t->action >= def->action_count)
                    return EPROTO;
            } else if (t->action != 0) {
                return EPROTO;
            }
            if (t->guard_count > 0) {
                if (t->guards == NULL)
                    return EPROTO;
                SM_CHECK_IDS_IN_RANGE(t->guards, t->guard_count, def->guard_count);
            }
            if (t->ordinal != ti)
                return EPROTO;
            if (t->_reserved != 0)
                return EPROTO;
        }
    }

    /* initial_child chain reaches a leaf within MAX_DEPTH. */
    for (i = 0; i < def->state_count; i++) {
        state_machine_state_id_t cur = i;
        unsigned d = 0;

        while (def->states[cur].initial_child != STATE_MACHINE_ID_NONE) {
            if (d >= STATE_MACHINE_MAX_DEPTH)
                return EPROTO;
            cur = def->states[cur].initial_child;
            if (cur >= def->state_count)
                return EPROTO;
            d++;
        }
    }

    return 0;
}

/* Fill `out` with the canonical "rejected, no state change" diagnostic. */
static void
sm_fill_reject(state_machine_result_t *out,
               state_machine_outcome_t outcome,
               state_machine_state_id_t pre,
               uint64_t pre_gen,
               state_machine_event_id_t event,
               uint16_t transitions_tested)
{
    if (out == NULL)
        return;
    out->outcome            = outcome;
    out->from               = pre;
    out->to                 = pre;
    out->event              = event;
    out->transition_index   = UINT16_MAX;
    out->guard_ordinal      = UINT16_MAX;
    out->guard_id           = STATE_MACHINE_ID_NONE;
    out->action_id          = STATE_MACHINE_ID_NONE;
    out->transitions_tested = transitions_tested;
    out->generation_after   = pre_gen;
}

int
state_machine_init(state_machine_t *m,
                   const state_machine_def_t *def,
                   void *userdata)
{
    int err;

    if (m == NULL)
        return EINVAL;

    err = sm_validate_def(def);
    if (err != 0) {
        sm_zero_machine(m);
        return err;
    }

    m->def            = def;
    m->userdata       = userdata;
    m->generation     = 0;
    m->current        = def->initial;
    m->_dispatch_flag = 0;
    m->_reserved[0]   = 0;
    m->_reserved[1]   = 0;
    return 0;
}

void
state_machine_destroy(state_machine_t *m)
{
    if (m == NULL)
        return;
    sm_zero_machine(m);
}

state_machine_state_id_t
state_machine_current(const state_machine_t *m)
{
    return m->current;
}

uint64_t
state_machine_generation(const state_machine_t *m)
{
    return m->generation;
}

void *
state_machine_userdata(const state_machine_t *m)
{
    return m->userdata;
}

const state_machine_def_t *
state_machine_def(const state_machine_t *m)
{
    return m->def;
}

bool
state_machine_is_in(const state_machine_t *m, state_machine_state_id_t s)
{
    if (s == STATE_MACHINE_ID_NONE)
        return false;
    if (s >= m->def->state_count)
        return false;
    return sm_is_descendant(m->def, m->current, s);
}

#define SM_NAMED_ACCESSOR(d_, id_, count_f_, arr_f_) \
    ((d_) == NULL ? NULL : ((id_) >= (d_)->count_f_ ? NULL : (d_)->arr_f_[id_].name))

const char *
state_machine_state_name(const state_machine_def_t *d,
                         state_machine_state_id_t s)
{
    return SM_NAMED_ACCESSOR(d, s, state_count, states);
}

const char *
state_machine_event_name(const state_machine_def_t *d,
                         state_machine_event_id_t e)
{
    return SM_NAMED_ACCESSOR(d, e, event_count, events);
}

static int
sm_dispatch_core(state_machine_t *m,
                 state_machine_event_id_t event,
                 void *payload,
                 state_machine_result_t *out)
{
    const state_machine_def_t *def;
    const state_machine_event_def_t *ev;
    state_machine_state_id_t pre;
    uint64_t pre_gen;
    uint16_t tested = 0;
    uint16_t outcome_trans_idx = UINT16_MAX;
    uint16_t outcome_guard_ord = UINT16_MAX;
    state_machine_guard_id_t outcome_guard_id = STATE_MACHINE_ID_NONE;
    state_machine_action_id_t outcome_action_id = STATE_MACHINE_ID_NONE;
    state_machine_outcome_t outcome = STATE_MACHINE_OUTCOME_NO_TRANSITION;
    state_machine_state_id_t new_current;
    bool accepted = false;
    uint16_t ti;

    /* m == NULL leaves *out untouched. */
    if (m == NULL)
        return EINVAL;

    def     = m->def;
    pre     = m->current;
    pre_gen = m->generation;

    /* Bounds check before any deref of def->events. */
    if (event >= def->event_count) {
        sm_fill_reject(out, STATE_MACHINE_OUTCOME_INVALID_ARG,
                       pre, pre_gen, event, 0);
        return EINVAL;
    }

    /* Same-context reentry. */
    if (m->_dispatch_flag != 0) {
        sm_fill_reject(out, STATE_MACHINE_OUTCOME_REENTRY,
                       pre, pre_gen, event, 0);
        return EBUSY;
    }
    m->_dispatch_flag = 1;

    ev = &def->events[event];
    new_current = pre;

    for (ti = 0; ti < ev->transition_count; ti++) {
        const state_machine_transition_def_t *t = &ev->transitions[ti];
        bool source_matched = false;
        uint16_t si;

        tested++; /* Count every evaluated transition. */

        /* Source match (self or descendant). */
        for (si = 0; si < t->source_count; si++) {
            if (sm_is_descendant(def, pre, t->sources[si])) {
                source_matched = true;
                break;
            }
        }
        if (!source_matched)
            continue;

        /* Guards in declaration order, short-circuit on false. */
        {
            bool all_passed = true;
            uint16_t failing_ord = UINT16_MAX;
            state_machine_guard_id_t failing_id = STATE_MACHINE_ID_NONE;
            uint16_t gi;

            for (gi = 0; gi < t->guard_count; gi++) {
                state_machine_guard_id_t gid = t->guards[gi];
                const state_machine_guard_def_t *g = &def->guards[gid];

                if (!g->fn(m, event, payload)) {
                    all_passed = false;
                    failing_ord = gi;
                    failing_id = gid;
                    break;
                }
            }

            if (!all_passed) {
                if (outcome == STATE_MACHINE_OUTCOME_NO_TRANSITION) { /* First wins */
                    outcome           = STATE_MACHINE_OUTCOME_GUARD_FAILED;
                    outcome_trans_idx = ti;
                    outcome_guard_ord = failing_ord;
                    outcome_guard_id  = failing_id;
                }
                continue;
            }
        }

        /* Apply: target descent to leaf. */
        {
            state_machine_state_id_t t_leaf = t->target;
            unsigned d;

            for (d = 0; d <= STATE_MACHINE_MAX_DEPTH; d++) {
                if (def->states[t_leaf].initial_child == STATE_MACHINE_ID_NONE)
                    break;
                t_leaf = def->states[t_leaf].initial_child;
            }
            new_current = t_leaf;
        }

        outcome           = STATE_MACHINE_OUTCOME_ACCEPTED;
        outcome_trans_idx = ti;
        outcome_guard_ord = UINT16_MAX;
        outcome_guard_id  = STATE_MACHINE_ID_NONE;
        outcome_action_id = (t->flags & STATE_MACHINE_TRANSITION_F_ACTION) != 0
                          ? t->action
                          : STATE_MACHINE_ID_NONE;
        accepted          = true;
        break;
    }

    if (accepted) {
        m->current = new_current;
        m->generation += 1; /* Same-state transitions increment generation. */
        if (outcome_action_id != STATE_MACHINE_ID_NONE)
            def->actions[outcome_action_id].fn(m, event, payload);
    }

    m->_dispatch_flag = 0;

    if (out != NULL) {
        out->outcome            = outcome;
        out->from               = pre;
        out->to                 = m->current;
        out->event              = event;
        out->transition_index   = outcome_trans_idx;
        out->guard_ordinal      = outcome_guard_ord;
        out->guard_id           = outcome_guard_id;
        out->action_id          = outcome_action_id;
        out->transitions_tested = tested;
        out->generation_after   = m->generation;
    }

    return accepted ? 0 : EOPNOTSUPP;
}

int
state_machine_dispatch(state_machine_t *m,
                       state_machine_event_id_t event,
                       void *payload)
{
    return sm_dispatch_core(m, event, payload, NULL);
}

int
state_machine_dispatch_ex(state_machine_t *m,
                          state_machine_event_id_t event,
                          void *payload,
                          state_machine_result_t *out_result)
{
    return sm_dispatch_core(m, event, payload, out_result);
}

uint32_t
state_machine_abi_version(void)
{
    return ((uint32_t)STATE_MACHINE_ABI_EPOCH << 16)
         |  (uint32_t)STATE_MACHINE_ABI_REVISION;
}

const char *
state_machine_outcome_name(state_machine_outcome_t o)
{
    switch (o) {
    case STATE_MACHINE_OUTCOME_ACCEPTED:      return "ACCEPTED";
    case STATE_MACHINE_OUTCOME_INVALID_ARG:   return "INVALID_ARG";
    case STATE_MACHINE_OUTCOME_NO_TRANSITION: return "NO_TRANSITION";
    case STATE_MACHINE_OUTCOME_GUARD_FAILED:  return "GUARD_FAILED";
    case STATE_MACHINE_OUTCOME_REENTRY:       return "REENTRY";
    }
    return NULL;
}
