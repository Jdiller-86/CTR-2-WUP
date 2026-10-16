CC ?= cc
ROOT := ../..
OUT := $(ROOT)/artifacts/gx2-shader-tests
FLAGS := -std=c11 -O2 -g -Wall -Wextra -Wpedantic -Werror -I$(ROOT)/runtime/core
SOURCES := test_gx2_shader.c $(ROOT)/runtime/core/ctr_gx2_shader.c $(ROOT)/runtime/core/ctr_shader.c
HEADERS := $(ROOT)/runtime/core/ctr_gx2_shader.h $(ROOT)/runtime/core/ctr_shader.h $(ROOT)/runtime/core/ctr_vertex.h
.PHONY: check sanitize
$(OUT):
	mkdir -p "$@"
$(OUT)/gx2-shader: $(SOURCES) $(HEADERS) | $(OUT)
	$(CC) $(FLAGS) $(SOURCES) -o "$@"
check: $(OUT)/gx2-shader
	"$(OUT)/gx2-shader"
sanitize: | $(OUT)
	$(CC) $(FLAGS) -O1 -fno-omit-frame-pointer -fsanitize=address,undefined $(SOURCES) -o "$(OUT)/gx2-shader-san"
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$(OUT)/gx2-shader-san"
