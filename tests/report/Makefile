CC ?= cc
CFLAGS := -std=c11 -O2 -g -Wall -Wextra -Werror -Istubs -I../../runtime/wiiu
SOURCES := test_report.c ../../runtime/wiiu/test_report.c ../../runtime/wiiu/test_guide.c ../../runtime/wiiu/test_lifecycle.c ../../runtime/wiiu/image_scale.c ../../runtime/wiiu/viewport.c ../../runtime/wiiu/report_storage.c ../../runtime/wiiu/test_result.c
OUT := ../../artifacts/report-tests
HEADERS := $(wildcard ../../runtime/wiiu/*.h) stubs/coreinit/filesystem_fsa.h
.PHONY: check sanitize clean
$(OUT):
	mkdir -p "$@"
$(OUT)/report-test: $(SOURCES) $(HEADERS) ../test_platform.h | $(OUT)
	$(CC) $(CFLAGS) $(SOURCES) -o "$@"
$(OUT)/report-test-san: $(SOURCES) $(HEADERS) ../test_platform.h | $(OUT)
	$(CC) $(CFLAGS) -O1 -fno-omit-frame-pointer -fsanitize=address,undefined $(SOURCES) -o "$@"
check: $(OUT)/report-test
	"$(OUT)/report-test"
sanitize: $(OUT)/report-test-san
	"$(OUT)/report-test-san"
clean:
	rm -f "$(OUT)/report-test" "$(OUT)/report-test-san"
