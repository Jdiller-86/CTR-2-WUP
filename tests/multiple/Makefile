CC ?= gcc
ROOT := ../..
OUT := $(ROOT)/artifacts/multiple-tests
FLAGS := -std=c11 -O2 -g -Wall -Wextra -Wpedantic -Werror -I$(ROOT)/runtime/core
SOURCES := test_multiple.c $(addprefix $(ROOT)/runtime/core/,ctr_cpu.c ctr_memory.c ctr_guest.c ctr_ipc.c ctr_heap.c ctr_kernel.c ctr_hid.c ctr_gsp.c ctr_gx.c ctr_transfer.c ctr_fs.c ctr_apt.c ctr_stack_fixture.c) \
           $(ROOT)/tests/fixtures/stack/stack.generated.c $(ROOT)/tests/fixtures/stack/stack-image.generated.c
.PHONY: check sanitize
$(OUT):
	mkdir -p "$@"
$(OUT)/multiple-tests: $(SOURCES) $(wildcard $(ROOT)/runtime/core/*.h) | $(OUT)
	$(CC) $(FLAGS) $(SOURCES) -o "$@"
check: $(OUT)/multiple-tests
	"$(OUT)/multiple-tests"
sanitize: | $(OUT)
	$(CC) $(FLAGS) -O1 -fno-omit-frame-pointer -fsanitize=address,undefined $(SOURCES) -o "$(OUT)/multiple-tests-sanitized"
	ASAN_OPTIONS=detect_leaks=1 "$(OUT)/multiple-tests-sanitized"
