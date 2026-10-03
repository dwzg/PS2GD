# Pulse Dash

A rhythm platformer for the **PlayStation 2**, inspired by Geometry Dash:
tap to jump, hold to fly, and get through each level in one go, in time
with the music.

![Title screen](docs/screenshots/title.png)

All levels, music, graphics and code in this repository are original. The
game plays like Geometry Dash (the vehicles, orbs, pads, portals, practice
mode) but contains none of its levels, songs, artwork, names or code. It is
an unofficial fan project, not affiliated with or endorsed by RobTop Games;
"Geometry Dash" is their trademark.

![One frame from each level](docs/screenshots/levels.png)

## Features

- **6 levels**, Easy to Demon, each with its own song, palette changes and 3
  secret coins: Neon Steps, Skyward Pulse, Gravity Garden, Saucer Groove,
  Wave Rider, Prism Overdrive.
- **Every jump is on the beat**: each level is checked to be beatable when
  every tap lands on an 8th note of its song, and blocks, spikes, orbs and
  pads pulse with the music. If the sound arrives late or early on your TV
  (soundbars and AV receivers add delay), set **Options > Audio delay**: a
  metronome plays and four lights flash with the beat; move the value until
  the flashes land on the kick.
- **5 vehicles**: cube, ship, ball, UFO and wave, plus gravity flips and four
  speeds.
- Yellow/pink/blue/green **orbs**, yellow/pink/blue **pads**, spikes, saws.
- **Practice mode** with checkpoints, attempt counter, best-percent tracking,
  pause menu, level-complete stats.
- **Garage**: 8 cube designs, 14 colours, previews of every vehicle.
- **Original soundtrack, synthesized live**: a small software synth (supersaw
  leads, plucks, filtered bass, drums with sidechain, echo) plays 8 original
  tracks from note data in `src/core/songs.c`. No audio files ship with the
  game.
- **Memory card saves** (`mc0:/PULSEDASH`, with a browser icon), falling back
  to the second slot.
- Vector graphics drawn entirely from GS primitives (no textures), 640x448.
  NTSC (59.94 Hz) and PAL (50 Hz): the game logic runs at a fixed 60 Hz and
  every frame is drawn interpolated to the moment it is shown, so the
  scrolling is even at either refresh rate.

## Controls

| Button | Action |
|--------|--------|
| ✕ / ○ / ↑ / L1 / R1 | Jump; hold to fly (ship) or climb (wave) |
| START | Pause |
| □ (practice) | Place checkpoint |
| △ (practice) | Remove last checkpoint |
| ✕ / □ / ○ in the level menu | Play / practice / back |

## Running it

**On a PS2:** download `pulsedash-packed.elf` from the latest
[release](../../releases/latest) (or build it, below), copy it to a USB
stick and launch it with wLaunchELF/uLaunchELF (for example from
FreeMcBoot), or from OPL's apps list. The version is shown in the corner of
the title screen.
Any controller in port 1 or 2 works; progress is saved to the memory card
in slot 1 (or slot 2).

**In an emulator:** [Play!](https://purei.org) boots the ELF directly
(File > Boot ELF), no BIOS needed. PCSX2 can boot it too ("Run ELF") with
your own BIOS dump.

**On a PC:** the same game builds as an SDL2 program (keyboard: Space/Up to
jump, Esc to pause, Q/E for checkpoints, Backspace to go back; gamepads
work too).

## Building

PS2, using the official toolchain image (needs Docker):

```sh
scripts/build-ps2.sh          # -> build/ps2/pulsedash.elf and pulsedash-packed.elf
```

or with a local [ps2dev](https://github.com/ps2dev/ps2dev) install
(`PS2DEV`, `PS2SDK`, `GSKIT` set): `make`.

Builds show `git describe` on the title screen (the tag for a release, else
the commit); `make VERSION=...` overrides it.

PC version and developer tools (needs SDL2):

```sh
make -f Makefile.host         # -> build/host/pulsedash, build/host/pd_tool
build/host/pulsedash
```

GitHub Actions builds the ELF (uploaded as an artifact) and runs the tests
on every push.

### Releases

Describe the version in [CHANGELOG.md](CHANGELOG.md) (a `## [1.1.0] - date`
section), then push a version tag; CI publishes a GitHub release once the
tests and the PS2 build pass:

```sh
git tag v1.1.0
git push origin v1.1.0
```

The release carries `pulsedash-packed.elf`, `pulsedash.elf`, a zip with both
plus the README, changelog and licenses, and `SHA256SUMS`. Its notes are the
version's changelog section followed by the merged pull requests. Tags with
a hyphen (`v1.1.0-rc1`) become pre-releases.

## Testing

```sh
make -f Makefile.host test        # data checks, smoke test, solver, rhythm, coins
make -f Makefile.host test-full   # also proves levels beatable at 30 and 20 Hz input
scripts/emu-test.sh               # run the real PS2 ELF in the Play! core, headless
```

`pd_tool` contains a level solver: a breadth-first search over button
presses that proves every level can be finished, that it can be finished
when inputs only change at 20 Hz (no frame-perfect tricks), that every
portal is mandatory, and that all three coins can be collected in one run.
All six levels pass at every input phase. `pd_tool rhythm` runs the same
search with presses allowed only on the 8th notes of the level's song (a
couple of ticks early or late), which is what makes the jumps line up with
the music; see [docs/LEVEL_FORMAT.md](docs/LEVEL_FORMAT.md).

`scripts/emu-test.sh` builds a small harness around the
[Play!](https://github.com/jpd002/Play-) emulator core, boots the PS2 ELF,
presses buttons on a schedule and prints the game's log. With
`PD_SHOTS=150,300` it also renders through Play!'s OpenGL GS on a headless
Mesa context and saves screenshots. These frames come from the PS2 build:

![PS2 build running in the Play! core](docs/screenshots/ps2-emulator.png)

That harness found two problems that would also have hit real hardware:
the open-source pad driver stalled, so the game now uses the BIOS pad and
memory card modules, and the audio thread was starved by gsKit's vsync
busy-wait. The game loop now sleeps on a semaphore that the vblank interrupt
signals, flips right at the vblank and runs above the audio thread, so
mixing fills the idle time and can never delay a flip.

`make PERF=1` (or `scripts/build-ps2.sh PERF=1`) builds
`build/ps2-perf/pulsedash.elf`, which logs how much of each frame the game
logic, drawing and synthesizer take, measured with the EE cycle counter, and
how many frames missed their vblank. In the Play! core (which counts roughly
one cycle per instruction, so real hardware needs more), a level uses about
2% of a frame for logic and drawing and 8% for the music, and no frame is
late. A perf build also prints a marker when an attempt starts, and
`pd_tool script` turns the rhythm check's on-beat presses into harness
input (see the comment in `scripts/emu-test.sh`): replayed on the PS2 ELF,
Neon Steps is played on the beat up to its first ship section on the first
attempt.

## Making levels

Levels are ASCII art in `src/levels/*.c`; see
[docs/LEVEL_FORMAT.md](docs/LEVEL_FORMAT.md) for the tile set, physics rules
of thumb and the checking workflow.

## Layout

```
src/core/     portable game: physics (sim.c), levels, rendering, menus,
              synth + songs, saves. Plain C99, single-precision floats only.
src/levels/   the six levels
src/ps2/      PS2 frontend: gsKit renderer, audsrv streaming thread,
              libpad, libmc saves + browser icon, boot/module loading
src/host/     SDL2 frontend and pd_tool (solver, screenshots, WAV export)
tools/play-harness/   emulator test harness (used by scripts/emu-test.sh)
tools/beat_align.py   moves a level's obstacles onto the beat of its song
```

## Status

Runs on real PS2 hardware, in the Play! emulator core (boot, module loading,
controller input, audio streaming, memory card save/load and GS rendering)
and on PC. Not yet tried in PCSX2.

## License

[MIT](LICENSE). The libraries the builds use, and the trademarks mentioned
here, are listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md); the
emulator test harness keeps Play!'s BSD license. Changes are tracked in
[CHANGELOG.md](CHANGELOG.md).
