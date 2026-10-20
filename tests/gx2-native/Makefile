CC ?= cc
ROOT := ../..
OUT := $(ROOT)/artifacts/gx2-native-tests
FLAGS := -std=c11 -O2 -g -Wall -Wextra -Wpedantic -Werror -I$(ROOT)/runtime/core -I$(ROOT)/runtime/wiiu -I.
SOURCE := $(ROOT)/runtime/wiiu/gx2_scene.c
HEADERS := gx2_mock.h $(ROOT)/runtime/wiiu/gx2_scene.h $(ROOT)/runtime/core/ctr_gx2_shader.h $(ROOT)/runtime/core/ctr_scene.h $(ROOT)/runtime/core/ctr_tev.h $(ROOT)/runtime/core/ctr_fragment.h
FRAGMENT := $(ROOT)/runtime/core/ctr_tev.c $(ROOT)/runtime/core/ctr_fragment.c
MOCK := -DCTR_GX2_SCENE_MOCK -Dmemalign=ctr_gx2_test_memalign -Dfree=ctr_gx2_test_free
.PHONY: check sanitize
$(OUT):
	mkdir -p "$@"
$(OUT)/native.o: $(SOURCE) $(HEADERS) | $(OUT)
	$(CC) $(FLAGS) $(MOCK) -c "$(SOURCE)" -o "$@"
$(OUT)/native: test_native.c $(HEADERS) $(OUT)/native.o $(FRAGMENT)
	$(CC) $(FLAGS) test_native.c $(FRAGMENT) $(OUT)/native.o -o "$@"
check: $(OUT)/native
	"$(OUT)/native"
sanitize: | $(OUT)
	$(CC) $(FLAGS) -O1 -fno-omit-frame-pointer -fsanitize=address,undefined $(MOCK) -c "$(SOURCE)" -o "$(OUT)/native-san.o"
	$(CC) $(FLAGS) -O1 -fno-omit-frame-pointer -fsanitize=address,undefined test_native.c $(FRAGMENT) "$(OUT)/native-san.o" -o "$(OUT)/native-san"
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$(OUT)/native-san"
