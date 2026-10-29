CC ?= gcc
ROOT := ../..
include $(ROOT)/runtime/core/sources.mk
OUT := $(ROOT)/artifacts/guest-audio-native-tests
FLAGS := -I../report/stubs -I../../artifacts/report-tests/sdk-stubs -I../../runtime/wiiu -std=c11 -O2 -g -Wall -Wextra -Wpedantic -Werror -I$(ROOT)/runtime/core
SOURCES := $(CTR_RUNTIME_SOURCES) test_frontend.c \
    $(ROOT)/runtime/core/ctr_dsp_fixture.c \
    $(ROOT)/tests/fixtures/dsp/dsp.generated.c $(ROOT)/tests/fixtures/dsp/dsp-image.generated.c
SOURCES += ../../runtime/wiiu/test_report.c ../../runtime/wiiu/test_result.c ../../runtime/wiiu/test_lifecycle.c ../../runtime/core/ctr_sha256.c
HEADERS := ../../runtime/wiiu/guest_audio.c $(wildcard ../../runtime/wiiu/*.h)
SDK := ../../artifacts/report-tests/sdk-stubs/ready
$(SDK):
	$(MAKE) -C ../report $(SDK)
.PHONY: check sanitize
$(OUT):
	mkdir -p "$@"
$(OUT)/guest-audio-native-tests: $(SOURCES) $(HEADERS) $(SDK) $(CTR_RUNTIME_MANIFEST) $(wildcard $(ROOT)/runtime/core/*.h) | $(OUT)
	$(CC) $(FLAGS) $(SOURCES) -o "$@"
check: $(OUT)/guest-audio-native-tests
	"$(OUT)/guest-audio-native-tests"
	"$(OUT)/guest-audio-native-tests" --ctr-test-child happy .
	"$(OUT)/guest-audio-native-tests" --ctr-test-child slow-storage .
	@ctr_test_status=0; "$(OUT)/guest-audio-native-tests" --ctr-test-child || ctr_test_status=$$?; test "$$ctr_test_status" -eq 2
sanitize: | $(OUT)
	$(CC) $(FLAGS) -O1 -fno-omit-frame-pointer -fsanitize=address,undefined $(SOURCES) -o "$(OUT)/guest-audio-native-tests-sanitized"
	ASAN_OPTIONS=detect_leaks=1 "$(OUT)/guest-audio-native-tests-sanitized"
	ASAN_OPTIONS=detect_leaks=1 "$(OUT)/guest-audio-native-tests-sanitized" --ctr-test-child happy .
	ASAN_OPTIONS=detect_leaks=1 "$(OUT)/guest-audio-native-tests-sanitized" --ctr-test-child slow-storage .
	@ctr_test_status=0; ASAN_OPTIONS=detect_leaks=1 "$(OUT)/guest-audio-native-tests-sanitized" --ctr-test-child || ctr_test_status=$$?; test "$$ctr_test_status" -eq 2

ifneq ($(OS),Windows_NT)
$(OUT)/retained-tests: test_retained.c ../../runtime/wiiu/retained_logs.c ../../runtime/wiiu/retained_logs.h | $(OUT)
	$(CC) $(FLAGS) test_retained.c ../../runtime/wiiu/retained_logs.c -o "$@"
check: retained-check
retained-check: $(OUT)/retained-tests
	"$(OUT)/retained-tests"
sanitize: retained-sanitize
retained-sanitize: | $(OUT)
	$(CC) $(FLAGS) -O1 -fno-omit-frame-pointer -fsanitize=address,undefined test_retained.c ../../runtime/wiiu/retained_logs.c -o "$(OUT)/retained-tests-san"
	ASAN_OPTIONS=detect_leaks=1 "$(OUT)/retained-tests-san"
endif
