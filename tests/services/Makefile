CC ?= gcc
ROOT := ../..
OUT := $(ROOT)/artifacts/service-tests
FLAGS := -std=c11 -O2 -g -Wall -Wextra -Wpedantic -Werror -I$(ROOT)/runtime/core
SOURCES := test_services.c $(ROOT)/runtime/core/ctr_cpu.c $(ROOT)/runtime/core/ctr_memory.c \
           $(ROOT)/runtime/core/ctr_guest.c $(ROOT)/runtime/core/ctr_ipc.c $(ROOT)/runtime/core/ctr_heap.c \
           $(ROOT)/runtime/core/ctr_service_fixture.c \
           $(ROOT)/tests/fixtures/services/boot.generated.c $(ROOT)/tests/fixtures/services/boot-image.generated.c
.PHONY: check sanitize
$(OUT):
	mkdir -p "$@"
$(OUT)/service-tests: $(SOURCES) $(wildcard $(ROOT)/runtime/core/*.h) | $(OUT)
	$(CC) $(FLAGS) $(SOURCES) -o "$@"
check: $(OUT)/service-tests
	"$(OUT)/service-tests"
sanitize: | $(OUT)
	$(CC) $(FLAGS) -O1 -fno-omit-frame-pointer -fsanitize=address,undefined $(SOURCES) -o "$(OUT)/service-tests-sanitized"
	ASAN_OPTIONS=detect_leaks=1 "$(OUT)/service-tests-sanitized"
