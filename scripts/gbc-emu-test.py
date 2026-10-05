#!/usr/bin/env python3
"""
Run the Game Boy Color ROM in PyBoy, headless, and play levels with the
solver's inputs (gbc_tool script): proves the ROM's player is where the
reference physics (src/core/sim.c, gbc_tool replay) puts it on every tick
of each level, and that every frame of it fits in the CPU's time (a late
frame slows the game and its music down by a frame).

  scripts/gbc-emu-test.py [--levels 0,1,..] [--shots DIR] [--every N]
                          [--allow-late] [--perf] [--perf-csv FILE]

needs: pip install pyboy (2.x), and build/gbc/pulsedash.gbc
(scripts/build-gbc.sh), build/host/gbc_tool. Exits non-zero if a level
isn't finished, the player differs from the reference's on any tick, its
results are drawn more than once, the music's beats fall on other ticks of
the run after a pause (the first level, played again with one), the screen
shows anything but the run and then the whole pause menu (from the next
frame) or results, or the sky's first band in another colour, or it goes
white between screens (the LCD off), or (unless --allow-late) frames took
too long, in the levels or around six deaths in the first. --perf plays the
PERF=1 build (build/gbc-perf) instead and prints how many scanlines each
part of a frame took (its timers make it slower: late frames are expected
there).
"""
import argparse
import io
import os
import subprocess
import sys

import numpy
from pyboy import PyBoy

ROM = "build/gbc/pulsedash.gbc"
ROM_PERF = "build/gbc-perf/pulsedash.gbc"
TOOL = "build/host/gbc_tool"
SCR_TITLE, SCR_SELECT, SCR_PLAY, SCR_GARAGE = 1, 2, 3, 4
J_A, J_START = 0x10, 0x80
PH_RUN, PH_DEAD, PH_RESPAWN, PH_COMPLETE = 0, 1, 2, 3
# the timings of a PERF=1 build (src/gbc/gbc.h, src/gbc/gbsim.c), in scanlines
PERF = ["frame", "sim", "music", "stream", "hud", "pal", "sprites"]
PERF_SIM = ["physics", "x", "solids", "inner", "objects", "broad"]
LINES_PER_FRAME = 154

# GsPlayer (src/gbc/gbsim.h) as SDCC lays it out: no padding
OFF_X, OFF_Y, OFF_VY, OFF_GRAV, OFF_MODE, OFF_DEAD, OFF_DONE, OFF_COINS, OFF_TICKS, OFF_JUMPS = \
    0, 6, 10, 16, 17, 21, 22, 23, 26, 28


def symbols(noi):
    sym = {}
    for line in open(noi):
        p = line.split()
        if len(p) == 3 and p[0] == "DEF":
            sym[p[1]] = int(p[2], 16)
    return sym


def gb_levels():
    """the game's number of each level on the Game Boy (gbc_tool levels)"""
    out = subprocess.run([TOOL, "levels"], check=True, capture_output=True, text=True).stdout
    return [int(line.split()[1]) for line in out.split("\n") if line]


def load_replay(level, script):
    """the reference's player after each tick: {ticks: (x, y, vy, mode, grav)}"""
    out = subprocess.run([TOOL, "replay", str(level), script], check=True, capture_output=True, text=True).stdout
    ref = {}
    for line in out.split("\n"):
        if line:
            t, x, y, vy, mode, grav = map(int, line.split()[:6])
            ref[t] = (x, y, vy, mode, grav)
    return ref


def signed(v, bits):
    return v - (1 << bits) if v >= 1 << (bits - 1) else v


def load_script(path):
    """held per tick, and the tick the host's run finishes on"""
    held, finish = {}, None
    for line in open(path):
        if line.startswith("#"):
            finish = int(line.split("after ")[1].split()[0])
            continue
        t, n = map(int, line.split())
        for k in range(n):
            held[t + k] = True
    return held, finish


# SaveData (src/gbc/gbc.h) as SDCC lays it out: Progress (src/core/progress.h), then the garage
SAVE_BEST, SAVE_BEST_PRACTICE, SAVE_COINS, SAVE_ATTEMPTS = 0, 16, 32, 48  # [16] each, attempts 32-bit
SAVE_ICON, SAVE_COL1, SAVE_COL2 = 120, 121, 122
ST_CUBE, CUBE_FRAMES = 128, 6  # src/gbc/gfx_ids.h
SRAM_SIZE = 8192


def save_v1(best, best_practice, coins, attempts):
    """the cartridge RAM of the first version's save ("PDG1", six levels)"""
    body = bytes(best) + bytes(best_practice) + bytes(coins) + b"".join(a.to_bytes(2, "little") for a in attempts)
    s = 0x5A
    for b in body:
        s = ((s << 1 | s >> 7) & 0xFF) ^ b
    return (b"PDG1" + body + bytes([s])).ljust(SRAM_SIZE, b"\0")


class Game:
    def __init__(self, rom, sram=bytes(SRAM_SIZE)):
        # the cartridge's RAM from sram, not from a file next to the ROM
        self.pb = PyBoy(rom, window="null", cgb=True, sound_emulated=False, ram_file=io.BytesIO(sram))
        self.pb.set_emulation_speed(0)
        self.sym = symbols(os.path.splitext(rom)[0] + ".noi")
        self.m = self.pb.memory
        self.frames = 0
        self.selected = 0
        self.lcd_off = 0  # frames the LCD was off (a Game Boy Color shows white)

    def u8(self, name, off=0):
        return self.m[self.sym[name] + off]

    def u16(self, name, off=0):
        a = self.sym[name] + off
        return self.m[a] | self.m[a + 1] << 8

    def u32(self, name, off=0):
        return self.u16(name, off) | self.u16(name, off + 2) << 16

    def save(self, field, level, size=1):
        """a field of g_save.progress for a level (by the game's number)"""
        a = self.sym["_g_save"] + field + level * size
        return sum(self.m[a + k] << (8 * k) for k in range(size))

    def rom(self, name, off, n):
        """n bytes of a symbol's data in its ROM bank"""
        a = self.sym[name]
        return [self.pb.memory[a >> 16, (a & 0xFFFF) + off + k] for k in range(n)]

    def player(self):
        """gs_p as the reference's replay prints it: x, y, vy, mode, grav"""
        return (self.u32("_gs_p", OFF_X), signed(self.u32("_gs_p", OFF_Y), 32), signed(self.u16("_gs_p", OFF_VY), 16),
                self.u8("_gs_p", OFF_MODE), signed(self.u8("_gs_p", OFF_GRAV), 8))

    def tick(self, n=1):
        for _ in range(n):
            self.pb.tick()
            self.frames += 1
            if not self.m[0xFF40] & 0x80:
                self.lcd_off += 1

    def press(self, button):
        self.pb.button_press(button)
        self.tick(2)
        self.pb.button_release(button)
        self.tick(2)

    def wait_screen(self, scr, limit=600):
        for _ in range(limit):
            if self.u8("_g_screen") == scr:
                return True
            self.tick()
        return False

    def shot(self, path):
        self.pb.screen.image.convert("RGB").save(path)


class Perf:
    """scanlines per part of each frame played: mean, max, worst frames"""

    def __init__(self):
        self.rows = []

    def sample(self, g):
        f = [g.u8("_g_perf", i) for i in range(len(PERF))]
        s = [g.u8("_g_perf_sim", i) for i in range(len(PERF_SIM))]
        self.rows.append((g.u16("_gs_p", OFF_TICKS), f, s))

    def report(self, level, csv):
        rows = self.rows[1:]
        if not rows:
            return
        if csv:
            with open(csv % level if "%" in csv else csv, "w") as f:
                f.write(",".join(["tick"] + PERF + PERF_SIM) + "\n")
                for t, a, b in rows:
                    f.write(",".join(map(str, [t] + a + b)) + "\n")
        print("  level %d, scanlines per frame (%d a frame), %d frames:" % (level, LINES_PER_FRAME, len(rows)))
        for names, k in ((PERF, 1), (PERF_SIM, 2)):
            for i, name in enumerate(names):
                v = [r[k][i] for r in rows]
                print("    %-8s mean %6.1f  max %3d" % (name, sum(v) / len(v), max(v)))
        worst = sorted(rows, key=lambda r: -r[1][0])[:5]
        print("  worst frames (tick: frame, sim):", ", ".join("%d: %d, %d" % (t, f[0], f[1]) for t, f, _ in worst))
        over = sum(1 for r in rows if r[1][0] >= LINES_PER_FRAME)
        print("  frames over budget: %d" % over)


UT_COIN_NO, UT_COIN_YES, UT_BAR = 67, 68, 72  # src/gbc/gfx_ids.h


def card_coins(g):
    """The level card's coins (3, background row 8) are the saved ones."""
    g.tick(10)
    saved = g.save(SAVE_COINS, 0)
    shown = [g.pb.memory[0, 0x9800 + 8 * 32 + x] for x in (8, 10, 12)]
    want = [UT_COIN_YES if saved >> i & 1 else UT_COIN_NO for i in range(3)]
    return shown == want, "saved %s, shown %s" % (bin(saved), ["got" if t == UT_COIN_YES else "-" for t in shown])


def migration(rom):
    """A save of the first version is carried over (bests, coins, attempts),
    and the level card shows its coins."""
    best, practice = [47, 0, 0, 0, 0, 12], [82, 0, 0, 0, 0, 0]
    coins, attempts = [0b101, 0, 0, 0, 0, 0b10], [300, 0, 0, 0, 0, 65535]
    g = Game(rom, save_v1(best, practice, coins, attempts))
    if not g.wait_screen(SCR_TITLE):
        return False, "no title screen"
    got = ([g.save(SAVE_BEST, i) for i in range(6)], [g.save(SAVE_BEST_PRACTICE, i) for i in range(6)],
           [g.save(SAVE_COINS, i) for i in range(6)], [g.save(SAVE_ATTEMPTS, i, 4) for i in range(6)])
    if got != (best, practice, coins, attempts):
        return False, "carried over as %s" % (got,)
    g.tick(30)
    g.press("start")
    g.wait_screen(SCR_SELECT)
    ok, info = card_coins(g)
    g.pb.stop(save=False)
    return ok, "bests, coins and attempts kept; card " + info


def title_demo(g, loops=4):
    """The title's demo run loops its level on the beat of the menu song
    without dying, every frame on time (its presses are the other
    versions', checked by pd_tool demo)."""
    g.tick(1029 * loops + 60)  # 180 blocks at 10.5 a second: 1028.6 frames a loop
    n, deaths, late = g.u16("_g_demo_loops"), g.u16("_g_demo_deaths"), g.u16("_g_dropped")
    return n >= loops and not deaths and not late, "%d loops, %d deaths, %d frames late" % (n, deaths, late)


def garage(g):
    """From the title to the garage: the icon and colours chosen there are
    saved, and the player is drawn with them (its cube frames in VRAM, its
    sprite palette)."""
    g.press("right")
    g.tick(4)
    g.press("a")
    if not g.wait_screen(SCR_GARAGE):
        return False, "no garage"
    g.tick(10)
    for _ in range(3):  # icon 3
        g.press("right")
        g.tick(30)
    g.press("down")
    for _ in range(4):  # colour 1: 4
        g.press("right")
    g.press("down")
    g.press("left")  # colour 2: 1 -> 0
    g.press("b")
    if not g.wait_screen(SCR_TITLE):
        return False, "no title after the garage"
    got = (g.save(SAVE_ICON, 0), g.save(SAVE_COL1, 0), g.save(SAVE_COL2, 0))
    if got != (3, 4, 0):
        return False, "saved icon and colours %s, not (3, 4, 0)" % (got,)
    n = CUBE_FRAMES * 64
    vram = [g.pb.memory[1, 0x8000 + ST_CUBE * 16 + k] for k in range(n)]
    if vram != g.rom("_gfx_icons", 3 * n, n):
        return False, "the cube's frames in VRAM aren't icon 3's"
    pal = [g.u16("_g_objpal", 2 * k) for k in (1, 2)]

    def rgb555(c):
        r, gr, b = g.rom("_gbc_player_colors", 3 * c, 3)
        return r | gr << 5 | b << 10
    if pal != [rgb555(4), rgb555(0)]:
        return False, "the player's palette is %s, not colours 4 and 0" % (pal,)
    return True, "icon 3, colours 4 and 0: saved, in VRAM and the palette"


def bar_after_restart(g, level_id):
    """Die late in the first level: after the restart the progress bar (HUD
    row, cells 3..14) shows the new percentage, every cell of it (a restart
    changes more cells than a frame's cell queue holds)."""
    script = os.path.join("build", "gbc", "script%d.txt" % level_id)
    subprocess.run([TOOL, "script", str(level_id), script], check=True, stdout=subprocess.DEVNULL)
    held, _ = load_script(script)

    def percent():  # progress_percent (src/core/progress.c)
        x = signed(g.u32("_gs_p", OFF_X), 32)
        return 0 if x <= 0 else min(100, ((x >> 16) * 100 + ((x & 0xFFFF) * 100 >> 16)) // g.u16("_gs_width"))
    crash = []

    def on_input(_):
        if not crash and percent() >= 80:
            crash.append(1)
        h = held.get(g.u16("_gs_p", OFF_TICKS), False)
        g.m[g.sym["_g_test_keys"]] = J_A if h != bool(crash) else 0  # past 80%: the wrong buttons
    g.tick(10)
    g.press("a")
    if not g.wait_screen(SCR_PLAY):
        return False, "level did not start"
    while g.u8("_g_phase") != PH_RUN:  # (it still says how the last run ended)
        g.tick()
    g.pb.hook_register(0, g.sym["_input_update"], on_input, None)
    for _ in range(12000):
        g.tick()
        if g.u8("_g_phase") == PH_DEAD:
            break
    died = percent()
    while g.u8("_g_phase") != PH_RUN:
        g.tick()
    g.tick(30)
    g.pb.hook_deregister(0, g.sym["_input_update"])
    g.m[g.sym["_g_test_keys"]] = 0xFF
    fill = percent() * 123 >> 7
    want = [max(0, min(8, fill - 8 * i)) for i in range(12)]
    shown = [g.pb.memory[0, 0x9800 + 3 + i] - UT_BAR for i in range(12)]
    g.press("start")
    g.tick(5)
    g.press("b")
    g.wait_screen(SCR_SELECT)
    return shown == want, "died at %d%%; restarted at %d%%, bar %s" % (died, percent(), "as it should be" if shown == want else
                                                                     "shows %s, not %s" % (shown, want))


def deaths_on_time(g, level_id, n=6):
    """Die n times in the first level, spread through it (the solver's presses, then the wrong ones): no frame
    runs late from the run through the death's effects to the next attempt (a death's frame ran the physics too;
    what it counts for, the burst, the shake and the flash come in the frames after)."""
    script = os.path.join("build", "gbc", "script%d.txt" % level_id)
    subprocess.run([TOOL, "script", str(level_id), script], check=True, stdout=subprocess.DEVNULL)
    held, finish = load_script(script)
    die_at = [finish * k // (n + 1) for k in range(1, n + 1)]
    st = {"deaths": 0, "dead": False, "vbl": None, "late": []}

    def on_input(_):
        vbl = g.u8("_g_vbl_count")
        if st["vbl"] is not None and (vbl - st["vbl"]) & 255 > 1:
            st["late"].append(st["deaths"])
        st["vbl"] = vbl
        ph = g.u8("_g_phase")
        if ph == PH_DEAD and not st["dead"]:
            st["deaths"] += 1
        st["dead"] = ph != PH_RUN
        t = g.u16("_gs_p", OFF_TICKS)
        k = st["deaths"]
        g.m[g.sym["_g_test_keys"]] = J_A if k < n and held.get(t, False) != (t >= die_at[k]) else 0
    g.tick(10)
    g.press("a")
    if not g.wait_screen(SCR_PLAY):
        return False, "level did not start"
    while g.u8("_g_phase") != PH_RUN:
        g.tick()
    g.pb.hook_register(0, g.sym["_input_update"], on_input, None)
    for _ in range(finish * (n + 2)):
        g.tick()
        if st["deaths"] >= n and g.u8("_g_phase") == PH_RUN:
            break
    g.pb.hook_deregister(0, g.sym["_input_update"])
    g.m[g.sym["_g_test_keys"]] = 0xFF
    g.press("start")
    g.tick(5)
    g.press("b")
    g.wait_screen(SCR_SELECT)
    ok = st["deaths"] >= n and not st["late"]
    return ok, "%d deaths, %s" % (st["deaths"], "no frame late" if not st["late"] else
                                   "%d frames late (after deaths %s)" % (len(st["late"]), st["late"][:8]))


def held_start(g):
    """The A that starts a level, still held, doesn't jump; then quit it."""
    g.tick(10)
    g.pb.button_press("a")
    if not g.wait_screen(SCR_PLAY):
        return False
    g.tick(40)
    jumps = g.u16("_gs_p", OFF_JUMPS)
    g.pb.button_release("a")
    g.tick(2)
    g.press("start")
    g.tick(5)
    g.press("b")
    return g.wait_screen(SCR_SELECT) and jumps == 0


def play_level(game, level, level_id, nlevels, held, finish, ref, shots, every, perf=None, allow_late=False, delay=0,
               pause_at=None, beats=None):
    """level: its number on the Game Boy (the select screen's order), level_id: the game's; pause_at: the run's
    tick to pause on, for a moment, then resume with A; beats: a list for the run's tick at each of the music's
    beats"""
    g = game
    if not g.wait_screen(SCR_SELECT):
        return False, "no level select"
    g.tick(10)
    # the select screen starts at the level played last
    while g.selected != level:
        g.press("right")
        g.tick(20)  # the card is redrawn over a few frames
        g.selected = (g.selected + 1) % nlevels
    g.tick(delay)
    g.press("a")
    if not g.wait_screen(SCR_PLAY):
        return False, "level did not start"
    # the buttons for each tick, put in place when the game reads the pad
    # for it (a frame's work can run past the emulator's frame boundary)
    late = []
    differs = []
    paused = [0]  # frames since the pause; past PAUSE_FRAMES: resumed
    paused_shots = []  # each frame from the pause to the first after the resume, and where its sprites can be
    resumed = 0  # frames since A was read to resume
    boxes = None

    def on_input(_):
        if perf is not None and g.u8("_g_phase") == PH_RUN:
            perf.sample(g)
        t = g.u16("_gs_p", OFF_TICKS)
        if g.u16("_g_dropped") > len(late):
            late.append(t)
        if t in ref and not differs and g.player() != ref[t]:
            differs.append("after tick %d the ROM has x, y, vy, mode, gravity %s, the reference %s" %
                           (t, g.player(), ref[t]))
        # (the music ticks first in a frame, then the game reads the pad)
        if beats is not None and g.u8("_music_beat"):
            beats.append(t)
        keys = J_A if held.get(t, False) else 0
        if t == pause_at and paused[0] <= PAUSE_FRAMES:
            # Start on this tick, then A a moment later (it doesn't jump)
            keys = J_START if paused[0] == 0 else J_A if paused[0] == PAUSE_FRAMES else 0
            paused[0] += 1
        g.m[g.sym["_g_test_keys"]] = keys

    g.pb.hook_register(0, g.sym["_input_update"], on_input, None)
    for _ in range(finish * 3 + 600):
        t = g.u16("_g_snap", OFF_TICKS)
        if shots and every and t % every == 0 and g.u8("_g_phase") == PH_RUN:
            g.shot(os.path.join(shots, "level%d_t%05d.png" % (level, t)))
        g.tick()
        resumed += paused[0] > PAUSE_FRAMES
        if paused[0] and resumed <= 2:
            paused_shots.append((g.pb.screen.ndarray.copy(), boxes))
            boxes = sprite_boxes(g)
        if g.u8("_g_snap", OFF_DEAD) or g.u8("_g_snap", OFF_DONE):
            break
    g.pb.hook_deregister(0, g.sym["_input_update"])
    g.m[g.sym["_g_test_keys"]] = 0xFF
    ticks = g.u16("_g_snap", OFF_TICKS)
    if g.u8("_g_snap", OFF_DEAD):
        x = g.u32("_g_snap", OFF_X) / 65536.0
        g.tick(80)
        g.press("start")
        g.press("b")
        return False, "died at tick %d, x=%.2f" % (ticks, x)
    if not g.u8("_g_snap", OFF_DONE):
        return False, "not finished after %d ticks" % ticks
    # what the finish counted for (src/core/progress.c on the Game Boy)
    run_coins = g.u8("_g_snap", OFF_COINS)
    saved = (g.save(SAVE_BEST, level_id), g.save(SAVE_COINS, level_id), g.save(SAVE_ATTEMPTS, level_id, 4))
    save_ok = saved[0] == 100 and saved[1] & run_coins == run_coins and saved[2] >= 1
    info = "finished in %d ticks (reference: %d), %d frames took too long" % (ticks, finish, g.u16("_g_dropped"))
    if late:
        info += " (ticks %s%s)" % (" ".join(map(str, late[:12])), " ..." if len(late) > 12 else "")
    info += "; " + (differs[0] if differs else "the same as the reference on every tick")
    torn = pause_at is not None and pause_shown(paused_shots)
    if torn:
        info += "; WHILE PAUSED, " + torn
    if not save_ok:
        info += "; SAVED best %d%%, coins %s (the run's %s), attempts %d" % (saved[0], bin(saved[1]), bin(run_coins),
                                                                            saved[2])
    # the results come up after a moment, drawn once (the fireworks go on
    # round every 256 frames), and whole
    results = []
    a = g.sym["_ui_results"]
    g.pb.hook_register(a >> 16, a & 0xFFFF, lambda _: results.append(1), None)
    shown = []  # from the frame before the one they are drawn in: each frame, and where its sprites can be
    prev, boxes = None, sprite_boxes(g)
    for _ in range(100):
        g.tick()
        cur = (g.pb.screen.ndarray.copy(), boxes)
        boxes = sprite_boxes(g)
        if results and len(shown) < RESULTS_FRAMES:
            shown += [cur] if shown else [prev, cur]
        prev = cur
    if shots:
        g.shot(os.path.join(shots, "level%d_complete.png" % level))
    g.tick(240)
    g.pb.hook_deregister(a >> 16, a & 0xFFFF)
    if len(results) != 1:
        info += "; RESULTS DRAWN %d TIMES" % len(results)
    torn_results = results_shown(shown, g.m[0xFF4A]) if results else ""
    if torn_results:
        info += "; THE RESULTS " + torn_results
    g.press("a")
    g.wait_screen(SCR_SELECT)
    return (ticks == finish and not differs and save_ok and len(results) == 1 and (allow_late or not late)
            and not torn and not torn_results), info


PAUSE_FRAMES = 30
RESULTS_FRAMES = 16


def sky_top(img):
    """the main colour of screen lines 8..21, under the progress bar: the sky's first band"""
    c, n = numpy.unique(img[8:22, :, :3].reshape(-1, 3), axis=0, return_counts=True)
    return tuple(c[n.argmax()])


def sprite_boxes(g):
    """where the sprites can be in the next frame (8x16 each; the OAM after a frame is the next one's)"""
    box = numpy.zeros((144, 160), bool)
    if g.m[0xFF40] & 2:
        for k in range(40):
            y, x = g.m[0xFE00 + 4 * k] - 16, g.m[0xFE01 + 4 * k] - 8
            box[max(y, 0):max(y + 16, 0), max(x, 0):max(x + 8, 0)] = True
    return box


def pause_shown(shots):
    """From the frame Start is read in (it still shows the tick before the last) to the first after the resume
    (shots: each frame, and where its sprites can be): the run as it stopped, the whole menu from the next frame
    on (it was drawn when the level began), and the run as it stopped again, nothing between (a part-drawn menu,
    the sprites hidden part-way down), and the sky's first band in its colour in every frame. What went wrong,
    or ""."""
    if len(shots) < PAUSE_FRAMES + 2:
        return "NO RESUME"
    if same_below(*shots[-2], *shots[1], 0):
        return "NO MENU (BUT FOR THE SPRITES, THE RUN AS IT STOPPED)"
    shots = [img for img, _ in shots]
    sky, run, menu = sky_top(shots[0]), shots[1], shots[-2]
    for i, img in enumerate(shots[1:], 1):
        if sky_top(img) != sky:
            return "FRAME %d SHOWED THE SKY'S FIRST BAND IN ANOTHER COLOUR" % i
        if i in (1, len(shots) - 1):
            if not (img == run).all():
                return "FRAME %d SHOWED OTHER THAN THE RUN AS IT STOPPED" % i
        elif not (img == menu).all():
            return "FRAME %d SHOWED OTHER THAN THE WHOLE MENU" % i
    return ""


def same_below(a, a_boxes, b, b_boxes, wy, dx=0):
    """Lines wy.. of a are b's scrolled dx pixels on, but where sprites can be."""
    w = 160 - dx
    return not ((a[wy:, :w] != b[wy:, dx:]).any(axis=2) & ~(a_boxes[wy:, :w] | b_boxes[wy:, dx:])).any()


def results_shown(shown, wy):
    """From the frame before the results are drawn (shown: each frame, and where its sprites can be): the run
    while they are drawn (below line wy as in the frame they start in, but for the sprites and the camera's last
    steps), then the results whole (below line wy as in the last frame, but for the sprites), nothing between,
    and the sky's first band in its colour in every frame. What went wrong, or ""."""
    if len(shown) < RESULTS_FRAMES:
        return "NOT SHOWN"
    sky, (run, run_boxes), (last, last_boxes) = sky_top(shown[0][0]), shown[1], shown[-1]
    up = 0
    for i, (img, boxes) in enumerate(shown[1:], 1):
        if sky_top(img) != sky:
            return "SHOWED THE SKY'S FIRST BAND IN ANOTHER COLOUR (FRAME %d)" % i
        if not up and any(same_below(img, boxes, run, run_boxes, wy, dx) for dx in range(8)):
            continue
        up = up or i
        if not same_below(img, boxes, last, last_boxes, wy):
            return "SHOWED PART-DRAWN (FRAME %d)" % i
    return "" if up else "NOT SHOWN"


def pause_tick(held, finish):
    """a tick a third of the way in with no press near it"""
    t = finish // 3
    while any(held.get(k, False) for k in range(t - 8, t + 9)):
        t += 1
    return t


def pause_sync(beats, paused_beats, pause_at):
    """The music's beats fall on the same ticks of the run with a pause as without one: the run and its song
    stop and go on together."""
    after = [b for b in beats if b > pause_at]
    paused_after = [b for b in paused_beats if b > pause_at]
    if not after:
        return False, "no beat after tick %d" % pause_at
    if beats != paused_beats:
        off = [p - b for b, p in zip(after, paused_after) if p != b]
        return False, "after a pause at tick %d the beats fall %s tick(s) off the run's (%d beats after it, %d with the " \
            "pause)" % (pause_at, off[0] if off else "?", len(after), len(paused_after))
    return True, "the %d beats after a pause at tick %d on the same ticks of the run as without it" % (len(after), pause_at)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--levels", default="", help="the levels to play, by their number on the Game Boy (default: all)")
    ap.add_argument("--shots", default="")
    ap.add_argument("--every", type=int, default=0, help="screenshot every N ticks")
    ap.add_argument("--allow-late", action="store_true", help="don't fail on frames that took too long")
    ap.add_argument("--delay", type=int, default=0,
                    help="frames to wait before starting each level (shifts work done every other or fourth frame)")
    ap.add_argument("--perf", action="store_true", help="time the frames (the PERF=1 build)")
    ap.add_argument("--perf-csv", default="", help="with --perf: every frame's timings to this file (%%d: level)")
    args = ap.parse_args()
    if args.shots:
        os.makedirs(args.shots, exist_ok=True)
    game = Game(ROM_PERF if args.perf else ROM)
    if not game.wait_screen(SCR_TITLE):
        print("FAIL: no title screen")
        return 1
    fails = 0
    while not game.m[0xFF40] & 0x80:  # (it starts off, and the title is set up before it comes on)
        game.tick()
    game.lcd_off = 0
    ok, info = title_demo(game)
    print("title's demo run: %s (%s)" % ("ok" if ok else "FAIL", info))
    fails += not ok
    if args.shots:
        game.shot(os.path.join(args.shots, "title.png"))
    ok, info = garage(game)
    print("garage: %s (%s)" % ("ok" if ok else "FAIL", info))
    fails += not ok
    game.tick(30)
    game.press("start")
    game.wait_screen(SCR_SELECT)
    ids = gb_levels()
    ok, info = card_coins(game)
    print("level card coins: %s (%s)" % ("ok" if ok else "FAIL", info))
    fails += not ok
    ok, info = migration(ROM_PERF if args.perf else ROM)
    print("a first version's save: %s (%s)" % ("ok" if ok else "FAIL", info))
    fails += not ok
    ok = held_start(game)
    print("A held from the level select: %s" % ("no jump" if ok else "FAIL, the cube jumped"))
    fails += not ok
    ok, info = bar_after_restart(game, ids[0])
    print("progress bar after a restart: %s (%s)" % ("ok" if ok else "FAIL", info))
    fails += not ok
    ok, info = deaths_on_time(game, ids[0])
    print("deaths: %s (%s)" % ("ok" if ok else "FAIL", info))
    fails += not ok and not (args.allow_late or args.perf)
    first = True
    for level in map(int, args.levels.split(",")) if args.levels else range(len(ids)):
        script = os.path.join("build", "gbc", "script%d.txt" % ids[level])
        subprocess.run([TOOL, "script", str(ids[level]), script], check=True, stdout=subprocess.DEVNULL)
        held, finish = load_script(script)
        ref = load_replay(ids[level], script)
        if args.shots:
            game.tick(10)
            game.shot(os.path.join(args.shots, "select%d.png" % level))
        perf = Perf() if args.perf else None
        beats = []
        ok, info = play_level(game, level, ids[level], len(ids), held, finish, ref, args.shots, args.every, perf,
                              args.allow_late or args.perf, args.delay, beats=beats)
        print("level %d: %s %s" % (ids[level], "ok" if ok else "FAIL", info))
        if perf:
            perf.report(level, args.perf_csv)
        sys.stdout.flush()
        fails += not ok
        if first and ok:
            # the first level again, paused a third of the way in
            first = False
            paused_beats = []
            at = pause_tick(held, finish)
            ok, info = play_level(game, level, ids[level], len(ids), held, finish, ref, "", 0, None,
                                  args.allow_late or args.perf, args.delay, pause_at=at, beats=paused_beats)
            if ok:
                ok, info = pause_sync(beats, paused_beats, at)
            print("level %d with a pause: %s (%s)" % (ids[level], "ok" if ok else "FAIL", info))
            sys.stdout.flush()
            fails += not ok
    # screens change in black, the LCD on: off, a Game Boy Color's is white
    print("screen changes: %s" % ("black, the LCD on throughout" if not game.lcd_off else
                                  "FAIL, the LCD was off (white) for %d frames" % game.lcd_off))
    fails += game.lcd_off > 0
    print("all levels finished in the ROM" if not fails else "SOME LEVELS FAILED IN THE ROM")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
