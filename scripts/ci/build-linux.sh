#!/bin/bash
# CI entry point: builds the Linux AppImage.
#
# Meant to run inside docker.io/streetpea/chiaki-ng-builder:qt6.9.3 (the image
# upstream chiaki-ng builds its AppImage in). To run the same thing locally:
#
#   docker run --rm -v "$PWD:/work" -w /work \
#       docker.io/streetpea/chiaki-ng-builder:qt6.9.3 scripts/ci/build-linux.sh
#
# Result: dist/PSRemotePlayTogether-<version>-linux-<arch>.AppImage
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../.."

VERSION="$(scripts/ci/version.sh)"

# The dependency builds install into /usr and the builder image runs as an
# unprivileged user with passwordless sudo, so carry on as root. HOME is kept
# so build-appimage.sh still finds the image's Python environment.
if [ "$(id -u)" != 0 ]; then
	exec sudo -E PSRPT_VERSION="$VERSION" "$0" "$@"
fi

ARCH="$(uname -m)"
echo "== Building PSRemotePlayTogether $VERSION for linux-$ARCH"

# No FUSE in a CI container: let linuxdeploy and its plugins unpack themselves instead
export APPIMAGE_EXTRACT_AND_RUN=1
# build-appimage.sh expects this file name whatever the desktop file calls the app
export LDAI_OUTPUT="chiaki-ng-${ARCH}.AppImage"

# Bundles Qt WebEngine's QML module (used for the PSN login page)
cp scripts/qtwebengine_import.qml gui/src/qml/

# linuxdeploy is unreliable when the AppDir sits on a mounted volume, so keep it in the container
APPDIR_ROOT="${PSRPT_APPDIR:-/tmp/psrpt-appdir}"
rm -rf "$APPDIR_ROOT"
scripts/build-appimage.sh "$APPDIR_ROOT"

echo "== Co-play tests"
build_appimage/coplay/coplay-tests

mkdir -p dist
OUT="dist/PSRemotePlayTogether-${VERSION}-linux-${ARCH}.AppImage"
mv appimage/chiaki-ng.AppImage "$OUT"
chmod +x "$OUT"

echo "== Checking that the AppImage starts"
# "list" prints the registered consoles and exits; it loads every bundled library on the way
SMOKE_HOME="$(mktemp -d)"
HOME="$SMOKE_HOME" XDG_CONFIG_HOME="$SMOKE_HOME/.config" QT_QPA_PLATFORM=offscreen \
	"$OUT" --appimage-extract-and-run list
rm -rf "$SMOKE_HOME"

( cd dist && sha256sum "$(basename "$OUT")" > "$(basename "$OUT").sha256" )

# The checkout belongs to the user the job runs as
if [ -n "${SUDO_UID:-}" ]; then
	chown -R "$SUDO_UID:${SUDO_GID:-$SUDO_UID}" dist
fi

ls -l dist
