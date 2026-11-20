CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Werror
CPPFLAGS = -Istubs -I../../runtime/wiiu -DCTR_GAME_CEMU_TEST -DCTR_SOURCE_COMMIT=\"test\"
SRCS = test_cemu_display.c ../../runtime/wiiu/diagnostic_display.c ../../runtime/wiiu/diagnostic_font.c ../../runtime/wiiu/viewport.c ../../runtime/wiiu/image_scale.c

.PHONY: check sanitize clean

check: test_cemu_display
	./test_cemu_display

sanitize:
	$(CC) $(CFLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer $(CPPFLAGS) $(SRCS) -o test_cemu_display_san
	./test_cemu_display_san

test_cemu_display: $(SRCS)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(SRCS) -o $@

clean:
	rm -f test_cemu_display test_cemu_display.exe test_cemu_display_san test_cemu_display_san.exe
