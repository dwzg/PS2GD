#!/usr/bin/env python3
"""Shift a level's obstacles onto the beat of its song.

The rhythm check (`pd_tool rhythm`) asks whether a level can be beaten when
every press in the tap modes (cube, ball, UFO) lands on an 8th note of the
song, a couple of ticks early or late. This tool makes a level pass it
without redesigning it. Wherever the check gets stuck it tries, in order:

  1. inserting or deleting a *plain* column in the run-up (a copy of its left
     neighbour holding only blocks or air), which moves everything after it
     by one block;
  2. moving an orb or pad one or two blocks;
  3. lengthening or shortening a spike pit by one column;

and keeps the first edit that gets the check well past the sticking point.

    tools/beat_align.py src/levels/skyward_pulse.c            # dry run
    tools/beat_align.py src/levels/skyward_pulse.c --write    # update the file

Afterwards the level must still pass the normal checks (the tool runs them
before writing): `pd_tool solve <file> 3` and `pd_tool coins <file>`.
"""
import argparse
import multiprocessing
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PD_TOOL = os.path.join(ROOT, "build", "host", "pd_tool")
LITERAL = re.compile(r'^"(.*)",\s*$')
PLAIN = set(" .#_=")
PIT = PLAIN | set("^v,`")
MOVABLE = set("ypbgYPB")


class Level:
    """The string literals of a level .c file, with sections as column lists."""

    def __init__(self, path):
        self.path = path
        self.lines = open(path).read().split("\n")
        self.headers = []   # header literals ("#name ...")
        self.sections = []  # [[line index, ...], [row text, ...]]
        cur = None
        for i, ln in enumerate(self.lines):
            m = LITERAL.match(ln)
            text = m.group(1) if m else None
            if text is not None and text[:1] in ("|", "!"):
                if cur is None:
                    cur = [[], []]
                    self.sections.append(cur)
                cur[0].append(i)
                cur[1].append(text)
                continue
            cur = None
            if text is not None and text.startswith("#"):
                self.headers.append(text)

    def copy(self):
        c = Level.__new__(Level)
        c.path, c.lines, c.headers = self.path, self.lines, self.headers
        c.sections = [[list(s[0]), list(s[1])] for s in self.sections]
        return c

    def width(self):
        return sum(len(s[1][0]) - 1 for s in self.sections)

    def locate(self, x):
        """Global column -> (section, column inside the section, counting the lead char)."""
        for si, s in enumerate(self.sections):
            w = len(s[1][0]) - 1
            if x < w:
                return si, x + 1
            x -= w
        return None, None

    def plain(self, x, allowed=PLAIN):
        """Is column x a copy of the column before it, with nothing but `allowed`?"""
        si, c = self.locate(x)
        if si is None or c < 2:
            return False
        for row in self.sections[si][1]:
            if row[c] != row[c - 1]:
                return False
            if row[0] == "!" and row[c] != " ":
                return False
            if row[c] not in allowed:
                return False
        return True

    def pit(self, x):
        """A plain column that holds spikes (part of a spike pit)."""
        si, c = self.locate(x)
        return self.plain(x, PIT) and any(r[c] in "^v,`" for r in self.sections[si][1])

    def movables(self, x):
        """Rows of orbs/pads in column x."""
        si, c = self.locate(x)
        if si is None:
            return []
        return [i for i, r in enumerate(self.sections[si][1]) if r[c] in MOVABLE]

    def move(self, x, row, d):
        """Move the object at (x, row) by d columns within its section; False if blocked."""
        si, c = self.locate(x)
        rows = self.sections[si][1]
        r = rows[row]
        if not (1 <= c + d < len(r)) or r[c + d] != " ":
            return False
        r = list(r)
        r[c + d], r[c] = r[c], " "
        rows[row] = "".join(r)
        return True

    def insert(self, x):
        si, c = self.locate(x)
        rows = self.sections[si][1]
        self.sections[si][1] = [r[:c] + r[c] + r[c:] for r in rows]

    def delete(self, x):
        si, c = self.locate(x)
        rows = self.sections[si][1]
        self.sections[si][1] = [r[:c] + r[c + 1:] for r in rows]

    def text(self):
        out = list(self.headers)
        for s in self.sections:
            out.extend(s[1])
            out.append("")
        return "\n".join(out) + "\n"

    def save(self, path=None):
        lines = list(self.lines)
        for idx, rows in self.sections:
            for i, r in zip(idx, rows):
                lines[i] = '"%s",' % r
        open(path or self.path, "w").write("\n".join(lines))


def pd(args, level):
    with tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False) as f:
        f.write(level.text())
        name = f.name
    try:
        r = subprocess.run([PD_TOOL] + args[:1] + [name] + args[1:], capture_output=True, text=True)
        return r.returncode, r.stdout
    finally:
        os.unlink(name)


def rhythm_fail(level, tol, until=None):
    """x where the rhythm check gets stuck (worst offset), or None if it passes
    (up to `until`, if given)."""
    _, out = pd(["rhythm", str(tol)] + ([str(until)] if until else []), level)
    if "presses on 8th notes" not in out:
        raise RuntimeError("pd_tool rhythm failed: %r" % out[-300:])
    xs = [float(v) for v in re.findall(r"FAILED at x=([0-9.]+)", out)]
    return min(xs) if xs else None


def candidates(level, fail, reach, grew):
    """Edits to try at a sticking point, cheapest and most local first."""
    fx = int(fail)
    near = list(range(fx + 1, max(fx - reach, 0), -1))
    ops = ("delete", "insert") if grew > 0 else ("insert", "delete")
    out = []
    for n in (1, 2):
        for x in near:
            if level.plain(x):
                for op in ops:
                    out.append(("%s %d plain column%s at x=%d" % (op, n, "s" * (n > 1), x), [(op, x, "plain")] * n))
    for x in near:
        for row in level.movables(x):
            for d in (-1, 1, -2, 2):
                out.append(("move orb/pad at x=%d by %+d" % (x, d), [("move", x, row, d)]))
    for x in near:
        if level.pit(x):
            for op in ("delete", "insert"):  # shorter pits first: never harder if avoidable
                out.append(("%s spike column at x=%d" % (op, x), [(op, x, "pit")]))
    return out


def apply(level, edits):
    """The level with the edits applied, or None if one is not possible."""
    trial = level.copy()
    for e in edits:
        if e[0] == "move":
            if not trial.move(e[1], e[2], e[3]):
                return None
            continue
        op, x, kind = e
        if not (trial.plain(x) if kind == "plain" else trial.pit(x)):
            return None
        getattr(trial, op)(x)
    return trial


def _evaluate(job):
    trial, tol, until = job
    return rhythm_fail(trial, tol, until)


def align(level, tol, reach=14, ahead=30, verbose=True):
    fail = rhythm_fail(level, tol)
    w0 = level.width()
    edits = 0
    pool = multiprocessing.Pool()
    while fail is not None:
        cands = []
        for name, e in candidates(level, fail, reach, level.width() - w0):
            trial = apply(level, e)
            if trial is not None:
                cands.append((name, trial))
        chosen = None
        progress = None  # (x, name, trial): the edit that gets furthest otherwise
        # evaluate in batches, in order of preference; take the first that
        # gets well past the sticking point
        batch = multiprocessing.cpu_count() * 2
        for i in range(0, len(cands), batch):
            part = cands[i:i + batch]
            res = pool.map(_evaluate, [(t, tol, fail + ahead) for _, t in part])
            for (name, trial), f2 in zip(part, res):
                if f2 is None:
                    chosen = (name, trial)
                    break
                if f2 > fail + 0.5 and (progress is None or f2 > progress[0]):
                    progress = (f2, name, trial)
            if chosen:
                break
        if not chosen and progress:
            chosen = progress[1:]  # a partial step; the next round continues from it
        if not chosen:
            pool.close()
            print("  stuck at x=%.1f: no edit in reach gets past it" % fail)
            return level, False, edits
        level = chosen[1]
        edits += 1
        fail = rhythm_fail(level, tol)
        if verbose:
            print("  %s -> %s" % (chosen[0], "passes" if fail is None else "now stuck at x=%.1f" % fail), flush=True)
    pool.close()
    return level, True, edits


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("level")
    ap.add_argument("--tol", type=int, default=2, help="ticks early/late a press may be (default 2)")
    ap.add_argument("--write", action="store_true", help="update the level file")
    ap.add_argument("--out", help="write the result here, even if the check still fails (to finish by hand)")
    a = ap.parse_args()
    if not os.path.exists(PD_TOOL):
        sys.exit("build pd_tool first: make -f Makefile.host")
    lvl = Level(a.level)
    w0 = lvl.width()
    lvl, ok, edits = align(lvl, a.tol)
    print("%s: %d edits, width %d -> %d, rhythm check %s" % (a.level, edits, w0, lvl.width(),
                                                            "passes" if ok else "still FAILS"))
    if a.out:
        lvl.save(a.out)
        print("wrote", a.out)
    if not ok:
        return 1
    rc, out = pd(["solve", "3"], lvl)
    print("".join(l + "\n" for l in out.splitlines() if "K=" in l or "WARNING" in l), end="")
    rc2, out2 = pd(["coins"], lvl)
    print(out2, end="")
    if rc or rc2:
        print("normal checks FAIL; not writing")
        return 1
    if a.write:
        lvl.save()
        print("wrote", a.level)
    return 0


if __name__ == "__main__":
    sys.exit(main())
