CC ?= cc
CFLAGS := -std=c11 -O2 -g -Wall -Wextra -Wpedantic -Werror -I../../runtime/wiiu -I../../runtime/core
SOURCES := test_install_capture.c ../../runtime/wiiu/install_capture.c ../../runtime/core/ctr_sha256.c
HEADERS := ../../runtime/wiiu/install_capture.h ../../runtime/core/ctr_sha256.h
OUT := ../../artifacts/install-capture-tests
.PHONY: check sanitize
$(OUT):
	mkdir -p "$@"
$(OUT)/capture: $(SOURCES) $(HEADERS) | $(OUT)
	$(CC) $(CFLAGS) $(SOURCES) -o "$@"
$(OUT)/capture-san: $(SOURCES) $(HEADERS) | $(OUT)
	$(CC) $(CFLAGS) -O1 -fno-omit-frame-pointer -fsanitize=address,undefined $(SOURCES) -o "$@"
check: $(OUT)/capture
	"$(OUT)/capture"
sanitize: $(OUT)/capture-san
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$(OUT)/capture-san"
