CC ?= gcc
ROOT := ../..
include $(ROOT)/runtime/core/sources.mk
OUT := $(ROOT)/artifacts/kernel-tests
FLAGS := -std=c11 -O2 -g -Wall -Wextra -Wpedantic -Werror -I$(ROOT)/runtime/core
SOURCES := $(CTR_RUNTIME_SOURCES) test_kernel.c \
                       $(ROOT)/runtime/core/ctr_kernel_fixture.c \
           $(ROOT)/tests/fixtures/kernel/kernel.generated.c $(ROOT)/tests/fixtures/kernel/kernel-image.generated.c
.PHONY: check sanitize
$(OUT):
	mkdir -p "$@"
$(OUT)/kernel-tests: $(SOURCES) $(CTR_RUNTIME_MANIFEST) $(wildcard $(ROOT)/runtime/core/*.h) | $(OUT)
	$(CC) $(FLAGS) $(SOURCES) -o "$@"
check: $(OUT)/kernel-tests
	"$(OUT)/kernel-tests"
sanitize: | $(OUT)
	$(CC) $(FLAGS) -O1 -fno-omit-frame-pointer -fsanitize=address,undefined $(SOURCES) -o "$(OUT)/kernel-tests-sanitized"
	ASAN_OPTIONS=detect_leaks=1 "$(OUT)/kernel-tests-sanitized"
