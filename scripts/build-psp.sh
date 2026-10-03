#!/bin/sh
# Build the PSP EBOOT inside the official pspdev Docker image.
# Usage: scripts/build-psp.sh [make variables or targets...], e.g. PERF=1
set -e
cd "$(dirname "$0")/.."
IMAGE="${PSPDEV_IMAGE:-pspdev/pspdev:latest}"

# Pass through an HTTPS proxy / CA bundle if the host uses one (needed for apk).
EXTRA=""
if [ -n "$HTTPS_PROXY" ]; then
    EXTRA="--network host -e HTTPS_PROXY=$HTTPS_PROXY -e https_proxy=$HTTPS_PROXY"
    if [ -n "$SSL_CERT_FILE" ] && [ -f "$SSL_CERT_FILE" ]; then
        EXTRA="$EXTRA -v $SSL_CERT_FILE:/etc/ssl/certs/ca-certificates.crt:ro"
    fi
fi

# The version comes from git on the host: inside the container the checkout
# belongs to another user and git refuses to read it.
VERSION="${VERSION:-$(git describe --tags --always --dirty 2>/dev/null || echo dev)}"

exec docker run --rm $EXTRA -v "$PWD":/src -w /src -e OWNER="$(id -u):$(id -g)" -e VERSION="$VERSION" "$IMAGE" \
    sh -c 'command -v make >/dev/null 2>&1 || { echo "installing make..."; apk add --no-cache make >/dev/null; }
           make -f Makefile.psp -j"$(nproc)" VERSION="$VERSION" "$@"; status=$?
           [ -d build ] && chown -R "$OWNER" build
           exit $status' sh "$@"
