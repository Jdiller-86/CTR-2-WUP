CC ?= gcc
ROOT := ../..
include $(ROOT)/runtime/core/sources.mk
OUT := $(ROOT)/artifacts/module-tests
FLAGS := -std=c11 -O2 -g -Wall -Wextra -Wpedantic -Werror -I$(ROOT)/runtime/core
SOURCES := $(CTR_RUNTIME_SOURCES) test_modules.c $(wildcard ../fixtures/modules/*.generated.c)
.PHONY: check sanitize
$(OUT):
	mkdir -p "$@"
$(OUT)/module-tests: $(SOURCES) $(CTR_RUNTIME_MANIFEST) $(wildcard $(ROOT)/runtime/core/*.h) | $(OUT)
	$(CC) $(FLAGS) $(SOURCES) -lm -o "$@"
check: $(OUT)/module-tests
	"$(OUT)/module-tests"
sanitize: | $(OUT)
	$(CC) $(FLAGS) -O1 -fno-omit-frame-pointer -fsanitize=address,undefined $(SOURCES) -lm -o "$(OUT)/module-tests-sanitized"
	ASAN_OPTIONS=detect_leaks=1 "$(OUT)/module-tests-sanitized"
