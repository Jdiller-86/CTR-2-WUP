CC ?= cc
FLAGS := -std=c11 -O2 -g -Wall -Wextra -Wpedantic -Werror -I../../runtime/core
OUT := ../../artifacts/resource-cache-tests
SOURCES := test_resource_cache.c ../../runtime/core/ctr_resource_cache.c ../../runtime/core/ctr_memory.c
HEADERS := ../../runtime/core/ctr_resource_cache.h ../../runtime/core/ctr_memory.h
.PHONY: check sanitize
$(OUT):
	mkdir -p "$@"
$(OUT)/resource-cache: $(SOURCES) $(HEADERS) | $(OUT)
	$(CC) $(FLAGS) $(SOURCES) -o "$@"
check: $(OUT)/resource-cache
	"$(OUT)/resource-cache"
sanitize: | $(OUT)
	$(CC) $(FLAGS) -O1 -fno-omit-frame-pointer -fsanitize=address,undefined $(SOURCES) -o "$(OUT)/resource-cache-san"
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$(OUT)/resource-cache-san"
