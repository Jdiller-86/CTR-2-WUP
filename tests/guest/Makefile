CC ?= gcc
ROOT := ../..
OUT := $(ROOT)/artifacts/guest-tests
FLAGS := -std=c11 -O2 -g -Wall -Wextra -Wpedantic -Werror -I$(ROOT)/runtime/core
SOURCES := test_guest.c $(ROOT)/runtime/core/ctr_cpu.c $(ROOT)/runtime/core/ctr_memory.c \
           $(ROOT)/runtime/core/ctr_guest.c $(ROOT)/runtime/core/ctr_ipc.c $(ROOT)/runtime/core/ctr_heap.c $(ROOT)/runtime/core/ctr_kernel.c $(ROOT)/runtime/core/ctr_hid.c $(ROOT)/runtime/core/ctr_guest_fixture.c \
           $(ROOT)/tests/fixtures/guest/framebuffer.generated.c $(ROOT)/tests/fixtures/guest/image.generated.c
.PHONY: check sanitize
$(OUT):
	mkdir -p "$@"
$(OUT)/guest-tests: $(SOURCES) $(wildcard $(ROOT)/runtime/core/*.h) | $(OUT)
	$(CC) $(FLAGS) $(SOURCES) -o "$@"
check: $(OUT)/guest-tests
	"$(OUT)/guest-tests"
sanitize: | $(OUT)
	$(CC) $(FLAGS) -O1 -fno-omit-frame-pointer -fsanitize=address,undefined $(SOURCES) -o "$(OUT)/guest-tests-sanitized"
	ASAN_OPTIONS=detect_leaks=1 "$(OUT)/guest-tests-sanitized"
