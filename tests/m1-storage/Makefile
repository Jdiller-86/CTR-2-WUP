CC ?= cc
ROOT := ../..
include $(ROOT)/runtime/core/sources.mk
CFLAGS := -std=c11 -O2 -g -Wall -Wextra -Wpedantic -Werror -I../../runtime/wiiu -I../../runtime/core
SOURCES := test_m1_storage.c ../../runtime/wiiu/m1_storage_probe.c
HEADERS := ../../runtime/wiiu/m1_storage_probe.h
SNAPSHOT_SOURCES := test_m1_snapshot.c ../../runtime/wiiu/m1_snapshot_probe.c ../../runtime/core/ctr_fs_snapshot.c $(CTR_RUNTIME_SOURCES)
SNAPSHOT_HEADERS := ../../runtime/wiiu/m1_snapshot_probe.h $(wildcard ../../runtime/core/*.h) $(CTR_RUNTIME_MANIFEST)
OUT := ../../artifacts/m1-storage-tests
.PHONY: check sanitize clean
$(OUT):
	mkdir -p "$@"
$(OUT)/m1-storage-test: $(SOURCES) $(HEADERS) | $(OUT)
	$(CC) $(CFLAGS) $(SOURCES) -o "$@"
$(OUT)/m1-storage-test-san: $(SOURCES) $(HEADERS) | $(OUT)
	$(CC) $(CFLAGS) -O1 -fno-omit-frame-pointer -fsanitize=address,undefined $(SOURCES) -o "$@"
$(OUT)/m1-snapshot-test: $(SNAPSHOT_SOURCES) $(SNAPSHOT_HEADERS) $(HEADERS) | $(OUT)
	$(CC) $(CFLAGS) $(SNAPSHOT_SOURCES) -o "$@"
$(OUT)/m1-snapshot-test-san: $(SNAPSHOT_SOURCES) $(SNAPSHOT_HEADERS) $(HEADERS) | $(OUT)
	$(CC) $(CFLAGS) -O1 -fno-omit-frame-pointer -fsanitize=address,undefined $(SNAPSHOT_SOURCES) -o "$@"
check: $(OUT)/m1-storage-test $(OUT)/m1-snapshot-test
	"$(OUT)/m1-storage-test"
	"$(OUT)/m1-snapshot-test"
sanitize: $(OUT)/m1-storage-test-san $(OUT)/m1-snapshot-test-san
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$(OUT)/m1-storage-test-san"
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$(OUT)/m1-snapshot-test-san"
clean:
	rm -f "$(OUT)/m1-storage-test" "$(OUT)/m1-storage-test-san" "$(OUT)/m1-snapshot-test" "$(OUT)/m1-snapshot-test-san"
