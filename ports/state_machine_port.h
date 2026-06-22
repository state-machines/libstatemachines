/*
 * state_machine_port.h - reference freestanding port for libstatemachines.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Satisfies the port contract selected by STATE_MACHINE_FREESTANDING. This
 * reference port assumes the compiler's freestanding headers (<stdint.h>,
 * <stddef.h>, <stdbool.h>) are reachable -- they are mandated by the C standard
 * even with -ffreestanding.
 *
 * A caller that cannot reach those headers provides its own state_machine_port.h
 * earlier on the include path, in which case this file is never seen. All
 * environment-specific definitions live behind this contract.
 */
#ifndef STATE_MACHINE_PORT_H
#define STATE_MACHINE_PORT_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* UINT16_MAX comes from <stdint.h>; the errno values the library returns are
 * not guaranteed freestanding, so define them. */
#ifndef EINVAL
#  define EINVAL 22
#endif
#ifndef EBUSY
#  define EBUSY 16
#endif
#ifndef EPROTO
#  define EPROTO 71
#endif
#ifndef EOPNOTSUPP
#  define EOPNOTSUPP 95
#endif

#endif /* STATE_MACHINE_PORT_H */
