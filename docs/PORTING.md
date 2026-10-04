# How the code is shared between platforms

Pulse Dash runs on a PC, a PS2, a PSP and a Game Boy Color. These are very
different machines, so not all of them can run the same code, but they all
play the same game: the same levels, songs and rules, and the same physics
to the tick. This is how the code is split to make that possible, and what a
new platform needs.

## The layers

**1. The game's definition: data and rules, shared by every platform.**

| What | Where |
|------|-------|
| Levels, as ASCII art | `src/levels/`, parsed by `src/core/level.c` |
| The physics' rules: every speed, gravity, hitbox and object box, in fixed point | `src/core/sim_rules.h` |
| Songs, as note data | `src/core/songs.c` |
| Level palettes, font | `src/core/theme.c`, `src/core/font.c` |

A platform that can't read these at run time gets them converted at build
time by a host tool that compiles them in: the Game Boy's `gbc_tool` turns
the levels into tile maps, the songs into byte streams for its sound chip
and the palettes into its 15-bit colours.

**2. The reference logic: integer C that every platform runs or matches.**

`src/core/sim.c` is the player's physics: one call advances the player by a
tick (1/60 s) in four sub-steps. It uses integers only, so it gives the same
results on every CPU (floating point doesn't: the PS2's rounds toward zero,
a PC's to nearest, and the levels have spots that one-thousandth of a
block decides). The level solver (`src/host/solver.c`) proves every level
beatable on it, on the beat and at 20 Hz input, so the proof holds on every
platform that plays it exactly.

`src/core/progress.c` is what an attempt counts for: when an attempt
counts (normal mode only), how far into a level a run got (a percentage of
the exact position, rounded down), what a new best is, what a finish and
its coins add, and when a run left from the pause menu counts as a death.
Its `Progress` is the part of a save every platform has; each platform
stores it its own way (a memory card file, battery RAM). The Game Boy
compiles this file as it is, into a ROM bank of its own.

**3. Platform versions of the reference, as fast as the platform needs.**

A platform may replace any part of the reference with its own code (in
assembly, with its own data layout, skipping work it can prove has no
effect) on one condition: given the same inputs, it gives exactly the same
results, tick for tick, and a test shows it. The Game Boy's physics
(`src/gbc/gbsim.c`) is the first: an 8-bit CPU without a multiply needs a
different program to fit a frame, but `gbc_tool difftest` plays it side by
side with the reference over some 30 million ticks and fails on the first
difference, and the emulator test checks the ROM itself on every tick.

Such a version may rely on limits the reference doesn't have (the Game
Boy's keeps 16 rows per column and remembers the last 8 used objects); its
platform's data tool checks every level against them, and a level that
doesn't fit is left out of that platform.

**4. Presentation: per platform, or per family of platforms.**

Graphics, sound output, menus and the frame loop belong to the platform.
Platforms that draw the same way share it:

- *The vector family* (PC, PS2, PSP): `src/core` draws everything as
  triangles and rectangles in a 640x448 virtual screen (wider on the PSP),
  plays the songs with a software synthesizer, and runs the menus. A
  platform supplies the primitives of `src/core/gfx.h`, an audio thread
  calling `audio_mix`, the pad as `BTN_*` bits and two save functions
  (`src/core/platform.h`): `src/ps2` and `src/psp` are 600 to 950 lines
  each.
- *The tile family* (Game Boy Color): `src/gbc` streams the level into a
  tile map, draws the player and effects as sprites and plays the songs on
  the sound chip, all from the data `gbc_tool` makes. It plays the levels
  that fit it (`gbc_tool levels` lists them, and says why any other
  doesn't).

## Rules for shared code

Code in layer 2 must run unchanged on every platform that uses it, and be
something a platform can match exactly:

- integers only, with explicit sizes (`int16_t`, `uint32_t`): on some
  compilers `int` is 16 bits;
- C the Game Boy's compiler (SDCC) builds: declarations at the top of a
  block, a function the Game Boy keeps in a ROM bank marked `PD_BANKED`
  (`portable.h`), and `Makefile.gbc` lists the shared files it compiles;
- no memory allocation, no I/O, nothing from the renderer or the audio;
- deterministic: the same inputs give the same results, always;
- a change to it is a change to every platform's game: change
  `sim_rules.h` and `sim.c` together, run `make -f Makefile.host test`
  (the levels must still pass), then make each platform version match
  (`gbc_tool difftest` shows where it doesn't).

## Adding a platform

- **A CPU with floating point and a GPU that draws triangles** (Dreamcast,
  GameCube/Wii, 3DS, a web build): a new frontend in `src/<platform>/`
  like `src/psp/`, with a `target.h` saying how wide its virtual screen is
  and what its buttons are called (see `src/psp/target.h`), and a makefile
  that includes `sources.mk` (the core's and the levels' sources), adds
  the frontend's and puts its folder on the include path. Everything else
  is shared.
- **A 32-bit CPU without floating point** (GBA, DS, PS1): the physics runs
  as it is; the vector family's renderer and synth use floats throughout
  and would be slow emulated, so the presentation would be the platform's
  own (a tile-based one on the GBA, like the Game Boy's).
- **An 8 or 16-bit CPU** (Game Boy, NES, Master System, Mega Drive): its own
  version of the physics, with a difftest against the reference like
  `gbc_tool difftest`, and a data tool like `gbc_tool` for its formats.

Whatever the platform, test it the way the Game Boy is tested
(`scripts/gbc-emu-test.py`): play the solver's runs of every level in an
emulator, compare the player with the reference after every tick, and
check that no frame runs late.
