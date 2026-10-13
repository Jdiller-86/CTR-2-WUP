CC ?= cc
ROOT := ../..
include $(ROOT)/runtime/core/sources.mk
OUT := ../../artifacts/recomp-progress-tests
FLAGS := -std=c11 -O2 -g -Wall -Wextra -Werror -I../../runtime/core -I../../runtime/recomp
CORE := 
SOURCES := $(CTR_RUNTIME_SOURCES) test_progress.c $(CORE) ../../runtime/recomp/ctr_recomp.c
HEADERS := $(wildcard ../../runtime/core/*.h ../../runtime/recomp/*.h)
.PHONY: check sanitize
$(OUT):
	mkdir -p "$@"
$(OUT)/progress-test: $(SOURCES) $(CTR_RUNTIME_MANIFEST) $(HEADERS) | $(OUT)
	$(CC) $(FLAGS) $(SOURCES) -lm -o "$@"
$(OUT)/progress-test-san: $(SOURCES) $(CTR_RUNTIME_MANIFEST) $(HEADERS) | $(OUT)
	$(CC) $(FLAGS) -O1 -fno-omit-frame-pointer -fsanitize=address,undefined $(SOURCES) -lm -o "$@"
check: $(OUT)/progress-test
	"$(OUT)/progress-test"
sanitize: $(OUT)/progress-test-san
	"$(OUT)/progress-test-san"
