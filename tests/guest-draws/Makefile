CC ?= cc
ROOT := ../..
include $(ROOT)/runtime/core/sources.mk
OUT := $(ROOT)/artifacts/guest-draw-tests
FLAGS := -std=c11 -O2 -g -Wall -Wextra -Wpedantic -Werror -I$(ROOT)/runtime/core
CORE := ctr_guest_fixture ctr_scene_fixture
SOURCES := $(CTR_RUNTIME_SOURCES) test_guest_draw.c $(addprefix $(ROOT)/runtime/core/,$(addsuffix .c,$(CORE))) $(ROOT)/tests/fixtures/guest/framebuffer.generated.c $(ROOT)/tests/fixtures/guest/image.generated.c
.PHONY: check sanitize
$(OUT):
	mkdir -p "$@"
$(OUT)/guest-draw: $(SOURCES) $(CTR_RUNTIME_MANIFEST) $(wildcard $(ROOT)/runtime/core/*.h) | $(OUT)
	$(CC) $(FLAGS) $(SOURCES) -lm -o "$@"
check: $(OUT)/guest-draw
	"$(OUT)/guest-draw"
sanitize: | $(OUT)
	$(CC) $(FLAGS) -O1 -fno-omit-frame-pointer -fsanitize=address,undefined $(SOURCES) -lm -o "$(OUT)/guest-draw-san"
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$(OUT)/guest-draw-san"
