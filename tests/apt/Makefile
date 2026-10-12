CC ?= gcc
ROOT := ../..
OUT := $(ROOT)/artifacts/apt-tests
FLAGS := -std=c11 -O2 -g -Wall -Wextra -Wpedantic -Werror -I$(ROOT)/runtime/core
SOURCES := test_apt.c $(addprefix $(ROOT)/runtime/core/,ctr_cpu.c ctr_memory.c ctr_guest.c ctr_ipc.c ctr_heap.c ctr_kernel.c ctr_hid.c ctr_gsp.c ctr_gx.c ctr_pica.c ctr_transfer.c ctr_fs.c ctr_apt.c ctr_service_fixture.c) $(ROOT)/tests/fixtures/services/boot.generated.c $(ROOT)/tests/fixtures/services/boot-image.generated.c
.PHONY: check sanitize
$(OUT):
	mkdir -p "$@"
$(OUT)/apt-tests: $(SOURCES) $(wildcard $(ROOT)/runtime/core/*.h) | $(OUT)
	$(CC) $(FLAGS) $(SOURCES) -o "$@"
check: $(OUT)/apt-tests
	"$(OUT)/apt-tests"
sanitize: | $(OUT)
	$(CC) $(FLAGS) -O1 -fno-omit-frame-pointer -fsanitize=address,undefined $(SOURCES) -o "$(OUT)/apt-tests-sanitized"
	ASAN_OPTIONS=detect_leaks=1 "$(OUT)/apt-tests-sanitized"
