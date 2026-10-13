CC ?= gcc
ROOT := ../..
include $(ROOT)/runtime/core/sources.mk
OUT := $(ROOT)/artifacts/multiple-tests
FLAGS := -std=c11 -O2 -g -Wall -Wextra -Wpedantic -Werror -I$(ROOT)/runtime/core
SOURCES := $(CTR_RUNTIME_SOURCES) test_multiple.c $(addprefix $(ROOT)/runtime/core/,ctr_stack_fixture.c) \
    $(ROOT)/tests/fixtures/stack/stack.generated.c $(ROOT)/tests/fixtures/stack/stack-image.generated.c
.PHONY: check sanitize
$(OUT):
	mkdir -p "$@"
$(OUT)/multiple-tests: $(SOURCES) $(CTR_RUNTIME_MANIFEST) $(wildcard $(ROOT)/runtime/core/*.h) | $(OUT)
	$(CC) $(FLAGS) $(SOURCES) -o "$@"
check: $(OUT)/multiple-tests
	"$(OUT)/multiple-tests"
sanitize: | $(OUT)
	$(CC) $(FLAGS) -O1 -fno-omit-frame-pointer -fsanitize=address,undefined $(SOURCES) -o "$(OUT)/multiple-tests-sanitized"
	ASAN_OPTIONS=detect_leaks=1 "$(OUT)/multiple-tests-sanitized"
