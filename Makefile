# PlayStation 2 build (requires the ps2dev toolchain: PS2SDK, GSKIT).
#   make                 -> build/ps2/PULSEDASH.ELF, the game (compressed with ps2-packer),
#                           and pulsedash-unpacked.elf (the same with debug info)
#   make PERF=1          -> build/ps2-perf/..., logging per-frame EE cycle statistics
#   make VERSION=v1.2.0  -> version shown on the title screen (default: git describe)
#   ./scripts/build-ps2.sh   runs this inside the ps2dev/ps2dev Docker image
#
# The PC version and developer tools live in Makefile.host.

GSKIT ?= $(PS2DEV)/gsKit
VERSION ?= $(shell git describe --tags --always --dirty 2>/dev/null || echo dev)

OUT := build/ps2$(if $(filter 1,$(PERF)),-perf)
EE_BIN := $(OUT)/pulsedash-unpacked.elf
EE_BIN_PACKED := $(OUT)/PULSEDASH.ELF

SRC := $(wildcard src/core/*.c) $(wildcard src/levels/*.c) $(wildcard src/ps2/*.c)
# IOP modules from ps2sdk, compiled into the ELF as C arrays (see src/ps2/irx.c)
IRX := libsd audsrv sio2man padman mcman mcserv
EE_OBJS := $(patsubst src/%.c,$(OUT)/%.o,$(SRC)) $(patsubst %,$(OUT)/irx/%_irx.o,$(IRX))

EE_INCS := -I$(GSKIT)/include -I$(PS2SDK)/ports/include
EE_CFLAGS := -std=gnu99 -DPS2 -Wno-unused-parameter $(if $(filter 1,$(PERF)),-DPD_PERF)
EE_OPTFLAGS := -O2
EE_LDFLAGS := -L$(GSKIT)/lib -L$(PS2SDK)/ports/lib
EE_LIBS := -lgskit -ldmakit -laudsrv -lpad -lmc -lpatches -lm

all: $(EE_BIN) $(EE_BIN_PACKED)

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal

$(OUT)/%.o: src/%.c $(wildcard src/core/*.h) $(wildcard src/ps2/*.h)
	@mkdir -p $(dir $@)
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

$(OUT)/irx/%_irx.c: $(PS2SDK)/iop/irx/%.irx
	@mkdir -p $(dir $@)
	$(PS2SDK)/bin/bin2c $< $@ $*_irx

$(OUT)/irx/%.o: $(OUT)/irx/%.c
	$(EE_CC) $(EE_CFLAGS) -c $< -o $@

# the version is compiled into one small object, rebuilt every time
$(OUT)/core/version.o: EE_CFLAGS += -DPD_VERSION=\"$(VERSION)\"
$(OUT)/core/version.o: FORCE
FORCE:

# the packed ELF decompresses itself at boot: a tenth of the size, faster to load
$(EE_BIN_PACKED): $(EE_BIN)
	@if command -v ps2-packer >/dev/null 2>&1; then ps2-packer $< $@ > /dev/null && echo "packed: $@"; \
	else echo "warning: ps2-packer not found, $@ is not compressed"; cp $< $@; fi

clean:
	rm -rf $(OUT)

.PHONY: all clean FORCE
