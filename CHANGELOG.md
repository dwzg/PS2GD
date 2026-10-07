# Changelog

All notable changes to Pulse Dash are listed here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/). Each release on GitHub uses its
section from this file as release notes.

## [Unreleased]

## [1.3.0] - 2026-10-07

### Added

- A Game Boy Advance version (`pulsedash.gba`, built by
  `scripts/build-gba.sh` with arm-none-eabi-gcc, and by CI as the
  `pulsedash-gba` artifact). It runs the game's own code, the same files as
  the PC, PS2 and PSP: all six levels, practice, the results, the title
  screen's demo run (its control hint under the ground's line, clear of
  the run, as on the PC, beside the version: a build's commit, as on the
  Game Boy Color), the garage, the level select, the options with the
  audio delay, and saves in the cartridge's SRAM, with the same physics to
  the tick, at the full frame rate with no frame late (the menus, the
  fades and a level's loading included). Its graphics are drawn by the PC's
  renderer at build time (12 pixels to a block; what turns, at each step
  of its turn, the player's vehicles with their edges moved onto whole
  pixels and their lines a pixel wide, so that a line doesn't come and go
  as they turn; what has lines thinner than a pixel at that size, the
  saws, orbs, coins, portals and the ball, pixel by pixel in its shapes) and
  shown as tiles and sprites, with the level's colours, gradients, the
  beat, parallax, see-through panels, the finish's flash, the glows (round
  the player, the objects, the blocks, spikes and saws) and added light
  done by the video hardware;
  its songs and sound effects are the game's synth's, recorded at build
  time (8-bit at 21 kHz, the songs in stereo on both Direct Sound
  channels), and a second time mixed for the GBA's speaker (mono, the
  bass cut, compressed: OUTPUT in the options). See
  [docs/GBA_PORT.md](docs/GBA_PORT.md).
- `gba_tool` makes the ROM's graphics, levels and sound with the game's
  renderer, parser and synth, and `gba_tool audiocheck` checks their
  encoding; `gba_test play` visits the menus and plays every level in the
  ROM in mGBA's core, after an attempt that dies and with a pause, half of
  them with the speaker's mix, failing unless the player is the
  reference's after every tick, no frame is late anywhere, every attempt
  keeps time with its song, the sound's DMAs are restarted where they have
  read their buffers to the end, no text tile mixes two palettes, the pause
  menu and the results appear whole, and the progress is saved, still
  there after a restart and not lost to a save cut short.
- PSP: OUTPUT in the options, AUTO (the default: by whether headphones are
  plugged in), SPEAKERS or HEADPHONES: on SPEAKERS the
  synth's mix is made for the PSP's own small speakers, the bass they can't
  play cut below 250 Hz and the rest compressed, about 9 dB louder where
  they play (`audio_set_output`, also on the GBA; `pd_tool wav ... speaker`
  renders it on the PC).
- PS2: FLICKER FILTER in the options, for a tube TV (off by default, kept
  in the save's last reserved byte): the display's two read circuits show
  the frame a line apart, blended as ps2sdk's libgraph does it, so each
  line of the interlaced picture mixes two of the frame's and edges and
  thin lines no longer flicker as much. The display's merge circuit does
  it as it reads the frame out: it costs the drawing nothing.
- `pd_tool shot` and `pd_tool menu` take a height after the width, to
  stretch the picture: `640 512` is the PS2's PAL picture, drawn on its
  pixel grid.
- Releases carry the Game Boy Advance ROM too (`pulsedash-<version>.gba`,
  and a zip with the documents).
- PSP: smoothed edges. When the frame has the time, the GE draws it twice,
  a quarter of a pixel apart, and blends the two: the turning cube, the
  spikes, saws, orbs and the edges of everything moving come out without
  stair steps, while text, icons and the strokes on whole pixels stay as
  sharp as before. The second drawing replays the first one's batches into
  the VRAM a depth buffer had (the game never used it), so it costs the CPU
  next to nothing. It is guarded frame by frame: it starts only when it is
  sure to end before the vblank (it takes the GE no longer than the frame
  has taken so far up to the first drawing's end; the blend is timed), a
  frame without the time shows the first drawing alone, and the smoothing
  then stays off until frames have been light enough for two seconds. The
  perf build reports it (`smooth` line). Glows are drawn from a small round
  texture then, smooth instead of faceted (`gfx_glow`, an optional hook of
  `gfx.h`; the other backends keep the triangle fans).

### Changed

- The vector family's drawing of the menus, the levels, the title's demo
  and the particles is in files of its own (`game_draw.c`, `play_draw.c`,
  `demo_draw.c`, `fx_draw.c`, in `VECTOR_SRC`), so a platform can run the
  game's logic (`CORE_SRC`) with a presentation of its own; the camera,
  the menus' palette and the difficulties' colours moved into the logic.
- Faster on every platform, with the same results: a level is parsed in
  linear time (each line's length was measured again for every column),
  and the title's demo looks up its presses in fixed point.
- PSP: a frame is no longer cleared before its background is drawn over
  the whole screen (every screen has one), which saved the GE filling the
  screen twice; a frame that does not start with one is still cleared.
- The vector family (PC, PS2, PSP) moves the camera less than a pixel up or
  down so the ground lies on whole pixels: with the PSP's smoothing it
  stays a sharp line; elsewhere it looks the same as before.
- Game Boy Color: the player's turned frames are cleaner (turned from the
  art smoothed by Scale2x, with fewer lumps and stray corners; the ball
  keeps its round outline), the small saw is a disc with a ring of its
  teeth spinning inside it rather than an oval ring, and the speed portals'
  chevrons no longer run together (every other one white).
- Game Boy Color: a death looks as it does on the other versions: the
  screen shakes and flashes white, and the player bursts into particles in
  its two colours and white sparks, with a ring (it only blinked out, with
  8 particles).
- Game Boy Color: the sky is a gradient, from the level palette's top
  colour to its bottom one, in 8 bands (in levels and on the title), each
  set by an interrupt as the screen is drawn, the first by the vertical
  blank's, so that a long frame (a pause's, the results') never shows the
  last band's colour at the top.
- Game Boy Color: the music is in stereo on headphones, the arpeggio panned
  as the other versions pan its instrument (the pluck to the left, the saw
  pluck and the bell to the right; melody, bass and drums in the middle).
- Game Boy Color: the music's tick is assembly, a third of the time it
  took (3 scanlines a frame, at most 8 where it was 32), which leaves room
  for the sky; `scripts/gbc-emu-test.py` also dies six times in the first
  level and fails if a frame runs late around a death.
- PSP: the game stands still while the HOME menu is open (it played on
  behind it, a run included). A level being played pauses, its pause menu
  waiting under the HOME menu, so the run goes on only when it is resumed
  from there (one being faded into pauses as it starts); the menus, the
  title's demo, fades and the results don't move; the sound stops, and
  comes back where it was when the HOME menu is closed (a level's music
  when the run is resumed); and no time is made up then. The PSP's frame
  loop tells the game with `game_suspend()` (`game.h`) and silences the
  synth with `audio_suspend()`; HOME > Quit still saves before the game
  exits.

### Fixed

- After a pause the music was a tick (17 ms) ahead of the run, a tick more
  for every pause: neither the tick that paused nor the one that resumed
  moves the run, but the music started again with the second. It now
  starts again with the run's next tick, on the Game Boy Color too (its
  own play code had the same offset: the frame that paused ticked the
  music, not the run).
- The button that went on from a menu jumped as the run went on: resuming
  or restarting from the pause menu or replaying from the results with A
  (a jump button too), the press was a jump on the run's first tick, and
  so was the button still down from the level select. That button is now
  not a jump until it has been let go, as on the Game Boy Color.
- PS2 in PAL: text, icons and outlines came out uneven. The PAL picture
  is 512 lines tall, so a pixel of the 448-line virtual screen is 8/7 of a
  line, but they were put on whole pixels as if it were one: the rows of
  small text were 2 or 3 lines tall, outlines 3 or 4. The pixel grid
  (`draw.h`) has a height of its own now, set to the picture's in PAL, and
  they are drawn on whole lines of it, as even as in NTSC. NTSC, the PSP
  and the PC draw exactly what they did.
- Game Boy Color: the melody went silent after a coin, a checkpoint or a
  menu's sound until its next note (the sound effect borrows its channel);
  the note held comes back now, and so do the notes held through a pause.
- Game Boy Color: the kick switched the wave channel's DAC off and on to
  change its waveform, twice for every kick, which clicks on hardware; the
  channel is now stopped by its length counter the tick before, and the
  DAC stays on.
- Game Boy Color: the frame a run died in could run late (the physics and
  the save, the progress and "new best" in one frame); what a death counts
  for is now done two frames later.
- Game Boy Color: the pause menu and the results came up part-drawn,
  starting part-way down the screen for a frame, and a pause showed the
  stopped run for three frames while its menu was drawn. The pause menu is
  now drawn as the level begins, hidden, and shown whole from the frame
  after the pause; the results from the top of the frame after they are
  drawn. `scripts/gbc-emu-test.py` fails if the screen shows anything else
  while paused.
- PSP: in the level select the music crackled and slowed down on the
  console. The audio thread handed its chunks to the hardware itself, one
  mixed while the other played (about 11 ms), and ran below the game loop:
  when a frame's drawing kept the CPU longer than that, as the level
  select's did, the hardware ran dry, and the song, whose clock is the
  samples mixed, fell behind. A thread above the loop now hands the chunks
  over (it only waits, and wakes for a moment as each one starts), while
  the mixer, still below the loop so that mixing never delays a frame,
  keeps two more mixed ahead: the loop can keep the CPU for over 20 ms. The
  sound comes about 21 ms later than before, which the song clock allows
  for, so runs keep time with their songs and the audio delay set in the
  options still holds. Checked in PPSSPP with the game's and the synth's
  work made to take longer, for the console's slower CPU: at 2.5 times as
  long the level select's song ran 3 to 7% slow before (the title's kept
  time); now every screen keeps time at 3 times as long, and no frame is
  late.
- PC, PS2 and PSP: the title's logo stuttered on the PS2: it jumped on
  every beat, and text is drawn on whole pixels, so the jump came as a
  drop of 2 or 3 pixels in one frame. It now bobs on its slow sine alone,
  as on the Game Boy Advance.
- PC, PS2 and PSP: the level select drew the chosen level's card twice, its panel darker,
  when it opened and after scrolling left (once after scrolling right, as
  in the screenshots), drew a card a whole screen away as well, and read
  every card's level header (all of the level, for its coins) each frame.
  Each card is now drawn once, only while some of it is on screen, from
  headers read once: at rest the level select draws half the vertices it
  did, fewer than the title. After scrolling right it looks exactly as it
  did.

## [1.2.0] - 2026-10-04

### Added

- A Game Boy Color version (`pulsedash.gbc`, built by `scripts/build-gbc.sh`
  with GBDK-2020, and by CI as the `pulsedash-gbc` artifact): all six
  levels with every vehicle, orb, pad and portal, coins, practice mode with
  checkpoints, the finish line and the results with their fireworks, the
  title screen over the game playing itself (the other versions' demo run,
  on the beat of the menu song), the garage (the eight icons and two of
  the 14 colours), level select, and saves in the cartridge's battery RAM,
  at the Game Boy's full frame rate. Each level's song is arranged for the
  four sound channels: melody and arpeggio on the pulse channels, the bass
  on the wave channel an octave up (where a small speaker plays it), the
  kick a falling sine there as in the other versions, snares and hats on
  the noise channel. Its physics gives exactly the same results as on the
  other platforms, tick for tick; see [docs/GBC_PORT.md](docs/GBC_PORT.md)
  for what the platform allows. Played on a Game Boy Advance.
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
  (`src/host/solver.c`).
- What an attempt counts for (when it counts, the percentage, bests,
  coins) is one set of rules for every platform (`src/core/progress.c`,
  integer C the Game Boy compiles too). Restarting from the pause menu
  counts as a death once the run has gone half a second; a quicker
  restart is free.
- Past the finish line the player levels out (a ship, UFO or wave stops
  climbing or diving, a cube lands its spin on a side) and speeds off the
  screen, on every platform.
- Garage: the five vehicle previews are all one size. The shoulder
  buttons, which only made one of them larger ("PREVIEW"), no longer do
  anything there.
- A platform's screen width and button names come from a `target.h` in its
  source folder instead of `#ifdef`s in `src/core/common.h`, and the
  makefiles take the shared sources from `sources.mk`.

### Fixed

- A ship flew off past the finish line at the angle it crossed the line
  at; it levels out now (above).
- Garage: the chosen icon was drawn larger than the others, its frame
  touching the edge of its panel; it is framed as the chosen colours are.

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

[Unreleased]: https://github.com/dwzg/pulsedash/compare/v1.3.0...HEAD
[1.3.0]: https://github.com/dwzg/pulsedash/compare/v1.2.0...v1.3.0
[1.2.0]: https://github.com/dwzg/pulsedash/compare/v1.1.0...v1.2.0
[1.1.0]: https://github.com/dwzg/pulsedash/compare/v1.0.0...v1.1.0
[1.0.0]: https://github.com/dwzg/pulsedash/releases/tag/v1.0.0
