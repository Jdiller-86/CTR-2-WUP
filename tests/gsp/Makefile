CC ?= gcc
ROOT := ../..
OUT := $(ROOT)/artifacts/gsp-tests
FLAGS := -std=c11 -O2 -g -Wall -Wextra -Wpedantic -Werror -I$(ROOT)/runtime/core
SOURCES := test_gsp.c $(addprefix $(ROOT)/runtime/core/,ctr_cpu.c ctr_memory.c ctr_guest.c ctr_ipc.c ctr_heap.c ctr_kernel.c ctr_hid.c ctr_gsp.c ctr_gx.c ctr_pica.c ctr_transfer.c ctr_fs.c ctr_apt.c ctr_hid_fixture.c ctr_gsp_fixture.c) \
           $(ROOT)/tests/fixtures/hid/hid.generated.c $(ROOT)/tests/fixtures/hid/hid-image.generated.c \
           $(ROOT)/tests/fixtures/gsp/gsp.generated.c $(ROOT)/tests/fixtures/gsp/gsp-image.generated.c
.PHONY: check sanitize
$(OUT):
	mkdir -p "$@"
$(OUT)/gsp-tests: $(SOURCES) $(wildcard $(ROOT)/runtime/core/*.h) | $(OUT)
	$(CC) $(FLAGS) $(SOURCES) -o "$@"
check: $(OUT)/gsp-tests
	"$(OUT)/gsp-tests"
sanitize: | $(OUT)
	$(CC) $(FLAGS) -O1 -fno-omit-frame-pointer -fsanitize=address,undefined $(SOURCES) -o "$(OUT)/gsp-tests-sanitized"
	ASAN_OPTIONS=detect_leaks=1 "$(OUT)/gsp-tests-sanitized"
