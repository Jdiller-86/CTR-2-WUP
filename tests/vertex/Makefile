CC ?= cc
ROOT := ../..
OUT := $(ROOT)/artifacts/vertex-tests
FLAGS := -std=c11 -O2 -g -Wall -Wextra -Wpedantic -Werror -I$(ROOT)/runtime/core
SOURCES := test_vertex.c $(ROOT)/runtime/core/ctr_vertex.c
.PHONY: check sanitize
$(OUT):
	mkdir -p "$@"
$(OUT)/vertex: $(SOURCES) $(ROOT)/runtime/core/ctr_vertex.h | $(OUT)
	$(CC) $(FLAGS) $(SOURCES) -o "$@"
check: $(OUT)/vertex
	"$(OUT)/vertex"
sanitize: | $(OUT)
	$(CC) $(FLAGS) -O1 -fno-omit-frame-pointer -fsanitize=address,undefined $(SOURCES) -o "$(OUT)/vertex-san"
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$(OUT)/vertex-san"
