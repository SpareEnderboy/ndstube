DEVKITPRO ?= /opt/devkitpro
DEVKITARM ?= $(DEVKITPRO)/devkitARM
.DEFAULT_GOAL := all

PROJECT_ROOT := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
ICON_ASSETS := $(PROJECT_ROOT)assets
GAME_ICON := $(PROJECT_ROOT)build/icon.bmp

ifeq ($(strip $(DEVKITARM)),)
$(error DEVKITARM is not set. Export the installed devkitPro devkitARM path.)
endif

include $(DEVKITARM)/ds_rules
GAME_TITLE := ndstube
GAME_SUBTITLE1 := YouTube for DSi
GAME_SUBTITLE2 :=
TARGET := ndstube
BUILD := build
SOURCES := client/source
INCLUDES := .
ARCH := -march=armv5te -mtune=arm946e-s -mthumb
CFLAGS := -g -Wall -Wextra -O2 -ffunction-sections -fdata-sections $(ARCH)
CFLAGS += $(INCLUDE) -DARM9
ASFLAGS := -g $(ARCH)
LDFLAGS = -specs=ds_arm9.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)
LIBS := -ldswifi9 -lnds9
LIBDIRS := $(LIBNDS)

$(GAME_ICON): $(ICON_ASSETS)/icon.h $(ICON_ASSETS)/icon.img.bin $(ICON_ASSETS)/icon.pal.bin $(PROJECT_ROOT)tools/icon_to_bmp.py
	@mkdir -p $(dir $@)
	python3 $(PROJECT_ROOT)tools/icon_to_bmp.py $(ICON_ASSETS)/icon.h $(ICON_ASSETS)/icon.img.bin $(ICON_ASSETS)/icon.pal.bin $@

ifneq ($(BUILD),$(notdir $(CURDIR)))
export OUTPUT := $(CURDIR)/$(TARGET)
export VPATH := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) $(CURDIR)
export DEPSDIR := $(CURDIR)/$(BUILD)
export LD := $(CC)
export INCLUDE := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
	$(foreach dir,$(LIBDIRS),-I$(dir)/include) -I$(CURDIR)/$(BUILD)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

CFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
export OFILES := $(CFILES:.c=.o) logoSmall.o
DEPENDS := $(OFILES:.o=.d)

.PHONY: all clean $(BUILD)
all: $(BUILD)

$(BUILD):
	@mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	rm -rf $(BUILD) $(TARGET).elf $(TARGET).nds $(TARGET).ds.gba
else
DEPENDS := $(OFILES:.o=.d)

.PHONY: all
all: $(OUTPUT).nds

$(OUTPUT).nds: $(GAME_ICON)
$(OUTPUT).nds: $(OUTPUT).elf
$(OUTPUT).elf: $(OFILES)

-include $(DEPENDS)
endif
