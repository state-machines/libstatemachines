#define _POSIX_C_SOURCE 200809L

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "guards.h"

char   g_log_buf[256];
size_t g_log_len;

void
log_reset(void)
{
    g_log_len = 0;
    g_log_buf[0] = '\0';
}

void
log_push(char c)
{
    if (g_log_len + 1 < sizeof(g_log_buf)) {
        g_log_buf[g_log_len++] = c;
        g_log_buf[g_log_len]   = '\0';
    }
}

static bool guard_always(const state_machine_t *m, state_machine_event_id_t e, const void *p)
{ (void)m; (void)e; (void)p; return true; }

static bool guard_never(const state_machine_t *m, state_machine_event_id_t e, const void *p)
{ (void)m; (void)e; (void)p; return false; }

static bool guard_payload_truthy(const state_machine_t *m, state_machine_event_id_t e, const void *p)
{ (void)m; (void)e; return p != NULL; }

static bool guard_payload_byte_zero(const state_machine_t *m, state_machine_event_id_t e, const void *p)
{ (void)m; (void)e; return p != NULL && *(const uint8_t *)p == 0; }

static bool guard_userdata_set(const state_machine_t *m, state_machine_event_id_t e, const void *p)
{ (void)e; (void)p; return state_machine_userdata(m) != NULL; }

#define DEFINE_RECORD_GUARD(name_, ch_) \
    static bool name_(const state_machine_t *m, state_machine_event_id_t e, const void *p) \
    { (void)m; (void)e; (void)p; log_push(ch_); return true; }

DEFINE_RECORD_GUARD(guard_record_a, 'a')
DEFINE_RECORD_GUARD(guard_record_b, 'b')

static bool guard_record_c_then_false(const state_machine_t *m, state_machine_event_id_t e, const void *p)
{ (void)m; (void)e; (void)p; log_push('c'); return false; }

state_machine_guard_fn
resolve_canonical_guard(const char *name)
{
    if (strcmp(name, "always") == 0)               return guard_always;
    if (strcmp(name, "never") == 0)                return guard_never;
    if (strcmp(name, "payload_truthy") == 0)       return guard_payload_truthy;
    if (strcmp(name, "payload_byte_zero") == 0)    return guard_payload_byte_zero;
    if (strcmp(name, "userdata_set") == 0)         return guard_userdata_set;
    if (strcmp(name, "record_a") == 0)             return guard_record_a;
    if (strcmp(name, "record_b") == 0)             return guard_record_b;
    if (strcmp(name, "record_c_then_false") == 0)  return guard_record_c_then_false;
    return NULL;
}
