#!/bin/sh
# Build the Game Boy Advance ROM, build/gba/pulsedash.gba.
# Usage: scripts/build-gba.sh [make variables or targets...], e.g. -j8
#
# Needs arm-none-eabi-gcc with newlib (Debian/Ubuntu: apt-get install
# gcc-arm-none-eabi libnewlib-arm-none-eabi), and a C compiler and SDL2
# (libsdl2-dev) for the host tool that makes the ROM's graphics and sound.
# The first time it fetches gbafix from devkitPro's gba-tools (a pinned
# version, checked against its hash) and builds it into build/gbafix: it
# writes the cartridge header's logo and checksum, without which a real
# Game Boy Advance won't start the game.
set -e
cd "$(dirname "$0")/.."
GBAFIX_VERSION=v1.2.0
GBAFIX_SHA256=cba3496659a7658aa7e8182c112c2846397f67d298631381575047c4780ba92e

if [ ! -x build/gbafix/gbafix ]; then
    command -v arm-none-eabi-gcc >/dev/null 2>&1 || {
        echo "build-gba.sh: needs arm-none-eabi-gcc (apt-get install gcc-arm-none-eabi libnewlib-arm-none-eabi)" >&2
        exit 1
    }
    mkdir -p build/gbafix
    echo "fetching gbafix $GBAFIX_VERSION..."
    curl -fsSL "https://raw.githubusercontent.com/devkitPro/gba-tools/$GBAFIX_VERSION/src/gbafix.c" \
        -o build/gbafix/gbafix.c
    echo "$GBAFIX_SHA256  build/gbafix/gbafix.c" | sha256sum -c - >/dev/null || {
        echo "build-gba.sh: gbafix.c does not match the expected hash" >&2
        rm -f build/gbafix/gbafix.c
        exit 1
    }
    ${CC:-cc} -O2 -w -o build/gbafix/gbafix build/gbafix/gbafix.c
fi
exec make -f Makefile.gba "$@"
