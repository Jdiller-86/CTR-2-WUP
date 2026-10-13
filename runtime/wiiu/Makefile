ROOT := ../..
include $(ROOT)/runtime/core/sources.mk
DEVKITPRO ?= /workspace/ctr2wup-sdk
DEVKITPPC ?= $(DEVKITPRO)/devkitPPC
export DEVKITPRO DEVKITPPC
export PATH := $(DEVKITPPC)/bin:$(DEVKITPRO)/tools/bin:$(PATH)
# Keep make targets relative so a checkout directory may contain spaces/Unicode.
OUTPUT ?= ../../artifacts/wiiu
TARGET ?= ctr2wup-prototype
GUEST_DEFAULT ?= 0
EXTERNAL_AOT ?= 0
COMPILED_HOMEBREW ?= 0
FILESYSTEM_HOMEBREW ?= 0
ROMFS_HOMEBREW ?= 0
ROMFS_FILE ?= 0
GX_HOMEBREW ?= 0
PICA_HOMEBREW ?= 0
AUDIO_ONLY ?= 0
SCENE_ONLY ?= 0
UDP_LOG ?= 0
CALL_LIMIT ?= 64
APP_VERSION := 0.23.0
UPSTREAM_DIR ?= ../../artifacts/generated-upstream-guest
ifeq ($(GUEST_DEFAULT),1)
APP_NAME := CTR2WUP ARM Guest $(APP_VERSION)
else
APP_NAME := CTR2WUP Native $(APP_VERSION)
endif
BUILD := $(OUTPUT)/objects
CC := $(DEVKITPPC)/bin/powerpc-eabi-gcc
WUT := $(DEVKITPRO)/wut
CFLAGS := -std=c11 -O2 -g -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
          -DCTR_APP_VERSION=\"$(APP_VERSION)\" \
          -DESPRESSO -D__WIIU__ -D__WUT__ -DCTR_GUEST_DEFAULT=$(GUEST_DEFAULT) -DCTR_DIAGNOSTIC_UDP=$(UDP_LOG) -mcpu=750 -meabi -mhard-float -I"$(WUT)/include" -I../core
ifeq ($(COMPILED_HOMEBREW),1)
CFLAGS += -DCTR_COMPILED_HOMEBREW -I"$(COMPILED_IMAGE_DIR)"
APP_NAME := CTR2WUP Compiled AOT $(APP_VERSION)
ifeq ($(CALL_LIMIT),2)
APP_NAME := CTR2WUP Guarded AOT $(APP_VERSION)
CFLAGS += -DCTR_SHALLOW_AOT
endif
endif
ifeq ($(FILESYSTEM_HOMEBREW),1)
CFLAGS += -DCTR_FILESYSTEM_HOMEBREW
APP_NAME := CTR2WUP Compiled Save $(APP_VERSION)
endif
ifeq ($(ROMFS_HOMEBREW),1)
CFLAGS += -DCTR_ROMFS_HOMEBREW
APP_NAME := CTR2WUP Compiled RomFS $(APP_VERSION)
ifeq ($(ROMFS_FILE),1)
CFLAGS += -DCTR_ROMFS_FILE
APP_NAME := CTR2WUP Asset Files $(APP_VERSION)
endif
endif
ifeq ($(GX_HOMEBREW),1)
CFLAGS += -DCTR_GX_HOMEBREW
APP_NAME := CTR2WUP GX Transfers $(APP_VERSION)
endif
ifeq ($(PICA_HOMEBREW),1)
CFLAGS += -DCTR_PICA_HOMEBREW
APP_NAME := CTR2WUP Guest Draws $(APP_VERSION)
endif
ifeq ($(AUDIO_ONLY),1)
CFLAGS += -DCTR_AUDIO_ONLY
APP_NAME := CTR2WUP Audio Check $(APP_VERSION)
endif
ifeq ($(SCENE_ONLY),1)
CFLAGS += -DCTR_SCENE_ONLY
APP_NAME := CTR2WUP Scene Check $(APP_VERSION)
endif
LDFLAGS := -mcpu=750 -meabi -mhard-float -specs="$(WUT)/share/wut.specs" \
           -Wl,-Map,$(OUTPUT)/$(TARGET).map
SOURCES := main.c viewport.c diagnostic_font.c test_report.c test_guide.c test_lifecycle.c image_scale.c report_storage.c test_result.c diagnostic_audio.c
OBJECTS := $(addprefix $(BUILD)/,$(SOURCES:.c=.o))
CORE_SOURCES := $(CTR_RUNTIME_SOURCES) ../core/ctr_cpu_selftest.c \
    ../core/ctr_fs_snapshot.c ../core/ctr_guest_fixture.c \
    ../core/ctr_service_fixture.c \
    ../core/ctr_heap_fixture.c ../core/ctr_scene_fixture.c \
    ../core/ctr_kernel_fixture.c \
    ../core/ctr_hid_fixture.c \
    ../core/ctr_gsp_fixture.c \
    ../core/ctr_relay_fixture.c \
    ../core/ctr_stack_fixture.c ../core/ctr_thread_fixture.c ../core/ctr_sha256.c ../core/ctr_romfs_file.c ../core/ctr_texture.c \
    ../../tests/fixtures/threads/thread.generated.c ../../tests/fixtures/threads/thread-image.generated.c \
    ../../tests/fixtures/stack/stack.generated.c ../../tests/fixtures/stack/stack-image.generated.c \
    ../../tests/fixtures/relay/relay.generated.c ../../tests/fixtures/relay/relay-image.generated.c \
    ../../tests/fixtures/gsp/gsp.generated.c ../../tests/fixtures/gsp/gsp-image.generated.c \
    ../../tests/fixtures/hid/hid.generated.c ../../tests/fixtures/hid/hid-image.generated.c \
    ../../tests/fixtures/kernel/kernel.generated.c ../../tests/fixtures/kernel/kernel-image.generated.c \
    ../../tests/fixtures/heap/heap.generated.c ../../tests/fixtures/heap/heap-image.generated.c \
    ../../tests/fixtures/services/boot.generated.c ../../tests/fixtures/services/boot-image.generated.c \
    ../../tests/fixtures/cpu/arithmetic.generated.c ../../tests/fixtures/cpu/exit.generated.c \
    ../../tests/fixtures/guest/framebuffer.generated.c ../../tests/fixtures/guest/image.generated.c
ifeq ($(EXTERNAL_AOT),1)
CFLAGS += -DCTR_EXTERNAL_AOT -I../recomp -I"$(UPSTREAM_DIR)" -ffp-contract=off -fno-fast-math
CFLAGS += -DCTR_RECOMP_CALL_LIMIT=$(CALL_LIMIT)
CORE_SOURCES := $(filter-out ../../tests/fixtures/guest/framebuffer.generated.c,$(CORE_SOURCES))
CORE_SOURCES += ../recomp/ctr_recomp.c $(UPSTREAM_DIR)/entries.c $(wildcard $(UPSTREAM_DIR)/code*.c)
endif
CORE_OBJECTS := $(addprefix $(BUILD)/,$(notdir $(CORE_SOURCES:.c=.o)))
OBJECTS += $(CORE_OBJECTS)
vpath %.c ../core ../recomp ../../tests/fixtures/cpu ../../tests/fixtures/guest ../../tests/fixtures/services ../../tests/fixtures/heap ../../tests/fixtures/kernel ../../tests/fixtures/hid ../../tests/fixtures/gsp ../../tests/fixtures/relay ../../tests/fixtures/stack ../../tests/fixtures/threads $(UPSTREAM_DIR)
.PHONY: all clean
all: $(OUTPUT)/$(TARGET).rpx $(OUTPUT)/$(TARGET).wuhb
$(BUILD):
	mkdir -p "$@"
$(BUILD)/%.o: %.c $(CTR_RUNTIME_MANIFEST) Makefile | $(BUILD)
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
