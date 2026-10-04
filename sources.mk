# The portable game's sources, for the makefiles of every platform that
# runs the shared core (PS2: Makefile, PSP: Makefile.psp, PC: Makefile.host).
# A platform adds its own folder: SRC := $(CORE_SRC) $(wildcard src/<it>/*.c),
# with -Isrc/<it> for its target.h (see docs/PORTING.md).
CORE_SRC := $(wildcard src/core/*.c) $(wildcard src/levels/*.c)
