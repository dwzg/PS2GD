# The portable game's sources, for the makefiles of every platform that
# runs the shared core (PS2: Makefile, PSP: Makefile.psp, PC: Makefile.host).
#
# CORE_SRC is the game's logic and data: the physics, levels, saves and
# progress, the menus' and the play screen's flow, the title screen's demo
# run, the camera, palette changes and particles. Every platform that runs
# the core links it (PC, PS2, PSP and GBA).
#
# VECTOR_SRC is the vector family's presentation (PC, PS2, PSP): the
# triangle renderer, the font and icons drawn with it, the drawing of the
# screens, and the software synthesizer with its songs. A platform with a
# presentation of its own links CORE_SRC only and provides game_draw_init,
# game_render (game.h) and the calls of audio.h the core makes.
#
# The core's files are listed one by one: a new one goes into one list or
# the other deliberately. A vector family platform adds its own folder:
# SRC := $(CORE_SRC) $(VECTOR_SRC) $(wildcard src/<it>/*.c), with
# -Isrc/<it> for its target.h (see docs/PORTING.md).
CORE_SRC := src/core/sim.c src/core/level.c src/core/progress.c src/core/save.c \
            src/core/theme.c src/core/version.c src/core/game.c src/core/play.c \
            src/core/demo.c src/core/fx.c $(wildcard src/levels/*.c)
VECTOR_SRC := src/core/draw.c src/core/render.c src/core/font.c src/core/icons.c \
              src/core/game_draw.c src/core/play_draw.c src/core/demo_draw.c \
              src/core/fx_draw.c src/core/audio.c src/core/songs.c
