CC ?= cc
CFLAGS := -std=c11 -O2 -g -Wall -Wextra -Wpedantic -Werror -I../../runtime/core
SOURCES := test_audio_engine.c ../../runtime/core/ctr_audio.c ../../runtime/core/ctr_cstm.c ../../runtime/core/ctr_dsp_adpcm.c
HEADERS := ../../runtime/core/ctr_audio.h ../../runtime/core/ctr_cstm.h ../../runtime/core/ctr_dsp_adpcm.h
OUT := ../../artifacts/audio-engine-tests
.PHONY: check sanitize clean
$(OUT):
	mkdir -p "$@"
$(OUT)/audio-engine-test: $(SOURCES) $(HEADERS) | $(OUT)
	$(CC) $(CFLAGS) $(SOURCES) -o "$@"
$(OUT)/audio-engine-test-san: $(SOURCES) $(HEADERS) | $(OUT)
	$(CC) $(CFLAGS) -O1 -fno-omit-frame-pointer -fsanitize=address,undefined $(SOURCES) -o "$@"
check: $(OUT)/audio-engine-test
	"$(OUT)/audio-engine-test"
sanitize: $(OUT)/audio-engine-test-san
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$(OUT)/audio-engine-test-san"
clean:
	rm -f "$(OUT)/audio-engine-test" "$(OUT)/audio-engine-test-san"
