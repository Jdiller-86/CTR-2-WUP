DEVKITPRO ?= /workspace/ctr2wup-sdk
DEVKITPPC ?= $(DEVKITPRO)/devkitPPC
export DEVKITPRO DEVKITPPC
OUTPUT ?= ../../../artifacts/install-capture-native
CONFIG ?= $(OUTPUT)
CC := $(DEVKITPPC)/bin/powerpc-eabi-gcc
WUT := $(DEVKITPRO)/wut
SHA := $(shell git rev-parse HEAD)
DIRTY := $(if $(shell git status --porcelain --untracked-files=normal),1,0)
CFLAGS := -std=c11 -O2 -g -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
          -DESPRESSO -D__WIIU__ -D__WUT__ -mcpu=750 -meabi -mhard-float \
          -DCTR_SOURCE_COMMIT=\"$(SHA)\" -DCTR_SOURCE_DIRTY=$(DIRTY) \
          -I"$(WUT)/include" -I.. -I../../core -I"$(CONFIG)"
LDFLAGS := -mcpu=750 -meabi -mhard-float -specs="$(WUT)/share/wut.specs"
SOURCES := main.c log.c display.c ../diagnostic_font.c ../diagnostic_audio.c ../viewport.c ../image_scale.c \
           ../install_capture.c ../test_report.c ../report_storage.c ../test_lifecycle.c ../../core/ctr_sha256.c
OBJECTS := $(addprefix $(OUTPUT)/,$(notdir $(SOURCES:.c=.o)))
vpath %.c .. ../../core
.PHONY: all
all: $(OUTPUT)/ctr2wup-install-capture.wuhb
$(OUTPUT):
	mkdir -p "$@"
$(OUTPUT)/%.o: %.c $(CONFIG)/install_capture_package.h Makefile | $(OUTPUT)
	"$(CC)" $(CFLAGS) -MMD -MP -c "$<" -o "$@"
$(OUTPUT)/ctr2wup-install-capture.elf: $(OBJECTS)
	"$(CC)" $(LDFLAGS) $(OBJECTS) -L"$(WUT)/lib" -lwut -o "$@"
$(OUTPUT)/ctr2wup-install-capture.rpx: $(OUTPUT)/ctr2wup-install-capture.elf
	"$(DEVKITPRO)/tools/bin/elf2rpl" "$<" "$@"
$(OUTPUT)/ctr2wup-install-capture.wuhb: $(OUTPUT)/ctr2wup-install-capture.rpx
	"$(DEVKITPRO)/tools/bin/wuhbtool" "$<" "$@" --name "CTR2WUP Install Capture" --short-name "Install Capture" --author "CTR2WUP contributors"
-include $(OBJECTS:.o=.d)
