#!/bin/sh
# Build the Game Boy Color ROM, build/gbc/pulsedash.gbc.
# Usage: scripts/build-gbc.sh [make variables or targets...], e.g. PERF=1
#
# Uses GBDK-2020 from $GBDK_HOME when it is set. Otherwise builds GBDK-2020
# 4.1.1 from source into build/gbdk, once, against the system's SDCC 4.2
# (Debian/Ubuntu: apt-get install sdcc; GBDK's own release archives carry a
# patched SDCC, this needs none). The data converter is a host tool built
# by Makefile.host, so a C compiler and SDL2 (libsdl2-dev) are needed too.
set -e
cd "$(dirname "$0")/.."
GBDK_VERSION=4.1.1

if [ -z "$GBDK_HOME" ]; then
    GBDK_HOME="$PWD/build/gbdk"
    if [ ! -x "$GBDK_HOME/bin/lcc" ]; then
        SDCC=$(command -v sdcc) || {
            echo "build-gbc.sh: needs SDCC 4.2 (apt-get install sdcc), or GBDK_HOME set to GBDK-2020" >&2
            exit 1
        }
        SDCC_BIN=$(dirname "$SDCC")
        SRC=build/gbdk-src
        mkdir -p build
        if [ ! -f "$SRC/Makefile" ]; then
            rm -rf "$SRC"
            echo "fetching GBDK-2020 $GBDK_VERSION..."
            GIT_LFS_SKIP_SMUDGE=1 git clone -q --depth 1 --branch "$GBDK_VERSION" \
                https://github.com/gbdk-2020/gbdk-2020 "$SRC"
        fi
        echo "building GBDK-2020 $GBDK_VERSION (log: build/gbdk-build.log)..."
        # Only the Game Boy port, with the system's SDCC. Its png2asset
        # misses an include that newer C++ libraries no longer pull in.
        GBDK_MAKE="make -C $SRC SDCCDIR=$(dirname "$SDCC_BIN") PORTS=sm83 PLATFORMS=gb"
        if ! { $GBDK_MAKE CXXFLAGS="-Os -include cstdint" gbdk-build &&
               $GBDK_MAKE gbdk-support-install gbdk-lib-install-prepare gbdk-lib-install-ports \
                   gbdk-lib-install-platforms; } > build/gbdk-build.log 2>&1; then
            tail -30 build/gbdk-build.log
            exit 1
        fi
        rm -rf "$GBDK_HOME"
        cp -r "$SRC/build/gbdk" "$GBDK_HOME"
        cp -r "$SRC/gbdk-lib/include" "$GBDK_HOME/"
        # lcc looks for the compiler, assembler and linker next to itself
        for b in sdcc sdcpp sdasgb sdldgb sdar sdranlib sdnm sdobjcopy; do
            ln -sf "$SDCC_BIN/$b" "$GBDK_HOME/bin/$b"
        done
    fi
fi
exec make -f Makefile.gbc GBDK_HOME="$GBDK_HOME" "$@"
