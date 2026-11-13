CC ?= cc
CFLAGS := -std=c11 -O2 -g -Wall -Wextra -Werror -Istubs -I../../runtime/wiiu
SOURCES := test_native_pcm.c ../../runtime/wiiu/audio_stream_diagnostic.c
HEADERS := ../../runtime/wiiu/audio_stream_diagnostic.h stubs/sndcore2/core.h stubs/sndcore2/voice.h stubs/coreinit/cache.h
OUT := ../../artifacts/audio-stream-native-tests
.PHONY: check sanitize clean
$(OUT):
	mkdir -p "$@"
$(OUT)/native-pcm-test: $(SOURCES) $(HEADERS) | $(OUT)
	$(CC) $(CFLAGS) $(SOURCES) -o "$@"
$(OUT)/native-pcm-test-san: $(SOURCES) $(HEADERS) | $(OUT)
	$(CC) $(CFLAGS) -O1 -fno-omit-frame-pointer -fsanitize=address,undefined $(SOURCES) -o "$@"
check: $(OUT)/native-pcm-test $(OUT)/native-stream-test
	"$(OUT)/native-pcm-test"
	"$(OUT)/native-stream-test"
sanitize: $(OUT)/native-pcm-test-san $(OUT)/native-stream-test-san
	"$(OUT)/native-pcm-test-san"
	"$(OUT)/native-stream-test-san"
STREAM_SOURCES := test_native_stream.c ../../runtime/wiiu/native_audio_stream.c ../../runtime/core/ctr_dsp_stream.c
STREAM_HEADERS := ../../runtime/wiiu/native_audio_stream.h ../../runtime/core/ctr_dsp_stream.h $(HEADERS)
$(OUT)/native-stream-test: $(STREAM_SOURCES) $(STREAM_HEADERS) | $(OUT)
	$(CC) $(CFLAGS) -I../../runtime/core $(STREAM_SOURCES) -o "$@"
$(OUT)/native-stream-test-san: $(STREAM_SOURCES) $(STREAM_HEADERS) | $(OUT)
	$(CC) $(CFLAGS) -I../../runtime/core -O1 -fno-omit-frame-pointer -fsanitize=address,undefined $(STREAM_SOURCES) -o "$@"
clean:
	rm -f "$(OUT)/native-pcm-test" "$(OUT)/native-pcm-test-san" "$(OUT)/native-stream-test" "$(OUT)/native-stream-test-san"
