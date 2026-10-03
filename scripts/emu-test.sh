#!/bin/sh
# Run the PS2 build inside the Play! emulator core, headless, with scripted
# controller input. Needs: git, cmake, ninja, a C++17 compiler, Mesa EGL +
# GL/GLU headers (only for screenshots).
#
#   scripts/emu-test.sh [seconds] [script]
#   PD_SHOTS=150,400 scripts/emu-test.sh 10    # also save shot_<frame>.ppm
#
# The default script goes title -> level select -> level 1.
#
# To play a level with on-beat input, use a perf build (it prints PD_MARK when
# an attempt starts) and append the rhythm check's presses:
#   scripts/build-ps2.sh PERF=1
#   ELF=$PWD/build/ps2-perf/PULSEDASH.ELF scripts/emu-test.sh 85 \
#       "200:CROSS:6,330:CROSS:6,$(build/host/pd_tool script 0)"
# (cube, ball and UFO parts replay reliably; ship and wave need frame-exact
# holds, so an open-loop replay may drift there)
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
WORK=${PLAY_DIR:-$ROOT/build/play}
PLAY_COMMIT=83700b2c31e593bc94e845b4b31b797be84dda59
ELF=${ELF:-$ROOT/build/ps2/PULSEDASH.ELF}

if [ ! -f "$WORK/src/CMakeLists.txt" ]; then
    mkdir -p "$WORK/src"
    cd "$WORK/src"
    git init -q
    git remote add origin https://github.com/jpd002/Play-.git
    git fetch -q --depth 1 origin "$PLAY_COMMIT"
    git checkout -q FETCH_HEAD
    git submodule update -q --init --recursive --depth 1
fi
cp "$ROOT/tools/play-harness/Main.cpp" "$WORK/src/tools/AutoTest/Main.cpp"
cp "$ROOT/tools/play-harness/CMakeLists.txt" "$WORK/src/tools/AutoTest/CMakeLists.txt"
cmake -S "$WORK/src/tools/AutoTest" -B "$WORK/build" -G Ninja -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$WORK/build" --target autotest

mkdir -p "$WORK/run"
cd "$WORK/run"
exec "$WORK/build/autotest" "$ELF" "${1:-20}" "${2:-200:CROSS:6,330:CROSS:6}"
