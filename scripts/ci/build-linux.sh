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

# Going live without a hardware encoder: Cisco's OpenH264, where the image's distribution
# has it (scripts/build-ffmpeg.sh uses it when pkg-config finds it)
if command -v apt-get >/dev/null; then
	DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends libopenh264-dev >/dev/null 2>&1 \
		|| { apt-get update -qq >/dev/null 2>&1 && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends libopenh264-dev >/dev/null 2>&1; } \
		|| echo "note: libopenh264-dev is not available, going live will need a hardware encoder"
fi

# Sharing a game on this computer: PipeWire's headers (the library itself is the system's,
# opened when needed, so the AppImage does not bundle it)
if command -v apt-get >/dev/null; then
	DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends libpipewire-0.3-dev >/dev/null 2>&1 \
		|| { apt-get update -qq >/dev/null 2>&1 && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends libpipewire-0.3-dev >/dev/null 2>&1; } \
		|| echo "note: libpipewire-0.3-dev is not available, sharing a game on this computer will be off"
fi

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
OUT_PATH="$PWD/$OUT"
# No sound device and no PipeWire service in a CI container: SDL's PipeWire output would try to
# connect to libraries that are there (the headers' package brings them) without a service
mkdir -m 700 -p "$SMOKE_HOME/run"
export XDG_RUNTIME_DIR="$SMOKE_HOME/run" SDL_AUDIODRIVER=dummy SDL_AUDIO_DRIVER=dummy
set +e
HOME="$SMOKE_HOME" XDG_CONFIG_HOME="$SMOKE_HOME/.config" QT_QPA_PLATFORM=offscreen \
	"$OUT_PATH" --appimage-extract-and-run list
status=$?
set -e
if [ "$status" -ne 0 ]; then
	# Say why: a library that is missing, or what the program itself says
	echo "The AppImage exited with $status" >&2
	(
		cd "$SMOKE_HOME"
		"$OUT_PATH" --appimage-extract >/dev/null 2>&1 || echo "it cannot even be unpacked" >&2
		if [ -d squashfs-root ]; then
			ls -l squashfs-root/AppRun >&2 || true
			ldd squashfs-root/usr/bin/PSRemotePlayTogether 2>&1 | grep -iE 'not found|not a dynamic' >&2 || echo "no library missing" >&2
			HOME="$SMOKE_HOME" XDG_CONFIG_HOME="$SMOKE_HOME/.config" QT_QPA_PLATFORM=offscreen QT_DEBUG_PLUGINS=1 \
				squashfs-root/AppRun list
			echo "AppRun exited with $?" >&2
			# Where it crashes
			if command -v apt-get >/dev/null && ! command -v gdb >/dev/null; then
				DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends gdb >/dev/null 2>&1 || true
			fi
			if command -v gdb >/dev/null; then
				HOME="$SMOKE_HOME" XDG_CONFIG_HOME="$SMOKE_HOME/.config" QT_QPA_PLATFORM=offscreen \
					gdb -q -batch -ex run -ex bt --args squashfs-root/usr/bin/PSRemotePlayTogether list 2>&1 | grep -v '^\[New Thread\|^\[Thread ' | tail -60 >&2
			fi
		fi
	) || true
	exit "$status"
fi
rm -rf "$SMOKE_HOME"

( cd dist && sha256sum "$(basename "$OUT")" > "$(basename "$OUT").sha256" )

# The checkout belongs to the user the job runs as
if [ -n "${SUDO_UID:-}" ]; then
	chown -R "$SUDO_UID:${SUDO_GID:-$SUDO_UID}" dist
fi

ls -l dist
