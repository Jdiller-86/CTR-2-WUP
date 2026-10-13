CC ?= gcc
ROOT := ../..
OUT := $(ROOT)/artifacts/cpu
FLAGS := -std=c11 -O2 -g -Wall -Wextra -Wpedantic -Werror -I$(ROOT)/runtime/core -I$(ROOT)/runtime/wiiu
SOURCES := test_core.c $(ROOT)/runtime/core/ctr_cpu.c $(ROOT)/runtime/core/ctr_memory.c \
    $(ROOT)/runtime/core/ctr_cpu_selftest.c $(ROOT)/runtime/wiiu/viewport.c \
    $(ROOT)/tests/fixtures/cpu/arithmetic.generated.c $(ROOT)/tests/fixtures/cpu/exit.generated.c
.PHONY: all check sanitize
all: $(OUT)/core-tests
$(OUT):
	mkdir -p "$@"
$(OUT)/core-tests: $(SOURCES) $(wildcard $(ROOT)/runtime/core/*.h) | $(OUT)
	$(CC) $(FLAGS) $(SOURCES) -o "$@"
check: $(OUT)/core-tests
	"$(OUT)/core-tests"
sanitize: | $(OUT)
	$(CC) $(FLAGS) -O1 -fno-omit-frame-pointer -fsanitize=address,undefined $(SOURCES) -o "$(OUT)/core-tests-sanitized"
	ASAN_OPTIONS=detect_leaks=1 "$(OUT)/core-tests-sanitized"
