#!/bin/sh
# Play the Nintendo DS ROM in melonDS, headless: build/host/nds_test play
# (src/host/nds_test.c) with melonDS's libretro core, built the first time
# from a pinned commit into build/melonds (needs git, make and a C++
# compiler; a few minutes).
# Usage: scripts/nds-emu-test.sh [levels [shots-dir]]   e.g. 0,3 /tmp/shots
# Needs build/nds/pulsedash.nds and its ELF (scripts/build-nds.sh).
set -e
cd "$(dirname "$0")/.."
MELONDS_REPO=https://github.com/libretro/melonDS.git
MELONDS_COMMIT=66b5d2634cd0a79030562811e6e05f5532f800ba
CORE=build/melonds/melonds_libretro.so

if [ ! -f "$CORE" ]; then
    rm -rf build/melonds
    git init -q build/melonds
    git -C build/melonds fetch -q --depth 1 "$MELONDS_REPO" "$MELONDS_COMMIT"
    git -C build/melonds checkout -q FETCH_HEAD
    make -C build/melonds -j"$(nproc)" HAVE_OPENGL=0 >/dev/null
fi
make -f Makefile.host -j"$(nproc)" build/host/nds_test >/dev/null
# (melonDS keeps its save and firmware files here)
export TMPDIR="${TMPDIR:-/tmp}"
exec build/host/nds_test play "$CORE" build/nds/pulsedash.nds "$@"
