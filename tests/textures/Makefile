CC ?= cc
FLAGS := -std=c11 -O2 -g -Wall -Wextra -Wpedantic -Werror -I../../runtime/core
OUT := ../../artifacts/texture-tests
SOURCES := test_textures.c ../../runtime/core/ctr_texture.c
.PHONY: check sanitize
$(OUT):
	mkdir -p "$@"
$(OUT)/textures: $(SOURCES) | $(OUT)
	$(CC) $(FLAGS) $(SOURCES) -o "$@"
check: $(OUT)/textures
	"$(OUT)/textures"
sanitize: | $(OUT)
	$(CC) $(FLAGS) -O1 -fno-omit-frame-pointer -fsanitize=address,undefined $(SOURCES) -o "$(OUT)/textures-san"
	ASAN_OPTIONS=detect_leaks=1 "$(OUT)/textures-san"
