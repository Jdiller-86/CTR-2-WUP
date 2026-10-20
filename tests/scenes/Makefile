CC ?= cc
ROOT := ../..
OUT := $(ROOT)/artifacts/scene-tests
FLAGS := -std=c11 -O2 -g -Wall -Wextra -Wpedantic -Werror -I$(ROOT)/runtime/core
SOURCES := test_scene.c $(ROOT)/runtime/core/ctr_scene.c $(ROOT)/runtime/core/ctr_fragment.c $(ROOT)/runtime/core/ctr_tev.c $(ROOT)/runtime/core/ctr_shader.c \
    $(ROOT)/runtime/core/ctr_scene_fixture.c $(ROOT)/runtime/core/ctr_vertex.c $(ROOT)/runtime/core/ctr_pica.c $(ROOT)/runtime/core/ctr_texture.c
HEADERS := $(ROOT)/runtime/core/ctr_scene.h $(ROOT)/runtime/core/ctr_fragment.h $(ROOT)/runtime/core/ctr_tev.h $(ROOT)/runtime/core/ctr_shader.h $(ROOT)/runtime/core/ctr_scene_fixture.h \
           $(ROOT)/runtime/core/ctr_vertex.h $(ROOT)/runtime/core/ctr_pica.h $(ROOT)/runtime/core/ctr_texture.h
.PHONY: check sanitize
$(OUT):
	mkdir -p "$@"
$(OUT)/scene: $(SOURCES) $(HEADERS) | $(OUT)
	$(CC) $(FLAGS) $(SOURCES) -lm -o "$@"
check: $(OUT)/scene
	"$(OUT)/scene"
sanitize: | $(OUT)
	$(CC) $(FLAGS) -O1 -fno-omit-frame-pointer -fsanitize=address,undefined $(SOURCES) -lm -o "$(OUT)/scene-san"
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$(OUT)/scene-san"
