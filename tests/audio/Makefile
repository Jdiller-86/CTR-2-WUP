CC ?= cc
CFLAGS := -std=c11 -O2 -g -Wall -Wextra -Werror -Istubs -I../../runtime/wiiu
SOURCES := test_audio.c ../../runtime/wiiu/diagnostic_audio.c
HEADERS := ../../runtime/wiiu/diagnostic_audio.h stubs/sndcore2/core.h
OUT := ../../artifacts/audio-tests
.PHONY: check sanitize clean
$(OUT):
	mkdir -p "$@"
$(OUT)/audio-test: $(SOURCES) $(HEADERS) | $(OUT)
	$(CC) $(CFLAGS) $(SOURCES) -o "$@"
$(OUT)/audio-test-san: $(SOURCES) $(HEADERS) | $(OUT)
	$(CC) $(CFLAGS) -O1 -fno-omit-frame-pointer -fsanitize=address,undefined $(SOURCES) -o "$@"
check: $(OUT)/audio-test
	"$(OUT)/audio-test"
sanitize: $(OUT)/audio-test-san
	"$(OUT)/audio-test-san"
clean:
	rm -f "$(OUT)/audio-test" "$(OUT)/audio-test-san"
