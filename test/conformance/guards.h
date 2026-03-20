#ifndef GUARDS_H
#define GUARDS_H

#include <stddef.h>
#include "state_machine.h"

extern char   g_log_buf[256];
extern size_t g_log_len;

void log_reset(void);
void log_push(char c);
state_machine_guard_fn resolve_canonical_guard(const char *name);

#endif /* GUARDS_H */
