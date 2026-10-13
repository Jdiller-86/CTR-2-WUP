CC ?= gcc
ROOT := ../..
OUT := $(ROOT)/artifacts/relay-tests
FLAGS := -std=c11 -O2 -g -Wall -Wextra -Wpedantic -Werror -I$(ROOT)/runtime/core
SOURCES := test_relay.c $(addprefix $(ROOT)/runtime/core/,ctr_cpu.c ctr_memory.c ctr_guest.c ctr_ipc.c ctr_heap.c ctr_kernel.c ctr_hid.c ctr_gsp.c ctr_gx.c ctr_guest_pica.c ctr_scene.c ctr_shader.c ctr_vertex.c ctr_pica.c ctr_transfer.c ctr_fs.c ctr_apt.c ctr_hid_fixture.c ctr_gsp_fixture.c ctr_relay_fixture.c) \
           $(ROOT)/tests/fixtures/hid/hid.generated.c $(ROOT)/tests/fixtures/hid/hid-image.generated.c \
           $(ROOT)/tests/fixtures/gsp/gsp.generated.c $(ROOT)/tests/fixtures/gsp/gsp-image.generated.c \
           $(ROOT)/tests/fixtures/relay/relay.generated.c $(ROOT)/tests/fixtures/relay/relay-image.generated.c
.PHONY: check sanitize
$(OUT):
	mkdir -p "$@"
$(OUT)/relay-tests: $(SOURCES) $(wildcard $(ROOT)/runtime/core/*.h) | $(OUT)
	$(CC) $(FLAGS) $(SOURCES) -o "$@"
check: $(OUT)/relay-tests
	"$(OUT)/relay-tests"
sanitize: | $(OUT)
	$(CC) $(FLAGS) -O1 -fno-omit-frame-pointer -fsanitize=address,undefined $(SOURCES) -o "$(OUT)/relay-tests-sanitized"
	ASAN_OPTIONS=detect_leaks=1 "$(OUT)/relay-tests-sanitized"
