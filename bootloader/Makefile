WONDERFUL_TOOLCHAIN ?= /opt/wonderful
BLOCKSDS            ?= /opt/blocksds/core

PREFIX  := $(WONDERFUL_TOOLCHAIN)/toolchain/gcc-arm-none-eabi/bin/arm-none-eabi-
CC      := $(PREFIX)gcc
OBJCOPY := $(PREFIX)objcopy

# Path to vendored TWiLightMenu bootloader source
BOOTER  := vendor

BUILD   := build
TARGET  := load

ARCH    := -mthumb -mthumb-interwork
NDS_INC := -I$(BLOCKSDS)/libs/libnds/include

CFLAGS  := -g -Wall -O2 \
           -mcpu=arm7tdmi -mtune=arm7tdmi \
           -fomit-frame-pointer -ffast-math -flto \
           $(ARCH) $(NDS_INC) \
           -DARM7 -DNO_DLDI

ASFLAGS := -g $(ARCH) -flto $(NDS_INC) -DARM7 -DNO_DLDI

LDFLAGS := -nostartfiles -T $(BOOTER)/load.ld -g $(ARCH) \
           -Wl,-Map,$(BUILD)/$(TARGET).map

SRCDIR  := $(BOOTER)/source

# source files (order matters for linker — crt0 first)
C_SRCS  := boot.c fat.c sdmmc.c dldi_patcher.c
ARM_SRCS := arm9clear.arm.c
S_SRCS  := load_crt0.s bios.s io_dldi.s arm9mpu_reset.s
STUB_SRCS := dma_stub.c

OBJS    := $(addprefix $(BUILD)/,$(S_SRCS:.s=.o) $(ARM_SRCS:.arm.c=.arm.o) $(C_SRCS:.c=.o) $(STUB_SRCS:.c=.o))

.PHONY: all clean

all: $(TARGET).bin
	@echo "  Built: $(TARGET).bin ($$(wc -c < $(TARGET).bin) bytes)"

$(TARGET).bin: $(BUILD)/$(TARGET).elf
	$(OBJCOPY) -O binary $< $@

$(BUILD)/$(TARGET).elf: $(OBJS)
	$(CC) $(LDFLAGS) $^ -o $@

# MPU reset stub: ARM9 code compiled to flat binary, embedded via .incbin
$(BUILD)/mpu_reset.bin: $(BUILD)/mpu_reset.elf
	$(OBJCOPY) -O binary $< $@

$(BUILD)/mpu_reset.elf: $(BOOTER)/arm9code/mpu_reset.s | $(BUILD)
	$(CC) $(ASFLAGS) -x assembler-with-cpp -Ttext=0 \
		-nostartfiles -nostdlib $< -o $@

# arm9mpu_reset.s .incbin's mpu_reset.bin — tell GAS where to find it
$(BUILD)/arm9mpu_reset.o: $(BUILD)/mpu_reset.bin

# ARM mode for arm9clear (overrides -mthumb)
$(BUILD)/arm9clear.arm.o: $(SRCDIR)/arm9clear.arm.c | $(BUILD)
	$(CC) $(CFLAGS) -marm -c $< -o $@

# generic rules
$(BUILD)/%.o: $(SRCDIR)/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: %.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: $(SRCDIR)/%.s | $(BUILD)
	$(CC) $(ASFLAGS) -x assembler-with-cpp -Wa,-I$(abspath $(BUILD)) -c $< -o $@

$(BUILD):
	mkdir -p $@

clean:
	rm -rf $(BUILD) $(TARGET).bin
