# MEM1 boot stub carrying a compressed MEM2 MAME image.
#   make PAYLOAD=path/to/mame.dol OUTPUT=path/to/boot.dol

DEVKITPRO ?= /opt/devkitpro
DEVKITPPC ?= $(DEVKITPRO)/devkitPPC
PYTHON    ?= python3

PAYLOAD ?= ../../../../wiitiny.dol
OUTPUT  ?= boot.dol
BUILD   ?= build

CC      := $(DEVKITPPC)/bin/powerpc-eabi-gcc
ARCH    := -mrvl -mcpu=750 -meabi -mhard-float
CFLAGS  := -O2 -Wall $(ARCH) -DGEKKO -DHW_RVL \
           -I$(DEVKITPRO)/libogc/include -I$(DEVKITPRO)/portlibs/ppc/include
LDFLAGS := $(ARCH) -L$(DEVKITPRO)/libogc/lib/wii -L$(DEVKITPRO)/portlibs/ppc/lib
LIBS    := -lz -logc -lm

HERE := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

.PHONY: all clean
all: $(OUTPUT)

$(BUILD)/payload.z: $(PAYLOAD) $(HERE)pack.py
	@mkdir -p $(BUILD)
	$(PYTHON) $(HERE)pack.py $(PAYLOAD) $@

$(BUILD)/payload.s: $(BUILD)/payload.z
	@printf '\t.section .rodata\n\t.balign 32\n\t.global payload_z\npayload_z:\n\t.incbin "%s"\npayload_z_end:\n\t.balign 4\n\t.global payload_z_size\npayload_z_size:\n\t.long payload_z_end - payload_z\n' "$(abspath $<)" > $@

$(BUILD)/payload.o: $(BUILD)/payload.s
	$(CC) $(ARCH) -c $< -o $@

$(BUILD)/bootstub.o: $(HERE)bootstub.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/bootstub.elf: $(BUILD)/bootstub.o $(BUILD)/payload.o
	$(CC) $(LDFLAGS) $^ $(LIBS) -o $@

$(OUTPUT): $(BUILD)/bootstub.elf
	$(DEVKITPRO)/tools/bin/elf2dol $< $@

clean:
	rm -rf $(BUILD) $(OUTPUT)
