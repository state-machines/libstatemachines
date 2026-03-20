#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "jp.h"

static int
jp_line(const jp_t *j)
{
    int line = 1;
    const char *q;
    for (q = j->src; q < j->p && q < j->end; q++)
        if (*q == '\n')
            line++;
    return line;
}

static void
jp_skip_ws(jp_t *j)
{
    while (!j->failed && j->p < j->end) {
        char c = *j->p;
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
            j->p++;
        else
            break;
    }
}

void
jp_fail(jp_t *j, const char *fmt, ...)
{
    va_list ap;
    if (j->failed)
        return;
    j->failed = true;
    va_start(ap, fmt);
    vsnprintf(j->err, sizeof(j->err), fmt, ap);
    va_end(ap);
}

bool
jp_peek(jp_t *j, char c)
{
    jp_skip_ws(j);
    return !j->failed && j->p < j->end && *j->p == c;
}

bool
jp_consume(jp_t *j, char c)
{
    jp_skip_ws(j);
    if (j->failed)
        return false;
    if (j->p < j->end && *j->p == c) {
        j->p++;
        return true;
    }
    jp_fail(j, "expected '%c' at line %d", c, jp_line(j));
    return false;
}

bool
jp_consume_lit(jp_t *j, const char *lit)
{
    size_t n = strlen(lit);
    jp_skip_ws(j);
    if (j->failed)
        return false;
    if ((size_t)(j->end - j->p) >= n && memcmp(j->p, lit, n) == 0) {
        j->p += n;
        return true;
    }
    return false;
}

bool
jp_at_null(jp_t *j)
{
    return jp_consume_lit(j, "null");
}

int64_t
jp_int(jp_t *j)
{
    int64_t sign = 1, val = 0;
    bool digits = false;

    jp_skip_ws(j);
    if (j->failed)
        return 0;
    if (j->p < j->end && *j->p == '-') {
        sign = -1;
        j->p++;
    }
    while (j->p < j->end && isdigit((unsigned char)*j->p)) {
        int d = *j->p - '0';
        /* Bound against signed overflow (UB under -fsanitize=undefined):
         * INT64_MAX = 9223372036854775807. */
        if (val > (INT64_MAX - d) / 10) {
            jp_fail(j, "integer overflow at line %d", jp_line(j));
            return 0;
        }
        val = val * 10 + d;
        j->p++;
        digits = true;
    }
    if (!digits) {
        jp_fail(j, "expected integer at line %d", jp_line(j));
        return 0;
    }
    return sign * val;
}

uint64_t
jp_uint(jp_t *j)
{
    int64_t v = jp_int(j);
    if (v < 0) {
        jp_fail(j, "expected non-negative integer at line %d", jp_line(j));
        return 0;
    }
    return (uint64_t)v;
}

/* Returns heap-allocated NUL-terminated copy; caller must free.
 * Handles \\, \", \n, \t, \r, \/ escapes. */
char *
jp_string_dup(jp_t *j)
{
    const char *s;
    char *out;
    size_t cap, len = 0;

    jp_skip_ws(j);
    if (j->failed)
        return NULL;
    if (j->p >= j->end || *j->p != '"') {
        jp_fail(j, "expected string at line %d", jp_line(j));
        return NULL;
    }
    j->p++;
    s = j->p;
    while (j->p < j->end && *j->p != '"') {
        if (*j->p == '\\' && j->p + 1 < j->end)
            j->p += 2;
        else
            j->p++;
    }
    if (j->p >= j->end) {
        jp_fail(j, "unterminated string");
        return NULL;
    }
    cap = (size_t)(j->p - s) + 1;
    out = malloc(cap);
    if (out == NULL) {
        jp_fail(j, "oom");
        return NULL;
    }
    {
        const char *q = s;
        while (q < j->p) {
            if (*q == '\\' && q + 1 < j->p) {
                switch (q[1]) {
                case '"':  out[len++] = '"';  break;
                case '\\': out[len++] = '\\'; break;
                case '/':  out[len++] = '/';  break;
                case 'n':  out[len++] = '\n'; break;
                case 't':  out[len++] = '\t'; break;
                case 'r':  out[len++] = '\r'; break;
                default:   out[len++] = q[1]; break;
                }
                q += 2;
            } else {
                out[len++] = *q++;
            }
        }
    }
    out[len] = '\0';
    j->p++;
    return out;
}

void
jp_skip_value(jp_t *j)
{
    jp_skip_ws(j);
    if (j->failed)
        return;
    if (j->p >= j->end) {
        jp_fail(j, "unexpected EOF");
        return;
    }
    switch (*j->p) {
    case '"': {
        char *s = jp_string_dup(j);
        free(s);
        return;
    }
    case '{':
        j->p++;
        if (jp_peek(j, '}')) { j->p++; return; }
        for (;;) {
            char *k = jp_string_dup(j);
            free(k);
            if (j->failed) return;
            if (!jp_consume(j, ':')) return;
            jp_skip_value(j);
            if (j->failed) return;
            if (jp_peek(j, ',')) { j->p++; continue; }
            if (!jp_consume(j, '}')) return;
            return;
        }
    case '[':
        j->p++;
        if (jp_peek(j, ']')) { j->p++; return; }
        for (;;) {
            jp_skip_value(j);
            if (j->failed) return;
            if (jp_peek(j, ',')) { j->p++; continue; }
            if (!jp_consume(j, ']')) return;
            return;
        }
    case 't': jp_consume_lit(j, "true");  return;
    case 'f': jp_consume_lit(j, "false"); return;
    case 'n': jp_consume_lit(j, "null");  return;
    default:
        if (*j->p == '-' || isdigit((unsigned char)*j->p)) {
            (void)jp_int(j);
            return;
        }
        jp_fail(j, "unexpected '%c' at line %d", *j->p, jp_line(j));
        return;
    }
}
