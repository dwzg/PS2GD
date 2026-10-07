#!/bin/sh
# Build the Nintendo DS ROM, build/nds/pulsedash.nds.
# Usage: scripts/build-nds.sh [make variables or targets...], e.g. PERF=1
#
# Two steps: on this machine, nds_tool (the PC build's synthesizer: needs a
# C compiler, as for the PC build) records the songs and sound effects
# into build/nds/nitrofs, the ROM's file system, and their table into
# build/nds/gen; then the ROM is built inside the BlocksDS Docker image
# (needs Docker), a pinned version, or with a local BlocksDS when
# BLOCKSDS is set (as its Docker image sets it): make -f Makefile.nds.
set -e
cd "$(dirname "$0")/.."
IMAGE="${BLOCKSDS_IMAGE:-skylyrac/blocksds:slim-v1.24.0}"

make -f Makefile.host -j4 build/host/nds_tool
# (recorded again only when the songs or the synth changed: 10 seconds)
if [ ! -f build/nds/nitrofs/music_hp.bin ] || [ -n "$(find src/core/audio.c src/core/songs.c src/core/songdata.h \
        src/core/audio.h src/host/nds_audio.c -newer build/nds/nitrofs/music_hp.bin)" ]; then
    mkdir -p build/nds/nitrofs build/nds/gen
    build/host/nds_tool audio build/nds/nitrofs build/nds/gen
fi

# The version comes from git on the host: inside the container the checkout
# belongs to another user and git refuses to read it.
VERSION="${VERSION:-$(git describe --tags --always --dirty 2>/dev/null || echo dev)}"

if [ -n "$BLOCKSDS" ]; then
    exec make -f Makefile.nds -j"$(nproc)" VERSION="$VERSION" "$@"
fi
exec docker run --rm -v "$PWD":/src -w /src -e OWNER="$(id -u):$(id -g)" -e VERSION="$VERSION" "$IMAGE" \
    sh -c 'make -f Makefile.nds -j"$(nproc)" BLOCKSDS="$BLOCKSDS" VERSION="$VERSION" "$@"; status=$?
           chown -R "$OWNER" build/nds
           exit $status' sh "$@"
