# Changelog

All notable changes to Pulse Dash are listed here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/). Each release on GitHub uses its
section from this file as release notes.

## [Unreleased]

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

### Fixed

- Stuttering scrolling on PS2: frames are now flipped right at the vertical
  blank (the audio thread could delay them before), and drawn interpolated
  between game ticks, which also makes PAL (50 Hz) consoles scroll evenly.

[Unreleased]: https://github.com/dwzg/pulse-dash/compare/v1.0.0...HEAD
[1.0.0]: https://github.com/dwzg/pulse-dash/releases/tag/v1.0.0
