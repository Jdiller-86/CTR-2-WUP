CC ?= cc
ROOT := ../..
include $(ROOT)/runtime/core/sources.mk
FLAGS := -std=c11 -O2 -g -Wall -Wextra -Wpedantic -Werror -I../../runtime/core
OUT := ../../artifacts/asset-tests
CORE := ctr_sha256 ctr_romfs_file
SOURCES := $(CTR_RUNTIME_SOURCES) test_assets.c $(addprefix ../../runtime/core/,$(addsuffix .c,$(CORE)))
.PHONY: check sanitize
$(OUT):
	mkdir -p "$@"
$(OUT)/assets: $(SOURCES) ../test_platform.h | $(OUT)
	$(CC) $(FLAGS) $(SOURCES) -o "$@"
check: $(OUT)/assets
	"$(OUT)/assets"
sanitize: | $(OUT)
	$(CC) $(FLAGS) -O1 -fno-omit-frame-pointer -fsanitize=address,undefined $(SOURCES) -o "$(OUT)/assets-san"
	ASAN_OPTIONS=detect_leaks=1 "$(OUT)/assets-san"
