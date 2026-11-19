DEVKITPRO ?= /workspace/ctr2wup-sdk
DEVKITPPC ?= $(DEVKITPRO)/devkitPPC
export DEVKITPRO DEVKITPPC
CONTROL_DEPS ?= $(DEVKITPRO)/../ctr2wup-control-deps
WUT := $(DEVKITPRO)/wut
WUPS := $(CONTROL_DEPS)/wups
RPXLOADER := $(CONTROL_DEPS)/rpxloader
MONO := $(CONTROL_DEPS)/monocypher-4.0.3/src
OUTPUT ?= ../../../artifacts/wiiu-control
BUILD := $(OUTPUT)/objects
TARGET := $(OUTPUT)/ctr2wup-control
CC := $(DEVKITPPC)/bin/powerpc-eabi-gcc
ARCH := -mcpu=750 -meabi -mhard-float
CFLAGS := $(ARCH) -std=c11 -O2 -g -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
          -DESPRESSO -D__WIIU__ -D__WUT__ -I.. -I../../core -I"$(WUT)/include" \
          -I"$(WUPS)/include" -I"$(RPXLOADER)/include" -I"$(MONO)" -I"$(MONO)/optional"
LDFLAGS := $(ARCH) -specs="$(WUT)/share/wut.specs" -T"$(WUPS)/share/wups.ld" \
           -specs="$(WUPS)/share/wups.specs"
LIBS := -L"$(RPXLOADER)/lib" -L"$(WUPS)/lib" -L"$(WUT)/lib" -lrpxloader -lwups -lwut
OBJECTS := $(addprefix $(BUILD)/,main.o install_native.o protocol.o install_capture.o \
                                report_storage.o ctr_sha256.o monocypher.o monocypher-ed25519.o)
vpath %.c .. ../../core $(MONO) $(MONO)/optional

.PHONY: all clean
all: $(TARGET).wps

$(BUILD):
	mkdir -p "$@"

$(BUILD)/%.o: %.c Makefile | $(BUILD)
	"$(CC)" $(CFLAGS) -MMD -MP -c "$<" -o "$@"

$(TARGET).elf: $(OBJECTS)
	"$(CC)" $(LDFLAGS) $(OBJECTS) $(LIBS) -o "$@"

$(TARGET).wps: $(TARGET).elf
	"$(DEVKITPPC)/bin/powerpc-eabi-strip" -g -o "$(BUILD)/control.strip.elf" "$<"
	"$(DEVKITPRO)/tools/bin/elf2rpl" "$(BUILD)/control.strip.elf" "$@"
	printf 'PL' | dd of="$@" bs=1 seek=9 count=2 conv=notrunc status=none

clean:
	rm -rf "$(BUILD)"
	rm -f "$(TARGET).elf" "$(TARGET).wps"

-include $(OBJECTS:.o=.d)
