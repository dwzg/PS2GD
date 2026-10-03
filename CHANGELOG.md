# Changelog

All notable changes to Pulse Dash are listed here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/). Each release on GitHub uses its
section from this file as release notes.

## [Unreleased]

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

- The NEW BEST popup: its black outline grows with the text as it pops in
  (it used to flash a black blot for the first frames), it fades out evenly
  (it used to fade in stripes where the outline's pieces overlapped), and
  it sits higher, above the next attempt's counter, instead of running
  into it. The LEVEL COMPLETE title pops in the same way.
- The glow over the ground's surface line stopped short of the screen
  edges with hard ends; it now fades out towards the edges with the line.
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

[Unreleased]: https://github.com/dwzg/pulse-dash/compare/v1.0.0...HEAD
[1.0.0]: https://github.com/dwzg/pulse-dash/releases/tag/v1.0.0
