/*
 * smoke_invalid.c -- Invalid-schema smoke test.
 *
 * For each validation violation class, build a deliberately broken schema
 * and assert that state_machine_init returns EPROTO (or EINVAL where
 * appropriate). Demonstrates the "*m reset to zero on failure" contract.
 */

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>

#include "state_machine.h"

#define BASE_DEF_FIELDS                                       \
    .magic        = STATE_MACHINE_MAGIC,                       \
    .abi_epoch    = STATE_MACHINE_ABI_EPOCH,                   \
    .abi_revision = STATE_MACHINE_ABI_REVISION,                \
    .struct_size  = sizeof(state_machine_def_t),               \
    .flags        = 0,                                         \
    .spec_version = STATE_MACHINE_SPEC_VERSION,                \
    ._reserved    = 0

/* Build a one-leaf def, apply mutate_, assert EPROTO. */
#define ASSERT_ONE_LEAF_BREAKS(mutate_) do {                  \
    state_machine_def_t d = {                                  \
        BASE_DEF_FIELDS,                                       \
        .name = "x", .states = one_leaf,                      \
        .state_count = 1, .initial = 0,                       \
        .event_count = 0, .guard_count = 0,                   \
    };                                                         \
    (mutate_);                                                 \
    assert(try_init(&d) == EPROTO);                            \
} while (0)

/* Build a single-transition def with one_leaf, assert EPROTO. */
#define ASSERT_TRANS_BREAKS(action_, flags_, action_count_) do {      \
    const state_machine_state_id_t at_src_[] = { 0 };                 \
    const state_machine_transition_def_t at_t_[] = {                  \
        { .sources = at_src_, .guards = NULL, .target = 0,            \
          .source_count = 1, .guard_count = 0,                        \
          .action = (action_), .ordinal = 0,                          \
          .flags = (flags_), ._reserved = 0 },                        \
    };                                                                 \
    const state_machine_event_def_t at_ev_[] = {                      \
        { .id = 0, .transition_count = 1,                             \
          .transitions = at_t_, .name = "go" },                       \
    };                                                                 \
    state_machine_def_t d = {                                          \
        BASE_DEF_FIELDS,                                               \
        .name = "x", .states = one_leaf, .events = at_ev_,           \
        .state_count = 1, .initial = 0,                               \
        .event_count = 1, .action_count = (action_count_),            \
    };                                                                 \
    assert(try_init(&d) == EPROTO);                                    \
} while (0)

static const state_machine_state_def_t one_leaf[] = {
    { .id = 0, .parent = STATE_MACHINE_ID_NONE,
      .initial_child = STATE_MACHINE_ID_NONE,
      .depth = 0, .flags = 0, .name = "only" },
};

static int try_init(const state_machine_def_t *d) {
    state_machine_t m = STATE_MACHINE_INITIALIZER;
    return state_machine_init(&m, d, NULL);
}

static int try_with_states(const state_machine_state_def_t *s,
                           uint16_t n, uint16_t init)
{
    state_machine_def_t d = {
        BASE_DEF_FIELDS,
        .name = "x", .states = s,
        .state_count = n, .initial = init,
        .event_count = 0, .guard_count = 0,
    };
    return try_init(&d);
}

int main(void) {
    /* m == NULL → EINVAL. */
    assert(state_machine_init(NULL, NULL, NULL) == EINVAL);

    /* def == NULL → EINVAL. */
    {
        state_machine_t m = STATE_MACHINE_INITIALIZER;
        assert(state_machine_init(&m, NULL, NULL) == EINVAL);
    }

    /* Bad magic → EPROTO. */
    ASSERT_ONE_LEAF_BREAKS(d.magic = 0xDEADBEEF);

    /* Bad abi_epoch → EPROTO. */
    ASSERT_ONE_LEAF_BREAKS(d.abi_epoch = (uint16_t)(STATE_MACHINE_ABI_EPOCH + 1));

    /* Bad struct_size → EPROTO. */
    ASSERT_ONE_LEAF_BREAKS(d.struct_size = 1);

    /* Reserved/flags non-zero → EPROTO. */
    ASSERT_ONE_LEAF_BREAKS(d.flags = 1);
    ASSERT_ONE_LEAF_BREAKS(d._reserved = 1);

    /* state_count == 0 → EPROTO. */
    ASSERT_ONE_LEAF_BREAKS(d.state_count = 0);

    /* state.id != index → EPROTO. */
    {
        const state_machine_state_def_t bad[] = {
            { .id = 7, .parent = STATE_MACHINE_ID_NONE,
              .initial_child = STATE_MACHINE_ID_NONE,
              .depth = 0, .flags = 0, .name = "wrong" },
        };
        assert(try_with_states(bad, 1, 0) == EPROTO);
    }

    /* Dangling parent → EPROTO. */
    {
        const state_machine_state_def_t bad[] = {
            { .id = 0, .parent = 99, .initial_child = STATE_MACHINE_ID_NONE,
              .depth = 1, .flags = 0, .name = "orphan" },
        };
        assert(try_with_states(bad, 1, 0) == EPROTO);
    }

    /* Parent cycle → EPROTO. */
    {
        const state_machine_state_def_t bad[] = {
            { .id = 0, .parent = 1, .initial_child = STATE_MACHINE_ID_NONE,
              .depth = 0, .flags = 0, .name = "a" },
            { .id = 1, .parent = 0, .initial_child = STATE_MACHINE_ID_NONE,
              .depth = 0, .flags = 0, .name = "b" },
        };
        assert(try_with_states(bad, 2, 0) == EPROTO);
    }

    /* Depth field mismatch → EPROTO. */
    {
        const state_machine_state_def_t bad[] = {
            { .id = 0, .parent = STATE_MACHINE_ID_NONE,
              .initial_child = 1,
              .depth = 0, .flags = 0, .name = "root" },
            { .id = 1, .parent = 0,
              .initial_child = STATE_MACHINE_ID_NONE,
              .depth = 99, .flags = 0, .name = "child" }, /* should be 1 */
        };
        assert(try_with_states(bad, 2, 1) == EPROTO);
    }

    /* Non-leaf initial → EPROTO (initial points to a state with children). */
    {
        const state_machine_state_def_t bad[] = {
            { .id = 0, .parent = STATE_MACHINE_ID_NONE,
              .initial_child = 1,
              .depth = 0, .flags = 0, .name = "root" },
            { .id = 1, .parent = 0,
              .initial_child = STATE_MACHINE_ID_NONE,
              .depth = 1, .flags = 0, .name = "child" },
        };
        assert(try_with_states(bad, 2, 0) == EPROTO); /* root is a superstate */
    }

    /* Superstate without initial_child → EPROTO. */
    {
        const state_machine_state_def_t bad[] = {
            { .id = 0, .parent = STATE_MACHINE_ID_NONE,
              .initial_child = STATE_MACHINE_ID_NONE,
              .depth = 0, .flags = 0, .name = "p" },
            { .id = 1, .parent = 0,
              .initial_child = STATE_MACHINE_ID_NONE,
              .depth = 1, .flags = 0, .name = "c" },
        };
        assert(try_with_states(bad, 2, 1) == EPROTO);
    }

    /* Guard fn NULL → EPROTO. */
    {
        const state_machine_guard_def_t bad_guards[] = {
            { .fn = NULL, .name = "broken" },
        };
        state_machine_def_t d = {
            BASE_DEF_FIELDS,
            .name = "x", .states = one_leaf,
            .state_count = 1, .initial = 0,
            .event_count = 0,
            .guards = bad_guards, .guard_count = 1,
        };
        assert(try_init(&d) == EPROTO);
    }

    /* Action fn NULL → EPROTO. */
    {
        const state_machine_action_def_t bad_actions[] = {
            { .fn = NULL, .name = "broken" },
        };
        state_machine_def_t d = {
            BASE_DEF_FIELDS,
            .name = "x", .states = one_leaf,
            .state_count = 1, .initial = 0,
            .event_count = 0,
            .actions = bad_actions, .action_count = 1,
        };
        assert(try_init(&d) == EPROTO);
    }

    /* Transition action out of range → EPROTO. */
    ASSERT_TRANS_BREAKS(0, STATE_MACHINE_TRANSITION_F_ACTION, 0);

    /* Unknown transition flag → EPROTO. */
    ASSERT_TRANS_BREAKS(0, 0x80, 0);

    /* Action set without ACTION flag → EPROTO. */
    ASSERT_TRANS_BREAKS(1, 0, 0);

    /* Init failure resets *m to zero. */
    {
        state_machine_t m;
        m.def = (const state_machine_def_t*)0xDEADBEEF;
        m.userdata = (void*)0xCAFEBABE;
        m.generation = 99;
        m.current = 42;
        m._dispatch_flag = 1;
        m._reserved[0] = 0xFFFF;
        m._reserved[1] = 0xFFFF;

        int rc = state_machine_init(&m, NULL, NULL);
        assert(rc == EINVAL);
        assert(m.def == NULL);
        assert(m.userdata == NULL);
        assert(m.generation == 0);
        assert(m.current == 0);
        assert(m._dispatch_flag == 0);
    }

    printf("smoke_invalid: OK\n");
    return 0;
}
