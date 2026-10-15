CC ?= cc
CFLAGS := -std=c11 -O2 -g -Wall -Wextra -Wpedantic -Werror -I../../runtime/wiiu
SOURCES := test_m1_storage.c ../../runtime/wiiu/m1_storage_probe.c
HEADERS := ../../runtime/wiiu/m1_storage_probe.h
OUT := ../../artifacts/m1-storage-tests
.PHONY: check sanitize clean
$(OUT):
	mkdir -p "$@"
$(OUT)/m1-storage-test: $(SOURCES) $(HEADERS) | $(OUT)
	$(CC) $(CFLAGS) $(SOURCES) -o "$@"
$(OUT)/m1-storage-test-san: $(SOURCES) $(HEADERS) | $(OUT)
	$(CC) $(CFLAGS) -O1 -fno-omit-frame-pointer -fsanitize=address,undefined $(SOURCES) -o "$@"
check: $(OUT)/m1-storage-test
	"$(OUT)/m1-storage-test"
sanitize: $(OUT)/m1-storage-test-san
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$(OUT)/m1-storage-test-san"
clean:
	rm -f "$(OUT)/m1-storage-test" "$(OUT)/m1-storage-test-san"
