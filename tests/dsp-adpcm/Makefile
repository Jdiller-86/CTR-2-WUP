CC ?= cc
CFLAGS := -std=c11 -O2 -g -Wall -Wextra -Wpedantic -Werror -I../../runtime/core
SOURCES := test_dsp_adpcm.c ../../runtime/core/ctr_dsp_adpcm.c
HEADERS := ../../runtime/core/ctr_dsp_adpcm.h
OUT := ../../artifacts/dsp-adpcm-tests
.PHONY: check sanitize clean
$(OUT):
	mkdir -p "$@"
$(OUT)/dsp-test: $(SOURCES) $(HEADERS) | $(OUT)
	$(CC) $(CFLAGS) $(SOURCES) -o "$@"
$(OUT)/dsp-test-san: $(SOURCES) $(HEADERS) | $(OUT)
	$(CC) $(CFLAGS) -O1 -fno-omit-frame-pointer -fsanitize=address,undefined $(SOURCES) -o "$@"
check: $(OUT)/dsp-test
	"$(OUT)/dsp-test"
sanitize: $(OUT)/dsp-test-san
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$(OUT)/dsp-test-san"
clean:
	rm -f "$(OUT)/dsp-test" "$(OUT)/dsp-test-san"
