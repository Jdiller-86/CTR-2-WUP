CC ?= gcc
ROOT := ../..
include $(ROOT)/runtime/core/sources.mk
OUT := $(ROOT)/artifacts/guest-dsp-tests
FLAGS := -std=c11 -O2 -g -Wall -Wextra -Wpedantic -Werror -I$(ROOT)/runtime/core
SOURCES := $(CTR_RUNTIME_SOURCES) test_dsp.c \
    $(ROOT)/runtime/core/ctr_dsp_fixture.c \
    $(ROOT)/tests/fixtures/dsp/dsp.generated.c $(ROOT)/tests/fixtures/dsp/dsp-image.generated.c
.PHONY: check sanitize
$(OUT):
	mkdir -p "$@"
$(OUT)/guest-dsp-tests: $(SOURCES) $(CTR_RUNTIME_MANIFEST) $(wildcard $(ROOT)/runtime/core/*.h) | $(OUT)
	$(CC) $(FLAGS) $(SOURCES) -o "$@"
check: $(OUT)/guest-dsp-tests
	"$(OUT)/guest-dsp-tests"
sanitize: | $(OUT)
	$(CC) $(FLAGS) -O1 -fno-omit-frame-pointer -fsanitize=address,undefined $(SOURCES) -o "$(OUT)/guest-dsp-tests-sanitized"
	ASAN_OPTIONS=detect_leaks=1 "$(OUT)/guest-dsp-tests-sanitized"
