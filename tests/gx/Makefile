CC ?= cc
ROOT := ../..
OUT := $(ROOT)/artifacts/gx-tests
FLAGS := -std=c11 -O2 -g -Wall -Wextra -Wpedantic -Werror -I$(ROOT)/runtime/core
CORE := ctr_memory ctr_cpu ctr_guest ctr_ipc ctr_heap ctr_kernel ctr_hid ctr_gsp ctr_gx ctr_guest_pica ctr_scene ctr_shader ctr_vertex ctr_pica ctr_transfer ctr_fs ctr_apt ctr_guest_fixture
SOURCES := test_gx.c $(addprefix $(ROOT)/runtime/core/,$(addsuffix .c,$(CORE))) $(ROOT)/tests/fixtures/guest/framebuffer.generated.c $(ROOT)/tests/fixtures/guest/image.generated.c
.PHONY: check sanitize
$(OUT):
	mkdir -p "$@"
$(OUT)/gx: $(SOURCES) $(wildcard $(ROOT)/runtime/core/*.h) | $(OUT)
	$(CC) $(FLAGS) $(SOURCES) -o "$@"
check: $(OUT)/gx
	"$(OUT)/gx"
sanitize: | $(OUT)
	$(CC) $(FLAGS) -O1 -fno-omit-frame-pointer -fsanitize=address,undefined $(SOURCES) -o "$(OUT)/gx-san"
	ASAN_OPTIONS=detect_leaks=1 "$(OUT)/gx-san"
