DEVKITPRO ?= /workspace/ctr2wup-sdk
DEVKITPPC ?= $(DEVKITPRO)/devkitPPC
export DEVKITPRO DEVKITPPC
export PATH := $(DEVKITPPC)/bin:$(DEVKITPRO)/tools/bin:$(PATH)
# Keep make targets relative so a checkout directory may contain spaces/Unicode.
OUTPUT ?= ../../artifacts/wiiu
TARGET ?= ctr2wup-prototype
GUEST_DEFAULT ?= 0
ifeq ($(GUEST_DEFAULT),1)
APP_NAME := CTR2WUP ARM Guest Fixture
else
APP_NAME := CTR2WUP Native Diagnostic
endif
BUILD := $(OUTPUT)/objects
CC := $(DEVKITPPC)/bin/powerpc-eabi-gcc
WUT := $(DEVKITPRO)/wut
CFLAGS := -std=c11 -O2 -g -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
          -DESPRESSO -D__WIIU__ -D__WUT__ -DCTR_GUEST_DEFAULT=$(GUEST_DEFAULT) -mcpu=750 -meabi -mhard-float -I"$(WUT)/include" -I../core
LDFLAGS := -mcpu=750 -meabi -mhard-float -specs="$(WUT)/share/wut.specs" \
           -Wl,-Map,$(OUTPUT)/$(TARGET).map
SOURCES := main.c viewport.c diagnostic_font.c
OBJECTS := $(addprefix $(BUILD)/,$(SOURCES:.c=.o))
CORE_SOURCES := ../core/ctr_memory.c ../core/ctr_cpu.c ../core/ctr_cpu_selftest.c \
                ../core/ctr_guest.c ../core/ctr_guest_fixture.c \
                ../../tests/fixtures/cpu/arithmetic.generated.c ../../tests/fixtures/cpu/exit.generated.c \
                ../../tests/fixtures/guest/framebuffer.generated.c ../../tests/fixtures/guest/image.generated.c
CORE_OBJECTS := $(addprefix $(BUILD)/,$(notdir $(CORE_SOURCES:.c=.o)))
OBJECTS += $(CORE_OBJECTS)
vpath %.c ../core ../../tests/fixtures/cpu ../../tests/fixtures/guest
.PHONY: all clean
all: $(OUTPUT)/$(TARGET).rpx $(OUTPUT)/$(TARGET).wuhb
$(BUILD):
	mkdir -p "$@"
$(BUILD)/%.o: %.c | $(BUILD)
	"$(CC)" $(CFLAGS) -MMD -MP -c "$<" -o "$@"
$(OUTPUT)/$(TARGET).elf: $(OBJECTS)
	"$(CC)" $(LDFLAGS) $(OBJECTS) -L"$(WUT)/lib" -lwut -lm -o "$@"
$(OUTPUT)/$(TARGET).rpx: $(OUTPUT)/$(TARGET).elf
	"$(DEVKITPRO)/tools/bin/elf2rpl" "$<" "$@"
$(OUTPUT)/$(TARGET).wuhb: $(OUTPUT)/$(TARGET).rpx
	"$(DEVKITPRO)/tools/bin/wuhbtool" "$<" "$@" --name "$(APP_NAME)" --short-name "CTR2WUP" --author "CTR2WUP contributors"
clean:
	rm -rf "$(BUILD)"
	rm -f "$(OUTPUT)/$(TARGET).elf" "$(OUTPUT)/$(TARGET).rpx" "$(OUTPUT)/$(TARGET).wuhb" "$(OUTPUT)/$(TARGET).map"
-include $(OBJECTS:.o=.d)
