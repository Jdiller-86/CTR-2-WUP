# Canonical portable guest runtime. Fixture-specific sources stay with consumers.
ROOT ?= ../..
CTR_RUNTIME_FILES := $(strip $(file <$(ROOT)/runtime/core/sources.list))
CTR_RUNTIME_SOURCES := $(addprefix $(ROOT)/runtime/core/,$(CTR_RUNTIME_FILES))
CTR_RUNTIME_MANIFEST := $(ROOT)/runtime/core/sources.list $(ROOT)/runtime/core/sources.mk
