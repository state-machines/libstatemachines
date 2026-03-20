#ifndef JP_H
#define JP_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

typedef struct {
    const char *src;
    const char *p;
    const char *end;
    const char *path;
    bool        failed;
    char        err[256];
} jp_t;

void     jp_fail(jp_t *j, const char *fmt, ...);
bool     jp_peek(jp_t *j, char c);
bool     jp_consume(jp_t *j, char c);
bool     jp_consume_lit(jp_t *j, const char *lit);
bool     jp_at_null(jp_t *j);
int64_t  jp_int(jp_t *j);
uint64_t jp_uint(jp_t *j);
char    *jp_string_dup(jp_t *j);
void     jp_skip_value(jp_t *j);

/* Object-key dispatch loop; goto on error. */
#define JP_OBJ_EACH(j_, key_, fail_lbl_)                              \
    for (;;) {                                                         \
        char *key_ = jp_string_dup(j_);                               \
        if (!key_) goto fail_lbl_;                                    \
        if (!jp_consume((j_), ':')) { free(key_); goto fail_lbl_; }

#define JP_OBJ_DONE(j_, key_, fail_lbl_)                              \
        free(key_);                                                    \
        if ((j_)->failed) goto fail_lbl_;                             \
        if (jp_peek((j_), ',')) { (j_)->p++; continue; }              \
        if (!jp_consume((j_), '}')) goto fail_lbl_;                   \
        break;                                                         \
    }

/* Object-key dispatch loop; return false on error. */
#define JP_OBJ_EACH_R(j_, key_)                                       \
    for (;;) {                                                         \
        char *key_ = jp_string_dup(j_);                               \
        if (!key_) return false;                                       \
        if (!jp_consume((j_), ':')) { free(key_); return false; }

#define JP_OBJ_DONE_R(j_, key_)                                       \
        free(key_);                                                    \
        if ((j_)->failed) return false;                                \
        if (jp_peek((j_), ',')) { (j_)->p++; continue; }              \
        if (!jp_consume((j_), '}')) return false;                      \
        break;                                                         \
    }

/* Array iteration: post-malloc OOM check, consume '[', handle empty. */
#define JP_ARR_BEGIN(j_, arr_, done_lbl_)                             \
    if ((arr_) == NULL) { jp_fail((j_), "oom"); return false; }       \
    if (!jp_consume((j_), '[')) { free(arr_); return false; }         \
    if (jp_peek((j_), ']')) { (j_)->p++; goto done_lbl_; }           \
    for (;;) {

/* Array iteration: element separator and closing bracket. */
#define JP_ARR_DONE(j_, fail_lbl_)                                    \
        if (jp_peek((j_), ',')) { (j_)->p++; continue; }              \
        if (!jp_consume((j_), ']')) goto fail_lbl_;                   \
        break;                                                         \
    }

/* Double capacity when full; goto fail on OOM. */
#define JP_ARR_GROW(j_, arr_, n_, cap_) do {                          \
    if ((n_) == (cap_)) {                                              \
        void *jp_na_ = realloc((arr_), (cap_) * 2 * sizeof(*(arr_))); \
        if (!jp_na_) { jp_fail((j_), "oom"); goto fail; }             \
        (arr_) = jp_na_;                                               \
        (cap_) *= 2;                                                   \
    }                                                                  \
} while (0)

#endif /* JP_H */
