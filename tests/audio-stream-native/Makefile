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
check: $(OUT)/native-pcm-test
	"$(OUT)/native-pcm-test"
sanitize: $(OUT)/native-pcm-test-san
	"$(OUT)/native-pcm-test-san"
clean:
	rm -f "$(OUT)/native-pcm-test" "$(OUT)/native-pcm-test-san"
