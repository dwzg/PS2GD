# Level format

Levels are ASCII art compiled straight into the game. Each level is one C
file in `src/levels/` holding a `NULL`-terminated array of strings, and is
listed in `src/levels/levels.c`.

```c
const char *const LEVEL_MY_LEVEL[] = {
"#name My Level",
"#song 0",        /* index into the level songs (0 = "Neon Steps") */
"#diff 1",        /* 0 easy, 1 normal, 2 hard, 3 harder, 4 insane, 5 demon */
"#stars 3",
"#speed 1",       /* starting speed portal 0..3 */
"#pal 0",         /* starting palette 0..9 (see src/core/theme.c) */

"|                                        ",
"|                    ##                  ",
"|        ^^      ^   ##    ^^^     ^^    ",
"!                         3              ",
"",
NULL};
```

## Sections

A section is a run of consecutive lines starting with `|` (or `!`). Sections
are placed left to right; the **last `|` line is row 0**, sitting on the
ground, and rows count upwards. All lines of a section must have the same
width (`pd_tool check` reports ragged sections). Any other line (`""`, a C
comment) ends the section.

A line starting with `!` is a trigger row: a digit in a column starts a
smooth palette change to that palette when the player reaches the column.

## Tiles

| Char | Object | Notes |
|------|--------|-------|
| `#` | block | solid; landing on top is safe, hitting a side kills |
| `_` `=` | half block (bottom / top half) | |
| `^` `v` | spike up / down | |
| `,` `` ` `` | small spike up / down | |
| `*` `@` | big / small saw | big saws reach about one block around the cell |
| `y` `p` | yellow / pink orb | press while touching: full / small jump |
| `b` `g` | blue / green orb | flip gravity / flip gravity and jump |
| `Y` `P` `B` | yellow / pink / blue pad | touch to launch; on a ceiling when there is a block above and none below |
| `C` `S` `A` `U` `W` | cube / ship / ball / UFO / wave portal | 3 blocks tall, centred on the cell |
| `G` `N` | gravity flip / normal portal | |
| `0` `1` `2` `3` | speed portal: 0.8x, 1x, 1.25x, 1.5x | 8.4 / 10.5 / 13.1 / 15.75 blocks per second |
| `$` | coin | every level should have three |
| space `.` | empty | |

Ship, ball, UFO and wave play inside a 10-block corridor whose floor is
picked from the portal's height (portals near the ground give a corridor
from row 0 to row 9).

## Physics rules of thumb

These are the numbers the levels were built against (normal speed):

- A cube jump rises **2.3 blocks** and covers **4.5 blocks**. Two-high walls
  can be jumped onto, three-high cannot. Three spikes in a row is the
  maximum on flat ground (impossible at 0.8x speed).
- A yellow pad launches about **4.5 blocks** high: a platform with its top at
  row 4 should start about 3 columns after the pad.
- Chained yellow orbs climb about one row per **4 columns**.
- A green orb first kicks the player away from the new gravity (about 2
  blocks), so keep its underside free of spikes.
- A full ball flip crosses the corridor in about **4.2 blocks** (5.2 at 2x).
- Pads and orbs must sit on something or be reachable mid-jump; a pad
  floating in mid-air can only be triggered by landing on it.

## Playing on the beat

The level's song starts with every attempt, at the same moment the player
starts moving, so each 8th note of the song falls at a fixed place in the
level. A level should be beatable by pressing *on* those notes: then the
jumps follow the music, and a player who taps along with the kick is
rewarded instead of punished.

- At speed `v` (blocks per second) and tempo `bpm`, a beat is `v * 60 / bpm`
  blocks. Neon Steps (126 BPM, normal speed) has exactly 5 blocks per beat,
  20 per bar, so its 40-column sections are two bars each.
- `pd_tool ruler N` prints the level source with the beat grid above each
  section: a digit where a bar starts, `+` on the other beats and `.` on the
  8th notes in between, at the column the player is in at that moment.
- Where to put things relative to a beat column `b` (normal speed; the solver
  measured these press windows):
  - a spike at `b+2` (the press window is 17 ticks wide, centred near `b`),
  - a double spike at `b+1` and `b+2`,
  - a step up (one or two blocks) starting at `b+3`,
  - triple spikes have a 5-tick window at normal speed: avoid them in easy
    levels.
- A yellow pad that the player walks onto lands about 6 blocks later; keep
  pits under a pad to three spikes. Don't put a pad where a jump lands: hit
  from the air it fires early.
- An orb is touched for about two blocks: put it where the player will be on
  the 8th note after a jump (for example jump on `b`, orb at `b+3` in row 3).

`pd_tool rhythm N [tol]` checks it: the level must be beatable when every
press in the cube, ball and UFO lands on an 8th note, at a fixed `tol` ticks
early, on time and late (default 2 ticks, about 33 ms; Neon Steps passes
with 3). Ship and wave are steered by holding, so they are not constrained.
`make -f Makefile.host test` runs it for every level.

To retime an existing level without redesigning it, `tools/beat_align.py`
moves its obstacles onto the beat: wherever the check gets stuck it inserts
or deletes a plain column in the run-up (or nudges an orb or pad, or
shortens a spike pit) and keeps the edit that gets furthest, then reruns the
normal checks before writing the file.

## Portals must be unavoidable

A portal that the player can fly over leaves them in the wrong mode for the
rest of the level. Rules that keep portals honest:

- Put ground-level portals (including speed portals) on **row 1**, never
  row 0: a jumping cube can't clear them there.
- When leaving a ship/ball/UFO/wave section, build a **funnel**: blocks above
  and below the portal so the opening is no taller than the portal's three
  rows (see the end of the ship sections in `neon_steps.c`).
- Close the space above "ceiling" blocks too; the corridor ceiling is at
  row 10, so a gap in row 9 is a bypass route.

## Checking a level

```sh
make -f Makefile.host
build/host/pd_tool check            # parse everything, report authoring slips
build/host/pd_tool solve 0 3        # beatable? also at 30 and 20 Hz input
build/host/pd_tool rhythm 0         # beatable pressing on the beat?
build/host/pd_tool ruler 0          # source with the beat grid
build/host/pd_tool coins 0          # all coins in one run?
build/host/pd_tool overview 0 out.bmp   # whole-level map with the solver's path
build/host/pd_tool trace 0 200 260  # player state along the solver's path
build/host/pd_tool trace 0 200 260 2  # ... along the rhythm check's run (2 ticks late)
build/host/pd_tool shot 0 12.5 out.bmp  # screenshot 12.5 s into the level
```

The solver is a breadth-first search over button states. With `K=3` inputs
may only change every third frame (at any of the three phases), which
models a human reacting at 20 Hz: a level that passes `solve N 3` has no
frame-perfect inputs. It also fails a level if the winning run skips a
portal. Every command that takes a level number also takes the path of a
text file holding a level (the strings of a level file, one per line), which
is how `tools/beat_align.py` tests its edits.
