CC ?= gcc
ROOT := ../..
include $(ROOT)/runtime/core/sources.mk
OUT := $(ROOT)/artifacts/hid-tests
FLAGS := -std=c11 -O2 -g -Wall -Wextra -Wpedantic -Werror -I$(ROOT)/runtime/core
SOURCES := $(CTR_RUNTIME_SOURCES) test_hid.c $(addprefix $(ROOT)/runtime/core/,ctr_hid_fixture.c) \
    $(ROOT)/tests/fixtures/hid/hid.generated.c $(ROOT)/tests/fixtures/hid/hid-image.generated.c
.PHONY: check sanitize
$(OUT):
	mkdir -p "$@"
$(OUT)/hid-tests: $(SOURCES) $(CTR_RUNTIME_MANIFEST) $(wildcard $(ROOT)/runtime/core/*.h) | $(OUT)
	$(CC) $(FLAGS) $(SOURCES) -o "$@"
check: $(OUT)/hid-tests
	"$(OUT)/hid-tests"
sanitize: | $(OUT)
	$(CC) $(FLAGS) -O1 -fno-omit-frame-pointer -fsanitize=address,undefined $(SOURCES) -o "$(OUT)/hid-tests-sanitized"
	ASAN_OPTIONS=detect_leaks=1 "$(OUT)/hid-tests-sanitized"
