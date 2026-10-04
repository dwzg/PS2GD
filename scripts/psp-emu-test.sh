#!/bin/sh
# Run the PSP build in PPSSPP's headless runner (software renderer), with
# scripted button presses. Needs: git, cmake, ninja, a C++17 compiler and the
# OpenGL/GLU headers PPSSPP's build looks for (libglu1-mesa-dev on Ubuntu).
#
#   scripts/psp-emu-test.sh [seconds] [script]
#   PD_SHOTS=150,400 scripts/psp-emu-test.sh 10    # also save shot_<frame>.ppm
#
# Frames are the PSP's (59.94 Hz), counted from boot; the default script goes
# title -> level select -> level 1. The game runs from build/ppsspp/run/,
# where its SAVE.DAT ends up (delete it for a fresh start).
#
# To play a level with on-beat input, use a perf build (it prints PD_MARK when
# an attempt starts) and append the rhythm check's presses:
#   make -f Makefile.psp PERF=1
#   make -f Makefile.host PSP=1
#   EBOOT=$PWD/build/psp-perf/EBOOT.PBP scripts/psp-emu-test.sh 85 \
#       "200:CROSS:6,330:CROSS:6,$(build/host-psp/pd_tool script 0)"
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
WORK=${PPSSPP_DIR:-$ROOT/build/ppsspp}
PPSSPP_COMMIT=a2f4ce214f224c6c26b3319409ec50205ff8253d
EBOOT=${EBOOT:-$ROOT/build/psp/EBOOT.PBP}

if [ ! -f "$WORK/src/CMakeLists.txt" ]; then
    mkdir -p "$WORK/src"
    cd "$WORK/src"
    git init -q
    git remote add origin https://github.com/hrydgard/ppsspp.git
    git fetch -q --depth 1 origin "$PPSSPP_COMMIT"
    git checkout -q FETCH_HEAD
    git submodule update -q --init --recursive --depth 1
fi

# Hook tools/ppsspp-harness/PdHarness.cpp into the headless runner: it is
# called after each frame and with everything the game prints.
cd "$WORK/src"
git checkout -q -- headless/Headless.cpp CMakeLists.txt
cp "$ROOT/tools/ppsspp-harness/PdHarness.cpp" headless/PdHarness.cpp
sed -i 's|^\t\theadless/Headless.cpp$|&\n\t\theadless/PdHarness.cpp|' CMakeLists.txt
sed -i -e 's|^static Path g_comparisonScreenshot;$|void PdHarnessFrame();\nvoid PdHarnessOutput(std::string_view text);\n&|' \
    -e 's|^void SendDebugOutput(DebugOutputChannel channel, std::string_view output) {$|&\n\tPdHarnessOutput(output);|' \
    -e 's|^\t\tPSP_RunLoopFor(blockTicks);$|&\n\t\tPdHarnessFrame();|' headless/Headless.cpp
if [ "$(grep -c PdHarness headless/Headless.cpp)" != 4 ] || ! grep -q PdHarness CMakeLists.txt; then
    echo "psp-emu-test: could not hook into this PPSSPP version" >&2
    exit 1
fi
cmake -Wno-dev -S . -B "$WORK/build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DHEADLESS=ON -DHEADLESS_CROSS=ON \
    -DUSE_DISCORD=OFF -DUSE_MINIUPNPC=OFF >/dev/null
cmake --build "$WORK/build" --target PPSSPPHeadless

mkdir -p "$WORK/run"
cd "$WORK/run"
cp "$EBOOT" EBOOT.PBP
SECS=${1:-20}
export PD_FRAMES=$((SECS * 5994 / 100))
export PD_SCRIPT="${2:-200:CROSS:6,330:CROSS:6}"
# (headless needs a path with a directory in it)
exec "$WORK/build/PPSSPPHeadless" --graphics=software --timeout-wall=$((SECS * 10 + 120)) "$WORK/run/EBOOT.PBP"
