/*
 * state_machine_schema.h - Table ABI for libstatemachines.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Public header. Defines the schema-side data types
 * (states, events, transitions, guards, machine def) and the version /
 * ABI constants every consumer needs to author or codegen a schema.
 *
 * This header is the consumer-side ABI surface. It is included by
 * state_machine.h, but may also be included directly by tools that
 * only need to construct or inspect schemas without the runtime API.
 */

#ifndef STATE_MACHINE_SCHEMA_H
#define STATE_MACHINE_SCHEMA_H

/*
 * By default the standard headers are used. Define STATE_MACHINE_FREESTANDING
 * to instead pull a caller-provided <state_machine_port.h>, letting the library
 * build in environments without a standard library.
 */
#if defined(STATE_MACHINE_FREESTANDING)
#  include <state_machine_port.h> /* must define uint8_t..uint64_t, bool/true/false,
                                     size_t, offsetof, UINT16_MAX, and the
                                     EINVAL/EBUSY/EPROTO/EOPNOTSUPP constants.
                                     A reference port ships in ports/. */
#else
#  include <stdint.h>
#  include <stdbool.h>
#  include <stddef.h>
#  include <errno.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Version and ABI constants.

#define STATE_MACHINE_SPEC_VERSION_MAJOR 0
#define STATE_MACHINE_SPEC_VERSION_MINOR 1
#define STATE_MACHINE_SPEC_VERSION_PATCH 0

/* Encoded form embedded in state_machine_def_t::spec_version. */
#define STATE_MACHINE_SPEC_VERSION \
    (((STATE_MACHINE_SPEC_VERSION_MAJOR) << 8) | (STATE_MACHINE_SPEC_VERSION_MINOR))

#define STATE_MACHINE_ABI_EPOCH    1   /* layout-generation; bumps on incompatible layout */
#define STATE_MACHINE_ABI_REVISION 0   /* informational; tracks spec changes within an epoch */

/*
 * Native uint32_t value. No portability claim is made for memory-dump
 * byte order; this is a native integer comparison.
 */
#define STATE_MACHINE_MAGIC        0x534D4330u

/* state_machine_transition_def_t::flags */
#define STATE_MACHINE_TRANSITION_F_ACTION 0x01u

/* Implementations MAY raise this; MUST NOT lower it. */
#define STATE_MACHINE_MAX_DEPTH    32

// Compile-time assertion macro.

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#  define STATE_MACHINE_CTASSERT(cond, tag) _Static_assert(cond, #tag)
#else
#  define STATE_MACHINE_CTASSERT(cond, tag) \
       typedef char state_machine_ctassert_##tag[(cond) ? 1 : -1]
#endif

// Identity types.

typedef uint16_t state_machine_state_id_t;
typedef uint16_t state_machine_event_id_t;
typedef uint16_t state_machine_guard_id_t;
typedef uint16_t state_machine_action_id_t;

/* Reserved sentinel for all three ID categories. */
#define STATE_MACHINE_ID_NONE ((uint16_t)0xFFFFu)

// Forward declaration so guard fn typedef can reference state_machine_t
// without pulling in the runtime header. The full struct is defined in
// state_machine.h.

struct state_machine;
typedef struct state_machine state_machine_t;

/* Guard function pointer type. */
typedef bool (*state_machine_guard_fn)(const state_machine_t *m,
                                       state_machine_event_id_t event,
                                       const void *payload);

/* Action function pointer type. */
typedef void (*state_machine_action_fn)(const state_machine_t *m,
                                        state_machine_event_id_t event,
                                        void *payload);

// state_machine_state_def_t.

typedef struct {
    state_machine_state_id_t id;
    state_machine_state_id_t parent;
    state_machine_state_id_t initial_child;
    uint8_t                  depth;
    uint8_t                  flags;          /* MUST be zero in v0 */
    const char              *name;
} state_machine_state_def_t;

// state_machine_transition_def_t.

typedef struct {
    const state_machine_state_id_t *sources;       /* non-NULL */
    const state_machine_guard_id_t *guards;        /* MAY be NULL iff guard_count == 0 */
    state_machine_state_id_t        target;
    uint16_t                        source_count;  /* >= 1 */
    uint16_t                        guard_count;
    state_machine_action_id_t       action;        /* valid iff ACTION flag is set */
    uint16_t                        ordinal;       /* index in event.transitions */
    uint8_t                         flags;         /* STATE_MACHINE_TRANSITION_F_* */
    uint8_t                         _reserved;     /* MUST be zero */
} state_machine_transition_def_t;

// Convenience constructors for transition tables.
//
// Every action transition fixes flags to the ACTION variant and _reserved
// to 0; spelling those two fields out per edge is pure boilerplate. These
// keep the variable part (sources/guards/target/action/ordinal) explicit
// while folding the constants away.
//
//   STATE_MACHINE_TRANSITION   - guarded edge.
//   STATE_MACHINE_TRANSITION_NG - unguarded edge (guards = NULL, guard_count = 0).
#define STATE_MACHINE_TRANSITION(src, scount, gds, gcount, tgt, act, ord)                 \
    { .sources = (src), .guards = (gds), .target = (tgt),                      \
      .source_count = (scount), .guard_count = (gcount), .action = (act),      \
      .ordinal = (ord), .flags = STATE_MACHINE_TRANSITION_F_ACTION,            \
      ._reserved = 0 }

#define STATE_MACHINE_TRANSITION_NG(src, scount, tgt, act, ord)                           \
    STATE_MACHINE_TRANSITION((src), (scount), NULL, 0, (tgt), (act), (ord))

// state_machine_event_def_t.

typedef struct {
    state_machine_event_id_t              id;
    uint16_t                              transition_count;
    const state_machine_transition_def_t *transitions;
    const char                           *name;
} state_machine_event_def_t;

// state_machine_guard_def_t.

typedef struct {
    state_machine_guard_fn fn;
    const char            *name;
} state_machine_guard_def_t;

// state_machine_action_def_t.

typedef struct {
    state_machine_action_fn fn;
    const char             *name;
} state_machine_action_def_t;

// state_machine_def_t.

typedef struct {
    uint32_t                              magic;        /* STATE_MACHINE_MAGIC */
    uint16_t                              abi_epoch;    /* layout-generation */
    uint16_t                              abi_revision; /* informational */
    uint16_t                              struct_size;  /* sizeof(state_machine_def_t) at producer */
    uint16_t                              flags;        /* MUST be zero in v0 */
    uint16_t                              spec_version; /* (major<<8)|minor library version at codegen */
    uint16_t                              _reserved;    /* MUST be zero */
    const char                           *name;
    const state_machine_state_def_t      *states;
    const state_machine_event_def_t      *events;
    const state_machine_guard_def_t      *guards;
    const state_machine_action_def_t     *actions;
    uint16_t                              state_count;
    state_machine_state_id_t              initial;
    uint16_t                              event_count;
    uint16_t                              guard_count;
    uint16_t                              action_count;
} state_machine_def_t;

// Static assertions for ID sentinel and schema layout.
//
// Universal invariants are asserted unconditionally. Exact size/offset
// values are LP64-specific (the dominant target) and are gated
// on detected 64-bit pointer ABI. The committed JSON under test/abi/ holds
// per-target expected sizes as reference values; a JSON-vs-sizeof validator
// is a planned deliverable, so today the _Static_asserts
// below are the active guarantee.

/* Mandate: STATE_MACHINE_ID_NONE == UINT16_MAX. */
STATE_MACHINE_CTASSERT(STATE_MACHINE_ID_NONE == UINT16_MAX,
                       id_none_is_uint16_max);

/* Epoch and revision each pack into a 16-bit half of abi_version(),
 * and into uint16_t table fields; neither may exceed UINT16_MAX. */
STATE_MACHINE_CTASSERT(STATE_MACHINE_ABI_EPOCH <= 0xFFFF
                       && STATE_MACHINE_ABI_REVISION <= 0xFFFF,
                       abi_version_fits_u16);

/* ID typedefs are 16-bit unsigned. */
STATE_MACHINE_CTASSERT(sizeof(state_machine_state_id_t) == 2, state_id_size);
STATE_MACHINE_CTASSERT(sizeof(state_machine_event_id_t) == 2, event_id_size);
STATE_MACHINE_CTASSERT(sizeof(state_machine_guard_id_t) == 2, guard_id_size);
STATE_MACHINE_CTASSERT(sizeof(state_machine_action_id_t) == 2, action_id_size);

/* state_machine_state_def_t - field ordering invariants. */
STATE_MACHINE_CTASSERT(offsetof(state_machine_state_def_t, id) == 0,
                       state_def_id_first);
STATE_MACHINE_CTASSERT(offsetof(state_machine_state_def_t, parent) == 2,
                       state_def_parent_offset);
STATE_MACHINE_CTASSERT(offsetof(state_machine_state_def_t, initial_child) == 4,
                       state_def_initial_child_offset);
STATE_MACHINE_CTASSERT(offsetof(state_machine_state_def_t, depth) == 6,
                       state_def_depth_offset);
STATE_MACHINE_CTASSERT(offsetof(state_machine_state_def_t, flags) == 7,
                       state_def_flags_offset);

/* state_machine_transition_def_t - field ordering invariants. */
STATE_MACHINE_CTASSERT(offsetof(state_machine_transition_def_t, sources) == 0,
                       trans_def_sources_first);

/* state_machine_event_def_t - field ordering invariants. */
STATE_MACHINE_CTASSERT(offsetof(state_machine_event_def_t, id) == 0,
                       event_def_id_first);
STATE_MACHINE_CTASSERT(offsetof(state_machine_event_def_t, transition_count) == 2,
                       event_def_transition_count_offset);

/* state_machine_guard_def_t - field ordering invariants. */
STATE_MACHINE_CTASSERT(offsetof(state_machine_guard_def_t, fn) == 0,
                       guard_def_fn_first);

/* state_machine_action_def_t - field ordering invariants. */
STATE_MACHINE_CTASSERT(offsetof(state_machine_action_def_t, fn) == 0,
                       action_def_fn_first);

/* state_machine_def_t - preamble field offsets are ABI-invariant
 * (uint32 + six uint16 packs identically on every supported target). */
STATE_MACHINE_CTASSERT(offsetof(state_machine_def_t, magic) == 0,
                       def_magic_first);
STATE_MACHINE_CTASSERT(offsetof(state_machine_def_t, abi_epoch) == 4,
                       def_abi_epoch_offset);
STATE_MACHINE_CTASSERT(offsetof(state_machine_def_t, abi_revision) == 6,
                       def_abi_revision_offset);
STATE_MACHINE_CTASSERT(offsetof(state_machine_def_t, struct_size) == 8,
                       def_struct_size_offset);
STATE_MACHINE_CTASSERT(offsetof(state_machine_def_t, flags) == 10,
                       def_flags_offset);
STATE_MACHINE_CTASSERT(offsetof(state_machine_def_t, spec_version) == 12,
                       def_spec_version_offset);
STATE_MACHINE_CTASSERT(offsetof(state_machine_def_t, _reserved) == 14,
                       def_reserved_offset);

/*
 * LP64 / LLP64 (any ABI with 8-byte pointers): exact sizes and pointer-
 * field offsets. This covers FreeBSD/DragonFly amd64, Linux x86_64,
 * macOS arm64, Linux/FreeBSD aarch64, and Win64 (LLP64) - all of which
 * pad identically here because no `long` is used in any public struct.
 */
#if defined(__LP64__) || defined(_LP64) || defined(_WIN64)

STATE_MACHINE_CTASSERT(sizeof(void *) == 8, lp64_pointer_size);

/* state_machine_state_def_t: 3*u16 + 2*u8 + 8B ptr → 16 bytes. */
STATE_MACHINE_CTASSERT(offsetof(state_machine_state_def_t, name) == 8,
                       state_def_name_offset_lp64);
STATE_MACHINE_CTASSERT(sizeof(state_machine_state_def_t) == 16,
                       state_def_size_lp64);

/* state_machine_transition_def_t: 2*ptr + 5*u16 + 2*u8 + tail pad → 32 bytes. */
STATE_MACHINE_CTASSERT(offsetof(state_machine_transition_def_t, guards) == 8,
                       trans_def_guards_offset_lp64);
STATE_MACHINE_CTASSERT(offsetof(state_machine_transition_def_t, target) == 16,
                       trans_def_target_offset_lp64);
STATE_MACHINE_CTASSERT(offsetof(state_machine_transition_def_t, source_count) == 18,
                       trans_def_source_count_offset_lp64);
STATE_MACHINE_CTASSERT(offsetof(state_machine_transition_def_t, guard_count) == 20,
                       trans_def_guard_count_offset_lp64);
STATE_MACHINE_CTASSERT(offsetof(state_machine_transition_def_t, action) == 22,
                       trans_def_action_offset_lp64);
STATE_MACHINE_CTASSERT(offsetof(state_machine_transition_def_t, ordinal) == 24,
                       trans_def_ordinal_offset_lp64);
STATE_MACHINE_CTASSERT(offsetof(state_machine_transition_def_t, flags) == 26,
                       trans_def_flags_offset_lp64);
STATE_MACHINE_CTASSERT(offsetof(state_machine_transition_def_t, _reserved) == 27,
                       trans_def_reserved_offset_lp64);
STATE_MACHINE_CTASSERT(sizeof(state_machine_transition_def_t) == 32,
                       trans_def_size_lp64);

/* state_machine_event_def_t: 2*u16 + 4B pad + 2*ptr → 24 bytes. */
STATE_MACHINE_CTASSERT(offsetof(state_machine_event_def_t, transitions) == 8,
                       event_def_transitions_offset_lp64);
STATE_MACHINE_CTASSERT(offsetof(state_machine_event_def_t, name) == 16,
                       event_def_name_offset_lp64);
STATE_MACHINE_CTASSERT(sizeof(state_machine_event_def_t) == 24,
                       event_def_size_lp64);

/* state_machine_guard_def_t: 2*ptr → 16 bytes. */
STATE_MACHINE_CTASSERT(offsetof(state_machine_guard_def_t, name) == 8,
                       guard_def_name_offset_lp64);
STATE_MACHINE_CTASSERT(sizeof(state_machine_guard_def_t) == 16,
                       guard_def_size_lp64);

/* state_machine_action_def_t: 2*ptr → 16 bytes. */
STATE_MACHINE_CTASSERT(offsetof(state_machine_action_def_t, name) == 8,
                       action_def_name_offset_lp64);
STATE_MACHINE_CTASSERT(sizeof(state_machine_action_def_t) == 16,
                       action_def_size_lp64);

/* state_machine_def_t: 16B preamble + 5*ptr + 5*u16 + padding → 72 bytes. */
STATE_MACHINE_CTASSERT(offsetof(state_machine_def_t, name) == 16,
                       def_name_offset_lp64);
STATE_MACHINE_CTASSERT(offsetof(state_machine_def_t, states) == 24,
                       def_states_offset_lp64);
STATE_MACHINE_CTASSERT(offsetof(state_machine_def_t, events) == 32,
                       def_events_offset_lp64);
STATE_MACHINE_CTASSERT(offsetof(state_machine_def_t, guards) == 40,
                       def_guards_offset_lp64);
STATE_MACHINE_CTASSERT(offsetof(state_machine_def_t, actions) == 48,
                       def_actions_offset_lp64);
STATE_MACHINE_CTASSERT(offsetof(state_machine_def_t, state_count) == 56,
                       def_state_count_offset_lp64);
STATE_MACHINE_CTASSERT(offsetof(state_machine_def_t, initial) == 58,
                       def_initial_offset_lp64);
STATE_MACHINE_CTASSERT(offsetof(state_machine_def_t, event_count) == 60,
                       def_event_count_offset_lp64);
STATE_MACHINE_CTASSERT(offsetof(state_machine_def_t, guard_count) == 62,
                       def_guard_count_offset_lp64);
STATE_MACHINE_CTASSERT(offsetof(state_machine_def_t, action_count) == 64,
                       def_action_count_offset_lp64);
STATE_MACHINE_CTASSERT(sizeof(state_machine_def_t) == 72,
                       def_size_lp64);

#endif /* LP64 / LLP64 */

/* struct_size is a uint16_t field, so the schema struct must fit. */
STATE_MACHINE_CTASSERT(sizeof(state_machine_def_t) <= 0xFFFFu,
                       def_fits_struct_size);

#ifdef __cplusplus
}
#endif

#endif /* STATE_MACHINE_SCHEMA_H */
