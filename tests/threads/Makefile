CC ?= gcc
ROOT := ../..
OUT := $(ROOT)/artifacts/thread-tests
FLAGS := -std=c11 -O2 -g -Wall -Wextra -Wpedantic -Werror -I$(ROOT)/runtime/core
SOURCES := test_threads.c $(addprefix $(ROOT)/runtime/core/,ctr_cpu.c ctr_memory.c ctr_guest.c ctr_ipc.c ctr_heap.c ctr_kernel.c ctr_hid.c ctr_gsp.c ctr_gx.c ctr_transfer.c ctr_fs.c ctr_apt.c ctr_thread_fixture.c) $(ROOT)/tests/fixtures/threads/thread.generated.c $(ROOT)/tests/fixtures/threads/thread-image.generated.c
.PHONY: check sanitize
$(OUT):
	mkdir -p "$@"
$(OUT)/thread-tests: $(SOURCES) $(wildcard $(ROOT)/runtime/core/*.h) | $(OUT)
	$(CC) $(FLAGS) $(SOURCES) -o "$@"
check: $(OUT)/thread-tests
	"$(OUT)/thread-tests"
sanitize: | $(OUT)
	$(CC) $(FLAGS) -O1 -fno-omit-frame-pointer -fsanitize=address,undefined $(SOURCES) -o "$(OUT)/thread-tests-sanitized"
	ASAN_OPTIONS=detect_leaks=1 "$(OUT)/thread-tests-sanitized"
