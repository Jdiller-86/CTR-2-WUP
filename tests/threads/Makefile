CC ?= gcc
ROOT := ../..
include $(ROOT)/runtime/core/sources.mk
OUT := $(ROOT)/artifacts/thread-tests
FLAGS := -std=c11 -O2 -g -Wall -Wextra -Wpedantic -Werror -I$(ROOT)/runtime/core
SOURCES := $(CTR_RUNTIME_SOURCES) test_threads.c $(addprefix $(ROOT)/runtime/core/,ctr_thread_fixture.c) $(ROOT)/tests/fixtures/threads/thread.generated.c $(ROOT)/tests/fixtures/threads/thread-image.generated.c
.PHONY: check sanitize
$(OUT):
	mkdir -p "$@"
$(OUT)/thread-tests: $(SOURCES) $(CTR_RUNTIME_MANIFEST) $(wildcard $(ROOT)/runtime/core/*.h) | $(OUT)
	$(CC) $(FLAGS) $(SOURCES) -o "$@"
check: $(OUT)/thread-tests
	"$(OUT)/thread-tests"
sanitize: | $(OUT)
	$(CC) $(FLAGS) -O1 -fno-omit-frame-pointer -fsanitize=address,undefined $(SOURCES) -o "$(OUT)/thread-tests-sanitized"
	ASAN_OPTIONS=detect_leaks=1 "$(OUT)/thread-tests-sanitized"
