#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITARM)/3ds_rules

#---------------------------------------------------------------------------------
# App metadata
#---------------------------------------------------------------------------------
APP_TITLE       := Pixel UI Designer
APP_DESCRIPTION := Pixel art & UI designer
APP_AUTHOR      := murphy97alex
APP_PRODUCT_CODE := CTR-H-PXUI
APP_UNIQUE_ID   := 0xC7A30
APP_VERSION_MAJOR := 0
APP_VERSION_MINOR := 4
APP_VERSION_MICRO := 0

#---------------------------------------------------------------------------------
TARGET      := pixel-ui-designer
BUILD       := build
SOURCES     := source
DATA        := data
INCLUDES    := include
GRAPHICS    := gfx
ROMFS       :=
GFXBUILD    := $(BUILD)

ICON        := assets/icon.png
BANNER_IMAGE := assets/banner.png
BANNER_AUDIO := assets/banner.wav
RSF_FILE    := assets/cia.rsf

#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------
ARCH    := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

CFLAGS  := -g -Wall -O2 -mword-relocations \
           -ffunction-sections \
           $(ARCH)

CFLAGS  += $(INCLUDE) -D__3DS__

CXXFLAGS := $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++11

ASFLAGS := -g $(ARCH)
LDFLAGS  = -specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

LIBS    := -lcitro2d -lcitro3d -lctru -lpng -lz -lm

LIBDIRS := $(PORTLIBS) $(CTRULIB)

#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT   := $(CURDIR)/$(TARGET)
export TOPDIR   := $(CURDIR)

export VPATH    := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
                   $(foreach dir,$(GRAPHICS),$(CURDIR)/$(dir)) \
                   $(foreach dir,$(DATA),$(CURDIR)/$(dir))

export DEPSDIR  := $(CURDIR)/$(BUILD)

CFILES      := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES    := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES      := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
PICAFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.v.pica)))
SHLISTFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.shlist)))
GFXFILES    := $(foreach dir,$(GRAPHICS),$(notdir $(wildcard $(dir)/*.t3s)))
BINFILES    := $(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

ifeq ($(strip $(CPPFILES)),)
	export LD := $(CC)
else
	export LD := $(CXX)
endif

export T3XFILES     := $(GFXFILES:.t3s=.t3x)
export OFILES_SOURCES := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES_BIN   := $(addsuffix .o,$(BINFILES)) \
                       $(PICAFILES:.v.pica=.shbin.o) $(SHLISTFILES:.shlist=.shbin.o)
export OFILES       := $(OFILES_BIN) $(OFILES_SOURCES)
export HFILES       := $(PICAFILES:.v.pica=_shbin.h) $(SHLISTFILES:.shlist=_shbin.h) \
                       $(addsuffix .h,$(subst .,_,$(BINFILES)))

export INCLUDE  := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                   $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                   -I$(CURDIR)/$(BUILD)

export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

export APP_ICON := $(TOPDIR)/$(ICON)
export _3DSXFLAGS += --smdh=$(CURDIR)/$(TARGET).smdh

.PHONY: all clean cia

all: $(BUILD)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

$(BUILD):
	@mkdir -p $@

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).3dsx $(TARGET).smdh $(TARGET).elf $(TARGET).cia \
	        $(TARGET).bnr $(TARGET).icn

#---------------------------------------------------------------------------------
# CIA: needs bannertool and makerom on PATH
#---------------------------------------------------------------------------------
cia: all $(TARGET).bnr $(TARGET).icn
	makerom -f cia -o $(TARGET).cia -elf $(TARGET).elf -rsf $(RSF_FILE) \
		-target t -exefslogo -icon $(TARGET).icn -banner $(TARGET).bnr \
		-major $(APP_VERSION_MAJOR) -minor $(APP_VERSION_MINOR) -micro $(APP_VERSION_MICRO) \
		-DAPP_TITLE="$(APP_TITLE)" -DAPP_PRODUCT_CODE="$(APP_PRODUCT_CODE)" \
		-DAPP_UNIQUE_ID="$(APP_UNIQUE_ID)"
	@echo built ... $(TARGET).cia

$(TARGET).bnr: $(BANNER_IMAGE) $(BANNER_AUDIO)
	bannertool makebanner -i $(BANNER_IMAGE) -a $(BANNER_AUDIO) -o $@

$(TARGET).icn: $(ICON)
	bannertool makesmdh -s "$(APP_TITLE)" -l "$(APP_DESCRIPTION)" -p "$(APP_AUTHOR)" \
		-i $(ICON) -o $@

#---------------------------------------------------------------------------------
else
#---------------------------------------------------------------------------------

$(OUTPUT).3dsx : $(OUTPUT).elf $(OUTPUT).smdh

$(OFILES_SOURCES) : $(HFILES)

$(OUTPUT).elf : $(OFILES)

%.bin.o %_bin.h : %.bin
	@echo $(notdir $<)
	@$(bin2o)

-include $(DEPSDIR)/*.d

#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------
