# Changelog

All notable changes to Pulse Dash are listed here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/). Each release on GitHub uses its
section from this file as release notes.

## [Unreleased]

### Added

- A Game Boy Color version (`pulsedash.gbc`, built by `scripts/build-gbc.sh`
  with GBDK-2020, and by CI as the `pulsedash-gbc` artifact): all six
  levels with every vehicle, orb, pad and portal, coins, practice mode with
  checkpoints, each level's song arranged for the four sound channels,
  palette changes with the beat flash, the title screen over the game
  playing itself (the other versions' demo run, on the beat of the menu
  song), the garage (the eight icons and two of the 14 colours), level
  select, and saves in the cartridge's battery RAM, at the Game Boy's full
  frame rate. Its
  physics gives exactly the same results as on the other platforms, tick
  for tick; see [docs/GBC_PORT.md](docs/GBC_PORT.md) for what the platform
  allows.
- `gbc_tool` makes the ROM's data from the game's levels, songs, font and
  colours, and `gbc_tool difftest` plays every level with the Game Boy's
  physics and the reference side by side, failing on the first tick they
  differ; `scripts/gbc-emu-test.py` plays all six levels in the ROM in
  PyBoy, failing unless the player is where the reference puts it on every
  tick and no frame runs late.
- [docs/PORTING.md](docs/PORTING.md): how the code is shared between the
  platforms, and what a new one needs.
- Releases carry the Game Boy Color ROM too (`pulsedash-<version>.gbc`, and
  a zip with the documents).
- The Game Boy Color plays the levels that fit it (`gbc_tool levels`): a
  new level too high or too dense for it is left out of the ROM instead of
  failing the build.

### Changed

- The physics is integer arithmetic now (`src/core/sim.c`, every number of
  it in `src/core/sim_rules.h`), so the game plays exactly the same on
  every platform. Before, it used floats, which a PS2 rounds differently
  from a PC: a run that just made it on one could fail on the other. The
  difference is about a hundredth of a block per jump, too little to feel,
  but four spots were tuned that finely and moved by a block:
  - Neon Steps: the first coin, in a ship section, one column later (it
    could no longer be reached).
  - Skyward Pulse: the middle orb of the orb chain over the long spike pit
    one row lower.
  - Gravity Garden: a double spike near the end one column earlier.
  - Prism Overdrive: the double spike before the two-high step after the
    gravity section one column later.

  All six levels pass every check as before (beatable, at 30 and 20 Hz
  input, on the beat 2 ticks early or late, all coins in one run).
- The level solver is shared by `pd_tool` and `gbc_tool`
  (`src/host/solver.c`); `gbc_tool solve` and `rhythm` are gone, as the
  Game Boy plays the same physics as `pd_tool` checks.
- What an attempt counts for is one set of rules for every platform
  (`src/core/progress.c`, integer C the Game Boy compiles too), so the
  platforms count the same:
  - The Game Boy Color counts an attempt when it starts, as the others do
    (it counted deaths, practice ones too, and not a finish), and keeps
    total jumps and attempts.
  - Its percentage is of the exact position, as the others' is (it was of
    the column, so near the start it could show a percent less).
  - Restarting from the pause menu counts as a death on every platform once
    the run has gone half a second (the Game Boy didn't count it, the
    others always did).

  A save from the Game Boy Color's first version is carried over.
- Past the finish line the player levels out (a ship, UFO or wave stops
  climbing or diving, a cube lands its spin on a side) and speeds off the
  screen, on every platform. A ship used to fly off at the angle it
  crossed the line at.
- Garage (PC, PS2, PSP): the chosen icon is framed as the chosen colours
  are, clear of its panel's edge (it was drawn larger, its frame touching
  the edge), and the five vehicle previews are all one size. The shoulder
  buttons, which only made one of them larger ("PREVIEW"), no longer do
  anything there.
- A platform's screen width and button names come from a `target.h` in its
  source folder instead of `#ifdef`s in `src/core/common.h`, and the
  makefiles take the shared sources from `sources.mk`.

### Fixed

- Game Boy Color version, found playing it on a Game Boy Advance:
  - The A press that starts a level (or resumes it from the pause menu)
    no longer makes the cube jump while it is still held.
  - Drums: the kick is now a falling tone on the wave channel, as in the
    other versions (a noise burst there crackled and had no low end), with
    a short click on the noise channel; the bass pauses for it and comes
    back, like the other versions' sidechain. The kick starts at 330 Hz,
    where a GBA's or Game Boy's small speaker can play it, before falling. Snares and hats stop after their length instead of
    ringing on, no noise is clocked slowly enough to crackle, and the
    melody is a little quieter so that the drums come through. Held notes and sound
    effects end at volume 0 instead of switching the channel off, which
    popped.
  - "NEW BEST" appears in the sky above the level, and above the attempt
    counter at the restart, instead of over the ground.
  - The version on the title screen ran into the logo when it was longer
    than the screen is wide; it is shortened (`v1.1.0-6-9906db9`, `*` for
    a modified checkout) and cut at the screen's edge.
  - The level card showed coins 2 and 3 as collected and coin 1 as not,
    whatever was saved: SDCC 4.2 miscompiled `coins >> i` there (it shifted
    a register that didn't hold the coins). The results screen had the
    same code.
  - The bass plays at the wave channel's full level instead of half: in
    the other versions it is the loudest melodic part.
  - The block edges and the ground line pulse visibly with the beat: their
    colours are nearly white in every palette, so a flash towards white
    hardly showed. They now rest dimmed towards the colour beside them and
    light up on each beat, fully on a bar's first.
  - Palettes and new background columns are written to the hardware in
    assembly, within the vertical blank: in C they could run past it, where
    the LCD can lose palette writes and every byte waits for it.
  - The saw and square bass sounded thin and buzzy: they played as low as
    the other versions' (down to 41 Hz), below what a small speaker plays,
    in waveforms of three harmonics (left so that the kick, played in the
    same waveform, would thump). They play an octave up now, in waveforms
    of their first eight or nine harmonics tapering off like the other
    versions' filtered saw and square, and the kick has a sine wave of its
    own.
  - There was no finish line: it is drawn now, a white line in a glow over
    the sky, as in the other versions.
  - Starting a level left the screen off (white on a Game Boy Color) for
    0.6 s, most of it spent working out where the progress bar steps up,
    with 32-bit divisions. That takes a fraction of a frame now
    (`progress_steps`, additions only). And the screen no longer goes
    white between screens at all: the LCD stays on, with every palette
    black for the 0.15 s the next screen takes to draw.
  - The results after a level were drawn again every 4.3 s, which
    flickered, and A or START did nothing for 1.2 s of every 4.3 s: the
    frame counter went round. The fireworks still go round.
  - After dying past about 70%, a part of the progress bar could stay lit
    in the next attempt: a restart changes more of the bar than a frame
    can draw, and what didn't fit was never drawn. It is drawn the next
    frame now.

## [1.1.0] - 2026-10-04

### Added

- A PSP version: `EBOOT.PBP` for a PSP with custom firmware, a PS Vita with
  Adrenaline, or PPSSPP. It fills the PSP's 16:9 screen (levels show more of
  what is ahead, menus are centred for it), draws text, icons and outlines
  on whole pixels of the 480x272 LCD so they stay sharp and even (the
  cube's borders come out the same width on every side, block edges keep
  their width as the level scrolls), plays the synthesized soundtrack at
  48 kHz, and saves progress next to the game on the memory stick. HOME >
  Quit saves before exiting. The XMB icon and background are drawn by the
  game's own renderer.
- Releases also carry a PSP zip that unpacks to `PSP/GAME/PulseDash/`.
- The PC version and tools can be built in the PSP's layout
  (`make -f Makefile.host PSP=1`); `pd_tool` writes PNG images and takes an
  output width for menu screenshots.
- `scripts/psp-emu-test.sh` runs the PSP build in PPSSPP's headless runner
  with scripted button presses and screenshots.

### Fixed

- The NEW BEST popup: it grows in smoothly over a third of a second, its
  black outline with it (it used to flash a black blot for the first
  frames), it fades out evenly (it used to fade in stripes where the
  outline's pieces overlapped), and it sits higher, above the next
  attempt's counter, instead of running into it. The LEVEL COMPLETE title
  pops in the same way.
- The glow over the ground's surface line stopped short of the screen
  edges with hard ends; it now fades out and thins towards the edges with
  the line.
- Blocks' glow now goes round their corners instead of leaving them dark.
- The percentage next to the progress bar, and the NORMAL / PRACTICE rows
  beside the bars in the level card and the pause menu, are centred on
  their bars; the option names on their rows.
- The analog stick (the PSP's nub) only works as a d-pad once it has been
  seen near its centre: a PSP-1000's nub can read far off centre at boot
  until it is moved, which pressed directions (and up jumps).
- Uneven borders on the PS2 and PC: the resting cube's black frame came out
  2 pixels wide on two sides and 3 on the others, and thin outlines
  changed width as the level scrolled. Text, icons and thin lines are now
  drawn on whole screen pixels in every build (1:1 on the PS2, the window's
  scale on the PC), as on the PSP.

## [1.0.0] - 2026-10-03

First release.

### Added

- Six original levels, Easy to Demon, each with its own song, palette
  changes and three secret coins: Neon Steps, Skyward Pulse, Gravity
  Garden, Saucer Groove, Wave Rider and Prism Overdrive.
- Five vehicles (cube, ship, ball, UFO, wave), gravity portals, four speeds,
  yellow/pink/blue/green orbs, yellow/pink/blue pads, spikes and saws.
- Practice mode with checkpoints, a pause menu, attempt counter, best
  percentages and level-complete stats.
- Garage with 8 cube designs and 14 colours.
- An original soundtrack of 8 songs, synthesized live on the console (no
  audio files).
- Memory card saves in `mc0:/PULSEDASH` (or slot 2), with a browser icon.
- Options: music and sound effect volume, and an audio delay setting with a
  metronome and beat lights for TVs and speakers that play the sound late.
- Every level can be played pressing on the beat of its music, and blocks,
  spikes, orbs and pads pulse with it.
- Releases ship a single `PULSEDASH.ELF` (compressed, it unpacks itself at
  boot) plus a zip with the documentation and licenses.
- The version is shown on the title screen.
- A PC version (SDL2) and developer tools: a level solver that also checks
  on-beat play, a tool that moves obstacles onto the beat, and an emulator
  test harness.

### Changed since the first test on real hardware

- Neon Steps was rebuilt as a proper first level: no spiked ship tunnel, no
  triple spikes, wide-open ship sections, and every jump on a beat.
- The other levels were retimed so their jumps land on the beat too.
- Songs keep a kick drum from the first bar, and the music is offset by the
  audio latency so that what you hear matches the level.
- Neon Steps' orbs sit one row lower, where they are easy to tap. Orbs in
  Skyward Pulse moved so that they can be tapped on the beat too: the rhythm
  check no longer lets a held button fire an orb.
- The title screen shows real gameplay: the game plays a short level on a
  loop behind the menu, jumping on the beat of the menu music (it follows
  the music, including the audio delay setting). It used to be a cube
  hopping in place while spikes slid past.
- Obstacles stand out better: spikes, saws and blocks glow in their outline
  colour, outlines are thicker, and the background squares are fainter and
  thinner so they can't be mistaken for blocks. This helps most in the dark
  red and night palettes.

### Fixed

- A short freeze when crossing the finish line on PS2: the game saved to
  the memory card right then, and waited for it. Saves are now written in
  the background, and the camera glides to a stop past the finish instead
  of stopping dead.
- Stuttering scrolling on PS2: frames are now flipped right at the vertical
  blank (the audio thread could delay them before), and drawn interpolated
  between game ticks, which also makes PAL (50 Hz) consoles scroll evenly.

[Unreleased]: https://github.com/dwzg/pulsedash/compare/v1.1.0...HEAD
[1.1.0]: https://github.com/dwzg/pulsedash/compare/v1.0.0...v1.1.0
[1.0.0]: https://github.com/dwzg/pulsedash/releases/tag/v1.0.0
