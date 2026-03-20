# libstatemachines

A freestanding, dependency-free finite state machine runtime in C11.

Schemas are plain data: you describe states, events, guards, and actions as
static tables, hand them to the runtime, and dispatch events. The library does
no allocation and pulls in nothing beyond the standard C headers.

## Features

- **Zero dependencies, zero allocation.** No libc requirement beyond freestanding
  headers; all storage is caller-provided.
- **Flat and hierarchical machines.** Nested states with `initial_child` descent
  and ancestor-aware matching (`state_machine_is_in`).
- **Deterministic selection.** Transitions are evaluated in declaration order;
  guards short-circuit; the first match wins.
- **Rich dispatch diagnostics.** `state_machine_dispatch_ex` reports the outcome,
  the transition taken, which guard failed, and how many transitions were tested.
- **Schema validation.** `state_machine_init` fully validates a schema before use
  and fails closed (`EINVAL`) on malformed input.
- **Frozen ABI.** Struct layouts are pinned with static assertions on LP64/LLP64
  targets; `state_machine_abi_version()` exposes the runtime ABI at load time.
- **Portable C11.** Standard headers only (`<stdint.h>`, `<stdbool.h>`,
  `<stddef.h>`, `<errno.h>`); no platform-specific dependencies.

## Build

```sh
make            # build/libstate_machine.a
make test       # build + run smoke tests, conformance corpus, and example
make example    # build + run the reference consumer
make clean
```

Override the toolchain as usual:

```sh
make CC=clang
```

The library builds under `-std=c11 -Wall -Wextra -Wpedantic -Werror` with
`-Wconversion` and `-Wshadow` enabled.

## Usage

```c
#include "state_machine.h"

/* Describe the machine as static data (see examples/ for a full schema). */
extern const state_machine_def_t my_def;

int main(void)
{
    state_machine_t m = STATE_MACHINE_INITIALIZER;

    if (state_machine_init(&m, &my_def, /*userdata=*/NULL) != 0)
        return 1;

    state_machine_result_t r;
    state_machine_dispatch_ex(&m, EV_START, /*payload=*/NULL, &r);

    if (r.outcome == STATE_MACHINE_OUTCOME_ACCEPTED)
        printf("now in: %s\n", state_machine_state_name(&my_def, r.to));
    else
        printf("rejected: %s\n", state_machine_outcome_name(r.outcome));

    state_machine_destroy(&m);
    return 0;
}
```

Dispatch outcomes:

| Outcome | Meaning |
| --- | --- |
| `ACCEPTED` | A transition fired; state changed (or self-transitioned). |
| `INVALID_ARG` | Null/invalid arguments; the instance is untouched. |
| `NO_TRANSITION` | No transition matched the event in the current state. |
| `GUARD_FAILED` | A transition matched but every candidate guard returned false. |
| `REENTRY` | Dispatch was called re-entrantly on the same instance. |

A worked example - a circuit breaker built on top of the runtime - lives in
[`examples/circuit_breaker`](examples/circuit_breaker).

## Testing

`make test` runs the smoke suite (flat machines, hierarchical machines, action
dispatch, and invalid-schema rejection), the JSON-driven conformance corpus
under `test/conformance/`, and the reference example. CI runs the same against
both GCC and Clang.

## License

BSD-2-Clause. See [LICENSE](LICENSE).
