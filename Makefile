# libstatemachines -- library build.
#
# Targets:
#   make            -> build/libstate_machine.a
#   make test       -> build + run all smoke tests
#   make clean      -> wipe build/

CC      ?= cc
AR      ?= ar
WARN    := -Wall -Wextra -Wpedantic -Werror -Wshadow -Wconversion -Wstrict-prototypes
CFLAGS  ?= -std=c11 -O2 -fno-strict-aliasing -fstack-usage $(WARN)
CPPFLAGS ?= -Iinclude
LDFLAGS ?=

BUILD   := build
LIB     := $(BUILD)/libstate_machine.a
LIB_SRC := src/state_machine.c
LIB_OBJ := $(BUILD)/state_machine.o

SMOKE_BINS := $(BUILD)/smoke_flat $(BUILD)/smoke_hsm $(BUILD)/smoke_invalid \
              $(BUILD)/smoke_action

EXAMPLE_BIN := $(BUILD)/circuit_breaker

CONFORMANCE_BIN      := $(BUILD)/conformance
CONFORMANCE_SRCS     := test/conformance/harness.c \
                        test/conformance/fixture.c \
                        test/conformance/guards.c \
                        test/conformance/jp.c
CONFORMANCE_FIXTURES := $(wildcard test/conformance/*.json)

.PHONY: all test example conformance clean

all: $(LIB)

$(BUILD):
	mkdir -p $(BUILD)

$(LIB_OBJ): $(LIB_SRC) include/state_machine.h include/state_machine_schema.h | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(LIB): $(LIB_OBJ)
	$(AR) rcs $@ $^

$(BUILD)/smoke_%: test/smoke_%.c $(LIB) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) $< $(LIB) $(LDFLAGS) -o $@

test: $(SMOKE_BINS) $(CONFORMANCE_BIN) $(EXAMPLE_BIN)
	@set -e; for t in $(SMOKE_BINS); do \
	    echo "== $$t =="; \
	    $$t; \
	done
	@echo "all smoke tests passed"
	@$(CONFORMANCE_BIN) $(CONFORMANCE_FIXTURES)
	@echo "== $(EXAMPLE_BIN) =="
	@$(EXAMPLE_BIN)

# Reference consumer.
$(EXAMPLE_BIN): examples/circuit_breaker/main.c \
                examples/circuit_breaker/circuit_breaker.c \
                examples/circuit_breaker/circuit_breaker.h \
                $(LIB) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -Iexamples/circuit_breaker \
	    examples/circuit_breaker/main.c \
	    examples/circuit_breaker/circuit_breaker.c \
	    $(LIB) $(LDFLAGS) -o $@

example: $(EXAMPLE_BIN)
	@echo "== $< =="
	@$<

# Conformance harness.
$(CONFORMANCE_BIN): $(CONFORMANCE_SRCS) $(LIB) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -Itest/conformance $(CONFORMANCE_SRCS) $(LIB) $(LDFLAGS) -o $@

conformance: $(CONFORMANCE_BIN)
	@$(CONFORMANCE_BIN) $(CONFORMANCE_FIXTURES)

clean:
	rm -rf $(BUILD)
