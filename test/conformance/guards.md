# Canonical Guard Registry

Cross-language conformance requires every harness (Ruby, Rust, C) to wire
named fixture guards to identical implementations. This document defines
the canonical behavior of every guard name used in `test/conformance/*.json`.

A new fixture introducing a new guard name MUST add it here in the same
patch. Harnesses MUST implement every guard listed here with these exact
semantics.

## Standard guards

### `always`

Always returns `true`. Reads no payload, no userdata. Pure.

### `never`

Always returns `false`. Reads no payload, no userdata. Pure.

### `payload_truthy`

Returns `true` iff `payload != NULL`. Does not dereference payload.

### `payload_byte_zero`

Returns `true` iff `payload != NULL` and the byte at `payload` (cast to
`uint8_t *`) is zero. UB if payload is non-NULL but does not point to at
least one readable byte; fixtures using this guard always supply a valid
single-byte buffer or NULL.

### `userdata_set`

Returns `true` iff `state_machine_userdata(m) != NULL`.

### `record_a`

Appends the literal byte `'a'` (0x61) to a harness-side log. Returns
`true`. The log is reset between dispatches by the harness, not by the
guard.

### `record_b`

Same as `record_a` but appends `'b'` (0x62). Returns `true`.

### `record_c_then_false`

Appends `'c'` (0x63) to the harness log. Returns `false`.

## Why three recording guards

Fixtures verifying guard ordering and short-circuit behavior need
distinguishable guard identities. A transition declared as

    guards: [record_a, record_c_then_false, record_b]

produces log `"ac"` on guard-fail (b is never invoked), proving:

1. Guards are evaluated in declaration order.
2. Evaluation short-circuits on the first `false`.

A single recording guard could not prove ordering since two invocations
would be indistinguishable.

## Forbidden guard behaviors

Guards MUST NOT:

- Call any mutating `state_machine_*` function on the same instance.
  This is undefined behavior.
- Mutate state visible to subsequent guards in the same dispatch in a
  way that would change a guard's return value. (The harness log
  is permitted because it does not feed back into guard returns.)
- Sleep, allocate, or perform I/O.

## Versioning

This registry is normative for the spec version recorded in the fixture's
`spec_version` field. New guard names land in new spec minor versions.
