CC ?= gcc
ROOT := ../..
OUT := $(ROOT)/artifacts/heap-tests
FLAGS := -std=c11 -O2 -g -Wall -Wextra -Wpedantic -Werror -I$(ROOT)/runtime/core
SOURCES := test_heap.c $(ROOT)/runtime/core/ctr_cpu.c $(ROOT)/runtime/core/ctr_memory.c \
           $(ROOT)/runtime/core/ctr_guest.c $(ROOT)/runtime/core/ctr_ipc.c $(ROOT)/runtime/core/ctr_heap.c $(ROOT)/runtime/core/ctr_kernel.c $(ROOT)/runtime/core/ctr_hid.c $(ROOT)/runtime/core/ctr_gsp.c $(ROOT)/runtime/core/ctr_gx.c $(ROOT)/runtime/core/ctr_transfer.c $(ROOT)/runtime/core/ctr_fs.c $(ROOT)/runtime/core/ctr_apt.c \
           $(ROOT)/runtime/core/ctr_heap_fixture.c \
           $(ROOT)/tests/fixtures/heap/heap.generated.c $(ROOT)/tests/fixtures/heap/heap-image.generated.c
.PHONY: check sanitize
$(OUT):
	mkdir -p "$@"
$(OUT)/heap-tests: $(SOURCES) $(wildcard $(ROOT)/runtime/core/*.h) | $(OUT)
	$(CC) $(FLAGS) $(SOURCES) -o "$@"
check: $(OUT)/heap-tests
	"$(OUT)/heap-tests"
sanitize: | $(OUT)
	$(CC) $(FLAGS) -O1 -fno-omit-frame-pointer -fsanitize=address,undefined $(SOURCES) -o "$(OUT)/heap-tests-sanitized"
	ASAN_OPTIONS=detect_leaks=1 "$(OUT)/heap-tests-sanitized"
