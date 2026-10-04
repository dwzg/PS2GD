#!/usr/bin/env python3
"""
Run the Game Boy Color ROM in PyBoy, headless, and play levels with the
solver's inputs (gbc_tool script): proves the ROM finishes each level in
exactly as many ticks as the host build of the same physics says, and
reports frames the ROM could not finish in time.

  scripts/gbc-emu-test.py [--levels 0,1,..] [--shots DIR] [--every N] [--perf]

needs: pip install pyboy (2.x), and build/gbc/pulsedash.gbc (make -f
Makefile.gbc), build/host/gbc_tool. --perf plays the PERF=1 build
(build/gbc-perf) and prints how many scanlines each part of a frame took. Exits non-zero if a level isn't
finished, or finishes on another tick than the host's run.
"""
import argparse
import os
import subprocess
import sys

from pyboy import PyBoy

ROM = "build/gbc/pulsedash.gbc"
ROM_PERF = "build/gbc-perf/pulsedash.gbc"
TOOL = "build/host/gbc_tool"
SCR_TITLE, SCR_SELECT, SCR_PLAY = 1, 2, 3
J_A = 0x10
PH_RUN, PH_DEAD, PH_RESPAWN, PH_COMPLETE = 0, 1, 2, 3
# the timings of a PERF=1 build (src/gbc/gbc.h, src/gbc/gbsim.c), in scanlines
PERF = ["frame", "sim", "music", "stream", "hud", "pal", "sprites"]
PERF_SIM = ["physics", "x", "solids", "inner", "objects", "broad"]
LINES_PER_FRAME = 154

# GsPlayer (src/gbc/gbsim.h) as SDCC lays it out: no padding
OFF_X, OFF_Y, OFF_MODE, OFF_DEAD, OFF_DONE, OFF_COINS, OFF_TICKS = 0, 6, 17, 21, 22, 23, 26


def symbols(noi):
    sym = {}
    for line in open(noi):
        p = line.split()
        if len(p) == 3 and p[0] == "DEF":
            sym[p[1]] = int(p[2], 16)
    return sym


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


class Game:
    def __init__(self, rom):
        self.pb = PyBoy(rom, window="null", cgb=True, sound_emulated=False)
        self.pb.set_emulation_speed(0)
        self.sym = symbols(os.path.splitext(rom)[0] + ".noi")
        self.m = self.pb.memory
        self.frames = 0
        self.selected = 0

    def u8(self, name, off=0):
        return self.m[self.sym[name] + off]

    def u16(self, name, off=0):
        a = self.sym[name] + off
        return self.m[a] | self.m[a + 1] << 8

    def u32(self, name, off=0):
        return self.u16(name, off) | self.u16(name, off + 2) << 16

    def tick(self, n=1):
        for _ in range(n):
            self.pb.tick()
            self.frames += 1

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


def play_level(game, level, held, finish, shots, every, perf=None):
    g = game
    if not g.wait_screen(SCR_SELECT):
        return False, "no level select"
    g.tick(10)
    # the select screen starts at the level played last
    while g.selected != level:
        g.press("right")
        g.tick(20)  # the card is redrawn over a few frames
        g.selected = (g.selected + 1) % 6
    g.press("a")
    if not g.wait_screen(SCR_PLAY):
        return False, "level did not start"
    # the buttons for each tick, put in place when the game reads the pad
    # for it (a frame's work can run past the emulator's frame boundary)
    late = []

    def on_input(_):
        if perf is not None and g.u8("_g_phase") == PH_RUN:
            perf.sample(g)
        t = g.u16("_gs_p", OFF_TICKS)
        if g.u16("_g_dropped") > len(late):
            late.append(t)
        g.m[g.sym["_g_test_keys"]] = J_A if held.get(t, False) else 0

    g.pb.hook_register(0, g.sym["_input_update"], on_input, None)
    for _ in range(finish * 3 + 600):
        t = g.u16("_g_snap", OFF_TICKS)
        if shots and every and t % every == 0 and g.u8("_g_phase") == PH_RUN:
            g.shot(os.path.join(shots, "level%d_t%05d.png" % (level, t)))
        g.tick()
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
    info = "finished in %d ticks (host: %d), %d frames took too long" % (ticks, finish, g.u16("_g_dropped"))
    if late:
        info += " (ticks %s%s)" % (" ".join(map(str, late[:12])), " ..." if len(late) > 12 else "")
    # the results come up after a moment
    g.tick(100)
    if shots:
        g.shot(os.path.join(shots, "level%d_complete.png" % level))
    g.press("a")
    g.wait_screen(SCR_SELECT)
    return ticks == finish, info


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--levels", default="0,1,2,3,4,5")
    ap.add_argument("--shots", default="")
    ap.add_argument("--every", type=int, default=0, help="screenshot every N ticks")
    ap.add_argument("--perf", action="store_true", help="time the frames (the PERF=1 build)")
    ap.add_argument("--perf-csv", default="", help="with --perf: every frame's timings to this file (%%d: level)")
    args = ap.parse_args()
    if args.shots:
        os.makedirs(args.shots, exist_ok=True)
    game = Game(ROM_PERF if args.perf else ROM)
    if not game.wait_screen(SCR_TITLE):
        print("FAIL: no title screen")
        return 1
    game.tick(30)
    if args.shots:
        game.shot(os.path.join(args.shots, "title.png"))
    game.press("start")
    game.wait_screen(SCR_SELECT)
    fails = 0
    for level in map(int, args.levels.split(",")):
        script = os.path.join("build", "gbc", "script%d.txt" % level)
        subprocess.run([TOOL, "script", str(level), script], check=True, stdout=subprocess.DEVNULL)
        held, finish = load_script(script)
        if args.shots:
            game.tick(10)
            game.shot(os.path.join(args.shots, "select%d.png" % level))
        perf = Perf() if args.perf else None
        ok, info = play_level(game, level, held, finish, args.shots, args.every, perf)
        print("level %d: %s %s" % (level, "ok" if ok else "FAIL", info))
        if perf:
            perf.report(level, args.perf_csv)
        sys.stdout.flush()
        fails += not ok
    print("all levels finished in the ROM" if not fails else "SOME LEVELS FAILED IN THE ROM")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
