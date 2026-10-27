CC ?= cc
FLAGS := -std=c11 -O2 -g -Wall -Wextra -Wpedantic -Werror -I../../runtime/core
OUT := ../../artifacts/texture-tests
SOURCES := test_textures.c ../../runtime/core/ctr_texture.c
CACHE_SOURCES := test_cache.c ../../runtime/core/ctr_texture.c ../../runtime/core/ctr_texture_cache.c
HEADERS := ../../runtime/core/ctr_texture.h ../../runtime/core/ctr_texture_cache.h
.PHONY: check sanitize
$(OUT):
	mkdir -p "$@"
$(OUT)/textures: $(SOURCES) $(HEADERS) | $(OUT)
	$(CC) $(FLAGS) $(SOURCES) -o "$@"
$(OUT)/cache: $(CACHE_SOURCES) $(HEADERS) | $(OUT)
	$(CC) $(FLAGS) $(CACHE_SOURCES) -o "$@"
check: $(OUT)/textures $(OUT)/cache
	"$(OUT)/textures"
	"$(OUT)/cache"
sanitize: | $(OUT)
	$(CC) $(FLAGS) -O1 -fno-omit-frame-pointer -fsanitize=address,undefined $(SOURCES) -o "$(OUT)/textures-san"
	ASAN_OPTIONS=detect_leaks=1 "$(OUT)/textures-san"
	$(CC) $(FLAGS) -O1 -fno-omit-frame-pointer -fsanitize=address,undefined $(CACHE_SOURCES) -o "$(OUT)/cache-san"
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$(OUT)/cache-san"
