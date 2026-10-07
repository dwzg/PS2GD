# How the code is shared between platforms

Pulse Dash runs on a PC, a PS2, a PSP, a Nintendo DS, a Game Boy Advance
and a Game Boy Color. These are very
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
and the palettes into its 15-bit colours. The Game Boy Advance reads the
palettes itself; its `gba_tool` runs the core's level parser and the
vector family's renderer and synth (layer 4) on the PC, to store the
levels already parsed and drawn as tiles, draw its sprites and record its
songs and sound effects.

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

- *The vector family* (PC, PS2, PSP, DS): `src/core` draws everything as
  triangles and rectangles in a 640x448 virtual screen (wider on the PSP,
  narrower on the DS)
  and plays the songs with a software synthesizer (`VECTOR_SRC` in
  `sources.mk`), around the game's logic that runs the menus, the levels
  and the title screen's demo (`CORE_SRC`, which a platform with a
  presentation of its own can link alone). A platform supplies the
  primitives of `src/core/gfx.h`, an audio thread calling `audio_mix` (below
  the frame loop, with enough mixed ahead to outlast the loop's longest
  stretch on the CPU, and the time from mixing to hearing given to
  `audio_set_latency`; mixing ahead only helps if handing the sound to the
  hardware doesn't wait for the loop too: the PSP hands it over from a
  thread above the loop; the PS2's audsrv plays from a ring on the IOP,
  its own processor, kept about 40 ms full), the pad as `BTN_*` bits and two save functions
  (`src/core/platform.h`):
  `src/ps2` and `src/psp` are 650 to 1,100 lines each. A backend built
  with `GFX_GLOW` also draws the glows itself (`gfx_glow`, from a texture
  on the PSP), and may decline one, which `draw_glow` then draws as a fan
  of triangles as on the other backends. Its `target.h`
  gives `PIXEL_GRID`, how many of the screen's pixels a virtual one
  covers, so that text, icons and outlines land on whole pixels and come
  out even (`draw.h`); where the picture stretches the virtual screen down
  (the PS2's in PAL: 448 lines on 512) the frontend sets the grid down the
  screen apart, with `draw_set_pixel_grid_y()`. The PS2's `FLICKER_OPTION`
  adds the options' FLICKER FILTER row, kept in the save's `flicker` byte
  (its last reserved byte: the save's format and size are unchanged) and
  passed to `plat_flicker_filter()`.
  - The Nintendo DS is the family's platform without floating point
    (`src/nds`, [NDS_PORT.md](NDS_PORT.md)): its 3D engine draws the
    primitives, but its CPU does the renderer's float math in software,
    and a backend can do two things for it. Built with
    `GFX_DEVICE_RECTS`, it draws rectangles given in whole device pixels
    of the pixel grid and the font's glyphs (and their outlines) whole
    (`gfx_rect_dev`, `gfx_glyph_dev`, `gfx_glyph_outline_dev`, `gfx.h`):
    text and panels on the grid are then worked out in integers
    (`font.c`, `render_panel`), a glyph a polygon rather than a rectangle
    a run of its pixels. With `GFX_GLOW` (as the PSP's) a glow is one
    textured square. Its `target.h` asks the core for what a coarse,
    slow screen needs, each off unless a target asks: `CURVE_DETAIL`
    (circles and rings cut by their radius in device pixels),
    `FX_MAX_PARTICLES` (fewer particles), `FLOAT_DIVIDE_SLOW` (a
    multiplication by an inverse where a float division would come twice:
    the same to millionths of a pixel, not to the bit) and
    `FACE_BUTTON_LETTERS` (the help texts name the face buttons A, B, Y,
    X). Its `sinf`, `cosf`, `expf` and `sqrtf` are its own (the C
    library's work in double precision), and it plays the synth's songs
    recorded at build time (`nds_tool`), as the GBA does, through the
    `audio.h` API. Speedups the DS needed in the shared drawing were made
    so that the other platforms' pictures stay the same to the bit.
- *The tile family* (Game Boy Advance, Game Boy Color): the level is
  streamed into a tile map, the player and the objects are sprites.
  - The Game Boy Advance runs `CORE_SRC` unchanged, the menus and the
    title's demo included, and supplies what the vector family's files
    would: `game_draw_init()` and `game_render()` (`game.h`), which draw
    the core's state each frame with tiles and sprites (`src/gba/gba_draw.c`),
    and the `audio.h` API, which plays the recordings `gba_tool` made. The
    vector family's drawing lives in files of its own for this
    (`game_draw.c`, `play_draw.c`, `demo_draw.c`, `fx_draw.c`); what both
    presentations need of it (the camera, the menus' palette, the
    difficulties' colours) is in the logic's files. Its `target.h` asks
    the core for what a CPU without floating point needs, each off unless
    a target asks: `FX_TARGET` (it implements `fx.h` itself, in fixed
    point), `LEVEL_PREBUILT` (the levels and the title's run parsed at
    build time, `level_prebuilt()` giving them) and `DEMO_SNAP_EVERY` (the
    title run's snapshots every block, made at build time with
    `demo_record()` and given back with `demo_take_snapshots()`). One more
    hook is about its sound, and the PSP asks for it too:
    `AUDIO_OUTPUT_OPTION` adds the options' OUTPUT row, the music mixed
    for headphones or for the console's own small speakers, kept in the
    save's `speaker` byte (a reserved byte before: the save's format and
    size are unchanged) and passed to `audio_set_output()` (`audio.h`).
    The synth (`audio.c`) then cuts the bass below 250 Hz and compresses
    its mix (the PSP); the GBA plays its second, speaker-mastered
    recording.
  - The Game Boy Color plays the levels from the data `gbc_tool` makes,
    with its own physics (layer 3) and music player. It plays the levels
    that fit it (`gbc_tool levels` lists them, and says why any other
    doesn't).

Where the system can put a menu of its own over the game (the PSP's HOME
menu), the frame loop stops ticking the game while it is there, without
making the ticks up afterwards, and tells the game with `game_suspend()`
(`game.h`), which pauses a run and keeps a button held as the menu goes
from counting as a press (or a direction from repeating); the synth's
`audio_suspend()` silences the sound meanwhile (`src/psp/main_psp.c` does
both).

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
  that includes `sources.mk` and builds both its lists (`CORE_SRC`, the
  game's logic and data, and `VECTOR_SRC`, the vector family's drawing and
  synth), adds the frontend's and puts its folder on the include path.
  Everything else is shared.
- **A 32-bit CPU without floating point**: the game's logic runs as it
  is, in software floating point (on the GBA a third of a frame on
  average, with the hottest routines in fast RAM). The synth uses floats
  per sample and won't fit: the host tool records the songs at build time
  (`gba_tool`, `nds_tool`). With a GPU that draws triangles (the DS, a
  PS1) the vector family's renderer can fit too, with the backend's
  capabilities and target options above (`src/nds`); without one, the
  presentation is the platform's own, as `src/gba` is, and the host tool
  can draw it with the renderer at build time, as `gba_tool` does.
- **An 8 or 16-bit CPU** (Game Boy, NES, Master System, Mega Drive): its own
  version of the physics, with a difftest against the reference like
  `gbc_tool difftest`, and a data tool like `gbc_tool` for its formats.

Whatever the platform, test it the way the handhelds are tested
(`scripts/gbc-emu-test.py`, `gba_test play`, `nds_test play`): play the solver's runs of
every level in an emulator, compare the player with the reference after
every tick, and check that no frame runs late.
