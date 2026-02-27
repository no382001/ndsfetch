BLOCKSDS        ?= /opt/blocksds/core
BLOCKSDSEXT     ?= /opt/blocksds/external
WONDERFUL_TOOLCHAIN ?= /opt/wonderful

NAME            := ndsfetch
GAME_TITLE      := ndsfetch
GAME_SUBTITLE   := NDS WiFi Hot Reload

SOURCES_C       := source/main.c
SOURCES_S       := source/load_bin.s
INCLUDEDIRS     := source
DEFINES         :=

ARM7ELF         := $(BLOCKSDS)/sys/arm7/main_core/arm7_dswifi.elf
LIBS            := -ldswifi9 -lnds9 -lc
LIBDIRS         := $(BLOCKSDS)/libs/dswifi \
                   $(BLOCKSDS)/libs/libnds

BUILDDIR        := build
ELF             := $(BUILDDIR)/$(NAME).elf
MAP             := $(BUILDDIR)/$(NAME).map
ROM             := $(NAME).nds

PREFIX          := $(WONDERFUL_TOOLCHAIN)/toolchain/gcc-arm-none-eabi/bin/arm-none-eabi-
CC              := $(PREFIX)gcc
LD              := $(PREFIX)gcc

ARCH            := -mthumb -mcpu=arm946e-s+nofp
SPECS           := $(BLOCKSDS)/sys/crts/dsi_arm9.specs

INCLUDEFLAGS    := $(foreach dir,$(INCLUDEDIRS),-I$(dir)) \
                   $(foreach dir,$(LIBDIRS),-isystem $(dir)/include)

LIBDIRSFLAGS    := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

CFLAGS          := -std=gnu11 \
                   -Wall -Wextra -Wpedantic -Werror \
                   -O2 \
                   $(ARCH) \
                   $(INCLUDEFLAGS) \
                   $(DEFINES) \
                   -ffunction-sections -fdata-sections \
                   -specs=$(SPECS)

LDFLAGS         := $(ARCH) $(LIBDIRSFLAGS) \
                   -Wl,-Map,$(MAP) \
                   -Wl,--gc-sections \
                   -Wl,--start-group $(LIBS) -Wl,--end-group \
                   -specs=$(SPECS)

OBJS            := $(addprefix $(BUILDDIR)/,$(notdir $(SOURCES_C:.c=.o))) \
                   $(addprefix $(BUILDDIR)/,$(notdir $(SOURCES_S:.s=.o)))
VPATH           := source
NDSTOOL         := $(BLOCKSDS)/tools/ndstool/ndstool

.PHONY: all clean bootloader

all: $(ROM)
	@echo ""
	@echo "  Built: $(ROM)"
	@echo ""

# Build ARM7 bootloader binary before the sideloader
bootloader:
	@$(MAKE) -C bootloader

# load_bin.s depends on the bootloader binary
$(BUILDDIR)/load_bin.o: bootloader

$(ROM): $(ELF) $(ARM7ELF)
	$(NDSTOOL) -c $@ \
		-7 $(ARM7ELF) \
		-9 $(ELF) \
		-b $(BLOCKSDS)/sys/icon.bmp \
		"$(GAME_TITLE);$(GAME_SUBTITLE)" \
		-h 0x4000 \
		-uc 2

$(ELF): $(OBJS)
	$(LD) -o $@ $(OBJS) $(LDFLAGS)

$(BUILDDIR)/%.o: %.c
	@mkdir -p $(BUILDDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILDDIR)/%.o: %.s
	@mkdir -p $(BUILDDIR)
	$(CC) $(ARCH) -x assembler-with-cpp -Wa,-I$(CURDIR)/bootloader -c $< -o $@

clean:
	$(MAKE) -C bootloader clean
	rm -rf $(BUILDDIR) $(ROM)
