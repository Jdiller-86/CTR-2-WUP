CC ?= cc
CFLAGS := -std=c11 -O2 -g -Wall -Wextra -Wpedantic -Werror -I../../runtime/core
SOURCES := test_cstm.c ../../runtime/core/ctr_cstm.c ../../runtime/core/ctr_dsp_adpcm.c
HEADERS := ../../runtime/core/ctr_cstm.h ../../runtime/core/ctr_dsp_adpcm.h
OUT := ../../artifacts/cstm-tests
.PHONY: check sanitize clean
$(OUT):
	mkdir -p "$@"
$(OUT)/cstm-test: $(SOURCES) $(HEADERS) | $(OUT)
	$(CC) $(CFLAGS) $(SOURCES) -o "$@"
$(OUT)/cstm-test-san: $(SOURCES) $(HEADERS) | $(OUT)
	$(CC) $(CFLAGS) -O1 -fno-omit-frame-pointer -fsanitize=address,undefined $(SOURCES) -o "$@"
check: $(OUT)/cstm-test
	"$(OUT)/cstm-test"
sanitize: $(OUT)/cstm-test-san
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$(OUT)/cstm-test-san"
clean:
	rm -f "$(OUT)/cstm-test" "$(OUT)/cstm-test-san"
