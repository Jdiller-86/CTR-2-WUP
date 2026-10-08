CC ?= gcc
ROOT := ../..
OUT := $(ROOT)/artifacts/hid-tests
FLAGS := -std=c11 -O2 -g -Wall -Wextra -Wpedantic -Werror -I$(ROOT)/runtime/core
SOURCES := test_hid.c $(addprefix $(ROOT)/runtime/core/,ctr_cpu.c ctr_memory.c ctr_guest.c ctr_ipc.c ctr_heap.c ctr_kernel.c ctr_hid.c ctr_gsp.c ctr_hid_fixture.c) \
           $(ROOT)/tests/fixtures/hid/hid.generated.c $(ROOT)/tests/fixtures/hid/hid-image.generated.c
.PHONY: check sanitize
$(OUT):
	mkdir -p "$@"
$(OUT)/hid-tests: $(SOURCES) $(wildcard $(ROOT)/runtime/core/*.h) | $(OUT)
	$(CC) $(FLAGS) $(SOURCES) -o "$@"
check: $(OUT)/hid-tests
	"$(OUT)/hid-tests"
sanitize: | $(OUT)
	$(CC) $(FLAGS) -O1 -fno-omit-frame-pointer -fsanitize=address,undefined $(SOURCES) -o "$(OUT)/hid-tests-sanitized"
	ASAN_OPTIONS=detect_leaks=1 "$(OUT)/hid-tests-sanitized"
