# PlayStation 2 build (requires the ps2dev toolchain: PS2SDK, GSKIT).
#   make                 -> build/ps2/pulsedash.elf (+ packed pulsedash-packed.elf)
#   make PERF=1          -> build/ps2-perf/..., logging per-frame EE cycle statistics
#   ./scripts/build-ps2.sh   runs this inside the ps2dev/ps2dev Docker image
#
# The PC version and developer tools live in Makefile.host.

GSKIT ?= $(PS2DEV)/gsKit

OUT := build/ps2$(if $(filter 1,$(PERF)),-perf)
EE_BIN := $(OUT)/pulsedash.elf
EE_BIN_PACKED := $(OUT)/pulsedash-packed.elf

SRC := $(wildcard src/core/*.c) $(wildcard src/levels/*.c) $(wildcard src/ps2/*.c)
EE_OBJS := $(patsubst src/%.c,$(OUT)/%.o,$(SRC))

EE_INCS := -I$(GSKIT)/include -I$(PS2SDK)/ports/include
EE_CFLAGS := -std=gnu99 -DPS2 -Wno-unused-parameter $(if $(filter 1,$(PERF)),-DPD_PERF)
EE_OPTFLAGS := -O2
EE_LDFLAGS := -L$(GSKIT)/lib -L$(PS2SDK)/ports/lib
EE_LIBS := -lgskit -ldmakit -lps2_drivers -laudsrv -lpad -lmtap -lmc -lpatches -lm

all: $(EE_BIN) $(EE_BIN_PACKED)

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal

$(OUT)/%.o: src/%.c $(wildcard src/core/*.h) $(wildcard src/ps2/*.h)
	@mkdir -p $(dir $@)
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

$(EE_BIN_PACKED): $(EE_BIN)
	@if command -v ps2-packer >/dev/null 2>&1; then ps2-packer $< $@ > /dev/null && echo "packed: $@"; \
	else cp $< $@; fi

clean:
	rm -rf $(OUT)

.PHONY: all clean
