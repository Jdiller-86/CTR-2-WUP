CC ?= cc
ROOT := ../..
OUT := $(ROOT)/artifacts/pica-tests
FLAGS := -std=c11 -O2 -g -Wall -Wextra -Wpedantic -Werror -I$(ROOT)/runtime/core
SOURCES := test_pica.c $(ROOT)/runtime/core/ctr_pica.c
.PHONY: check sanitize
$(OUT):
	mkdir -p "$@"
$(OUT)/pica: $(SOURCES) $(ROOT)/runtime/core/ctr_pica.h | $(OUT)
	$(CC) $(FLAGS) $(SOURCES) -o "$@"
check: $(OUT)/pica
	"$(OUT)/pica"
sanitize: | $(OUT)
	$(CC) $(FLAGS) -O1 -fno-omit-frame-pointer -fsanitize=address,undefined $(SOURCES) -o "$(OUT)/pica-san"
	ASAN_OPTIONS=detect_leaks=1 "$(OUT)/pica-san"
