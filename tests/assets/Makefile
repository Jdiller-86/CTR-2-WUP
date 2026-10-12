CC ?= cc
FLAGS := -std=c11 -O2 -g -Wall -Wextra -Wpedantic -Werror -I../../runtime/core
OUT := ../../artifacts/asset-tests
CORE := ctr_memory ctr_cpu ctr_guest ctr_ipc ctr_heap ctr_kernel ctr_hid ctr_gsp ctr_fs ctr_apt ctr_sha256 ctr_romfs_file
SOURCES := test_assets.c $(addprefix ../../runtime/core/,$(addsuffix .c,$(CORE)))
.PHONY: check sanitize
$(OUT):
	mkdir -p "$@"
$(OUT)/assets: $(SOURCES) | $(OUT)
	$(CC) $(FLAGS) $(SOURCES) -o "$@"
check: $(OUT)/assets
	"$(OUT)/assets"
sanitize: | $(OUT)
	$(CC) $(FLAGS) -O1 -fno-omit-frame-pointer -fsanitize=address,undefined $(SOURCES) -o "$(OUT)/assets-san"
	ASAN_OPTIONS=detect_leaks=1 "$(OUT)/assets-san"
