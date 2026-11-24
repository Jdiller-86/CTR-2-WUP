CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Werror
CPPFLAGS = -Istubs -I../../runtime/wiiu -DCTR_GAME_CEMU_TEST -DCTR_SOURCE_COMMIT=\"test\"
SRCS = test_cemu_display.c ../../runtime/wiiu/diagnostic_display.c ../../runtime/wiiu/diagnostic_font.c ../../runtime/wiiu/viewport.c ../../runtime/wiiu/image_scale.c
OUT := ../../artifacts/cemu-display-tests

.PHONY: check sanitize clean

check: $(OUT)/test_cemu_display
	"$(OUT)/test_cemu_display"

sanitize: | $(OUT)
	$(CC) $(CFLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer $(CPPFLAGS) $(SRCS) -o "$(OUT)/test_cemu_display_san"
	"$(OUT)/test_cemu_display_san"

$(OUT):
	mkdir -p "$@"

$(OUT)/test_cemu_display: $(SRCS) | $(OUT)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(SRCS) -o "$@"

clean:
	rm -f "$(OUT)/test_cemu_display" "$(OUT)/test_cemu_display.exe" "$(OUT)/test_cemu_display_san" "$(OUT)/test_cemu_display_san.exe"
