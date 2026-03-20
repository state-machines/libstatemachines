#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "state_machine.h"
#include "jp.h"
#include "fixture.h"
#include "guards.h"

static char *
slurp(const char *path, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    char *buf;
    long sz;

    if (f == NULL) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    sz = ftell(f);
    if (sz < 0) { fclose(f); return NULL; }
    rewind(f);
    buf = malloc((size_t)sz + 1);
    if (buf == NULL) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        fclose(f); free(buf); return NULL;
    }
    buf[sz] = '\0';
    fclose(f);
    *out_len = (size_t)sz;
    return buf;
}

static int
run_fixture(const char *path)
{
    char *src;
    size_t src_len;
    fixture_t fx = {0};
    build_ctx_t bc = { .fx = &fx };
    jp_t j;
    state_machine_t m = STATE_MACHINE_INITIALIZER;
    int rc;
    int failures = 0;
    size_t step_i;

    src = slurp(path, &src_len);
    if (src == NULL) {
        fprintf(stderr, "  cannot read %s: %s\n", path, strerror(errno));
        return 1;
    }

    j.src    = src;
    j.p      = src;
    j.end    = src + src_len;
    j.path   = path;
    j.failed = false;
    j.err[0] = '\0';

    if (!parse_fixture(&j, &bc) || j.failed) {
        fprintf(stderr, "  PARSE ERROR: %s\n", j.err[0] ? j.err : "(unknown)");
        free(src);
        ptr_pool_free(&bc.pool);
        fixture_destroy(&fx);
        return 1;
    }
    free(src);

    rc = state_machine_init(&m, &fx.def, &fx);
    if (rc != 0) {
        fprintf(stderr, "  INIT FAILED: rc=%d (%s)\n", rc, strerror(rc));
        ptr_pool_free(&bc.pool);
        fixture_destroy(&fx);
        return 1;
    }

    for (step_i = 0; step_i < fx.trace_count; step_i++) {
        const trace_step_t *st = &fx.trace[step_i];
        state_machine_result_t r;
        void *payload = st->has_payload ? (void *)&st->payload_byte : NULL;
        int actual_rc;
        int step_failures = 0;

        log_reset();
        actual_rc = state_machine_dispatch_ex(&m, st->event, payload, &r);

#define CHECK(cond, fmt, ...) do {                                            \
    if (!(cond)) {                                                            \
        fprintf(stderr, "  step %zu: " fmt "\n", step_i + 1, __VA_ARGS__);   \
        step_failures++;                                                      \
    }                                                                         \
} while (0)

        CHECK(actual_rc == st->expect_result,
              "result: expected %d (%s), got %d (%s)",
              st->expect_result, strerror(st->expect_result),
              actual_rc, strerror(actual_rc));
        CHECK(r.outcome == st->expect_outcome,
              "outcome: expected %s, got %s",
              state_machine_outcome_name(st->expect_outcome),
              state_machine_outcome_name(r.outcome));
        CHECK(state_machine_current(&m) == st->expect_current,
              "current: expected %u, got %u",
              st->expect_current, state_machine_current(&m));
        CHECK(state_machine_generation(&m) == st->expect_generation,
              "generation: expected %llu, got %llu",
              (unsigned long long)st->expect_generation,
              (unsigned long long)state_machine_generation(&m));
        CHECK(r.transition_index == st->expect_transition_index,
              "transition_index: expected %u, got %u",
              st->expect_transition_index, r.transition_index);
        CHECK(r.guard_ordinal == st->expect_guard_ordinal,
              "guard_ordinal: expected %u, got %u",
              st->expect_guard_ordinal, r.guard_ordinal);
        CHECK(r.guard_id == st->expect_guard_id,
              "guard_id: expected %u, got %u",
              st->expect_guard_id, r.guard_id);
        CHECK(r.transitions_tested == st->expect_transitions_tested,
              "transitions_tested: expected %u, got %u",
              st->expect_transitions_tested, r.transitions_tested);
        if (st->expect_log) {
            CHECK(strcmp(g_log_buf, st->expect_log) == 0,
                  "log: expected \"%s\", got \"%s\"",
                  st->expect_log, g_log_buf);
        }
#undef CHECK
        failures += step_failures;
    }

    state_machine_destroy(&m);
    ptr_pool_free(&bc.pool);
    fixture_destroy(&fx);
    return failures;
}

int
main(int argc, char **argv)
{
    int i;
    int total_failures = 0;
    int fixture_failures = 0;

    if (argc < 2) {
        fprintf(stderr, "usage: %s <fixture.json> [<fixture.json> ...]\n", argv[0]);
        return 2;
    }

    for (i = 1; i < argc; i++) {
        int rc;
        printf("== %s ==\n", argv[i]);
        rc = run_fixture(argv[i]);
        if (rc == 0) {
            printf("  OK\n");
        } else {
            printf("  FAIL (%d assertion failures)\n", rc);
            total_failures += rc;
            fixture_failures++;
        }
    }

    if (fixture_failures == 0) {
        printf("conformance: all %d fixture(s) passed\n", argc - 1);
        return 0;
    }
    fprintf(stderr, "conformance: %d fixture(s) failed, %d total assertion failures\n",
            fixture_failures, total_failures);
    return 1;
}
