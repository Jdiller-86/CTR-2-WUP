CC ?= gcc
ROOT := ../..
OUT := $(ROOT)/artifacts/kernel-tests
FLAGS := -std=c11 -O2 -g -Wall -Wextra -Wpedantic -Werror -I$(ROOT)/runtime/core
SOURCES := test_kernel.c $(ROOT)/runtime/core/ctr_cpu.c $(ROOT)/runtime/core/ctr_memory.c \
           $(ROOT)/runtime/core/ctr_guest.c $(ROOT)/runtime/core/ctr_ipc.c $(ROOT)/runtime/core/ctr_heap.c \
           $(ROOT)/runtime/core/ctr_kernel.c $(ROOT)/runtime/core/ctr_hid.c $(ROOT)/runtime/core/ctr_gsp.c $(ROOT)/runtime/core/ctr_gx.c $(ROOT)/runtime/core/ctr_guest_pica.c $(ROOT)/runtime/core/ctr_scene.c $(ROOT)/runtime/core/ctr_shader.c $(ROOT)/runtime/core/ctr_vertex.c $(ROOT)/runtime/core/ctr_pica.c $(ROOT)/runtime/core/ctr_transfer.c $(ROOT)/runtime/core/ctr_fs.c $(ROOT)/runtime/core/ctr_apt.c $(ROOT)/runtime/core/ctr_kernel_fixture.c \
           $(ROOT)/tests/fixtures/kernel/kernel.generated.c $(ROOT)/tests/fixtures/kernel/kernel-image.generated.c
.PHONY: check sanitize
$(OUT):
	mkdir -p "$@"
$(OUT)/kernel-tests: $(SOURCES) $(wildcard $(ROOT)/runtime/core/*.h) | $(OUT)
	$(CC) $(FLAGS) $(SOURCES) -o "$@"
check: $(OUT)/kernel-tests
	"$(OUT)/kernel-tests"
sanitize: | $(OUT)
	$(CC) $(FLAGS) -O1 -fno-omit-frame-pointer -fsanitize=address,undefined $(SOURCES) -o "$(OUT)/kernel-tests-sanitized"
	ASAN_OPTIONS=detect_leaks=1 "$(OUT)/kernel-tests-sanitized"
