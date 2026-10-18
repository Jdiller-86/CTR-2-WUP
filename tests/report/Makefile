CC ?= cc
CFLAGS := -std=c11 -O2 -g -Wall -Wextra -Werror -Istubs -I../../artifacts/report-tests/sdk-stubs -I../../runtime/wiiu -I../../runtime/core
SOURCES := test_report.c test_audio_stream_app.c test_installed_frontend.c ../../runtime/wiiu/audio_stream_app.c ../../runtime/wiiu/test_report.c ../../runtime/wiiu/test_guide.c ../../runtime/wiiu/test_lifecycle.c ../../runtime/wiiu/image_scale.c ../../runtime/wiiu/viewport.c ../../runtime/wiiu/report_storage.c ../../runtime/wiiu/test_result.c ../../runtime/wiiu/guest_slice.c ../../runtime/core/ctr_sha256.c
OUT := ../../artifacts/report-tests
HEADERS := $(wildcard ../../runtime/wiiu/*.h) stubs/coreinit/filesystem_fsa.h native_platform.h ../../runtime/wiiu/install_check.c
.PHONY: check sanitize clean
$(OUT):
	mkdir -p "$@"
$(OUT)/sdk-stubs/ready: native_platform.h | $(OUT)
	mkdir -p $(OUT)/sdk-stubs/coreinit $(OUT)/sdk-stubs/proc_ui $(OUT)/sdk-stubs/sysapp $(OUT)/sdk-stubs/whb $(OUT)/sdk-stubs/vpad
	touch $(addprefix $(OUT)/sdk-stubs/,coreinit/debug.h coreinit/foreground.h coreinit/systeminfo.h coreinit/title.h coreinit/thread.h coreinit/time.h proc_ui/procui.h sysapp/launch.h whb/proc.h vpad/input.h) "$@"
$(OUT)/report-test: $(SOURCES) $(HEADERS) ../test_platform.h $(OUT)/sdk-stubs/ready | $(OUT)
	$(CC) $(CFLAGS) $(SOURCES) -o "$@"
$(OUT)/report-test-san: $(SOURCES) $(HEADERS) ../test_platform.h $(OUT)/sdk-stubs/ready | $(OUT)
	$(CC) $(CFLAGS) -O1 -fno-omit-frame-pointer -fsanitize=address,undefined $(SOURCES) -o "$@"
check: $(OUT)/report-test
	"$(OUT)/report-test"
sanitize: $(OUT)/report-test-san
	"$(OUT)/report-test-san"
clean:
	rm -f "$(OUT)/report-test" "$(OUT)/report-test-san"
