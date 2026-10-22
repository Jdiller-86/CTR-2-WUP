CC ?= cc
ROOT := ../..
OUT := $(ROOT)/artifacts/gx2-shader-tests
FLAGS := -std=c11 -O2 -g -Wall -Wextra -Wpedantic -Werror -I$(ROOT)/runtime/core
SOURCES := test_gx2_shader.c $(ROOT)/runtime/core/ctr_gx2_shader.c $(ROOT)/runtime/core/ctr_shader.c
HEADERS := $(ROOT)/runtime/core/ctr_gx2_shader.h $(ROOT)/runtime/core/ctr_shader.h $(ROOT)/runtime/core/ctr_vertex.h
CACHE_SOURCES := test_cache.c $(ROOT)/runtime/core/ctr_gx2_shader.c $(ROOT)/runtime/core/ctr_shader.c $(ROOT)/runtime/core/ctr_tev.c
CACHE_HEADERS := $(HEADERS) $(ROOT)/runtime/core/ctr_gx2_cache.h $(ROOT)/runtime/core/ctr_tev.h
CACHE_WRAP := -Dcalloc=test_cache_calloc -Dfree=test_cache_free
.PHONY: check sanitize
$(OUT):
	mkdir -p "$@"
$(OUT)/gx2-shader: $(SOURCES) $(HEADERS) | $(OUT)
	$(CC) $(FLAGS) $(SOURCES) -o "$@"
$(OUT)/cache.o: $(ROOT)/runtime/core/ctr_gx2_cache.c $(CACHE_HEADERS) | $(OUT)
	$(CC) $(FLAGS) $(CACHE_WRAP) -c "$<" -o "$@"
$(OUT)/cache: $(CACHE_SOURCES) $(CACHE_HEADERS) $(OUT)/cache.o | $(OUT)
	$(CC) $(FLAGS) $(CACHE_SOURCES) $(OUT)/cache.o -o "$@"
check: $(OUT)/gx2-shader $(OUT)/cache
	"$(OUT)/gx2-shader"
	"$(OUT)/cache"
sanitize: | $(OUT)
	$(CC) $(FLAGS) -O1 -fno-omit-frame-pointer -fsanitize=address,undefined $(SOURCES) -o "$(OUT)/gx2-shader-san"
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$(OUT)/gx2-shader-san"
	$(CC) $(FLAGS) -O1 -fno-omit-frame-pointer -fsanitize=address,undefined $(CACHE_WRAP) -c $(ROOT)/runtime/core/ctr_gx2_cache.c -o "$(OUT)/cache-san.o"
	$(CC) $(FLAGS) -O1 -fno-omit-frame-pointer -fsanitize=address,undefined $(CACHE_SOURCES) "$(OUT)/cache-san.o" -o "$(OUT)/cache-san"
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$(OUT)/cache-san"
