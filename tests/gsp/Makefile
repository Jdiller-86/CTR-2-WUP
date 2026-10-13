CC ?= gcc
ROOT := ../..
include $(ROOT)/runtime/core/sources.mk
OUT := $(ROOT)/artifacts/gsp-tests
FLAGS := -std=c11 -O2 -g -Wall -Wextra -Wpedantic -Werror -I$(ROOT)/runtime/core
SOURCES := $(CTR_RUNTIME_SOURCES) test_gsp.c $(addprefix $(ROOT)/runtime/core/,ctr_hid_fixture.c ctr_gsp_fixture.c) \
           $(ROOT)/tests/fixtures/hid/hid.generated.c $(ROOT)/tests/fixtures/hid/hid-image.generated.c \
           $(ROOT)/tests/fixtures/gsp/gsp.generated.c $(ROOT)/tests/fixtures/gsp/gsp-image.generated.c
.PHONY: check sanitize
$(OUT):
	mkdir -p "$@"
$(OUT)/gsp-tests: $(SOURCES) $(CTR_RUNTIME_MANIFEST) $(wildcard $(ROOT)/runtime/core/*.h) | $(OUT)
	$(CC) $(FLAGS) $(SOURCES) -o "$@"
check: $(OUT)/gsp-tests
	"$(OUT)/gsp-tests"
sanitize: | $(OUT)
	$(CC) $(FLAGS) -O1 -fno-omit-frame-pointer -fsanitize=address,undefined $(SOURCES) -o "$(OUT)/gsp-tests-sanitized"
	ASAN_OPTIONS=detect_leaks=1 "$(OUT)/gsp-tests-sanitized"
