#!/bin/bash
# CI entry point: cross-compiles the Windows build on Linux and packages it.
#
# Meant to run as root inside a stock debian:trixie container. To run the same
# thing locally:
#
#   docker run --rm -v "$PWD:/work" -w /work debian:trixie scripts/ci/build-windows.sh
#
# Result: dist/PSRemotePlayTogether-<version>-windows-x64.zip (the app's folder) and
# dist/PSRemotePlayTogether-<version>-windows-x64-setup.exe (an installer for the same
# folder, scripts/ci/windows-installer.nsi), each with its .sha256.
#
# With wine installed, the co-play tests, the packaged app and the installer are also run.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../.."

VERSION="$(scripts/ci/version.sh)"
export PSRPT_WIN_ROOT="${PSRPT_WIN_ROOT:-/opt/psrpt-win}"
BUILD_DIR="${PSRPT_BUILD_DIR:-build_windows}"
PACKAGE=PSRemotePlayTogether

echo "== Building $PACKAGE $VERSION for windows-x64 on linux-$(uname -m)"

scripts/ci/setup-windows-cross.sh

# purge leftover proto/nanopb_pb2.py which may have been created with another protobuf version
rm -fv third-party/nanopb/generator/proto/nanopb_pb2.py

cmake -S . -B "$BUILD_DIR" -G Ninja \
	-DCMAKE_TOOLCHAIN_FILE="$PWD/scripts/ci/windows-toolchain.cmake" \
	-DCMAKE_BUILD_TYPE=Release \
	-DCHIAKI_ENABLE_CLI=OFF \
	-DCHIAKI_ENABLE_TESTS=OFF \
	-DCHIAKI_GUI_ENABLE_SDL_GAMECONTROLLER=ON
cmake --build "$BUILD_DIR" --target chiaki coplay-tests coplay-sim

echo "== Packaging"
rm -rf dist/"$PACKAGE"
mkdir -p dist
python3 scripts/ci/deploy-windows.py \
	--root "$PSRPT_WIN_ROOT" \
	--exe "$BUILD_DIR/gui/$PACKAGE.exe" \
	--search-dir "$BUILD_DIR/third-party/cpp-steam-tools" \
	--qml-dir gui/src/qml \
	--out "dist/$PACKAGE"
cp COPYING "dist/$PACKAGE/LICENSE.txt"

if command -v wine >/dev/null; then
	echo "== Running the Windows binaries with wine"
	export WINEPREFIX="${WINEPREFIX:-$(mktemp -d)/wine}"
	# Wine's "fixme" chatter hides real problems; its errors (a DLL that fails to load) stay visible
	export WINEDEBUG="${WINEDEBUG:-fixme-all}"
	# The co-play tests, run next to the packaged DLLs they need
	cp "$BUILD_DIR/coplay/coplay-tests.exe" "dist/$PACKAGE/"
	( cd "dist/$PACKAGE" && wine coplay-tests.exe )
	rm "dist/$PACKAGE/coplay-tests.exe"
	# "list" prints the registered consoles and exits; every DLL has to load for that
	( cd "dist/$PACKAGE" && wine "$PACKAGE.exe" list )
else
	echo "note: wine is not installed, the Windows binaries were not run"
fi

OUT="$PACKAGE-$VERSION-windows-x64.zip"
( cd dist && rm -f "$OUT" && zip -r -q -9 "$OUT" "$PACKAGE" && sha256sum "$OUT" > "$OUT.sha256" )

echo "== Building the installer"
# What the program itself says its version is: four numbers, which the name of a build between releases is not
FILEVERSION="$(for part in MAJOR MINOR PATCH; do sed -n "s/^set(PSRPT_VERSION_$part \\([0-9][0-9]*\\)).*/\\1/p" CMakeLists.txt; done | paste -sd. -).0"
SETUP="$PACKAGE-$VERSION-windows-x64-setup.exe"
rm -f "dist/$SETUP"
makensis -NOCD -V2 -DVERSION="$VERSION" -DFILEVERSION="$FILEVERSION" -DSOURCE="$PWD/dist/$PACKAGE" \
	-DICON="$PWD/gui/chiaking.ico" -DOUTFILE="$PWD/dist/$SETUP" scripts/ci/windows-installer.nsi
( cd dist && sha256sum "$SETUP" > "$SETUP.sha256" )

if command -v wine >/dev/null; then
	echo "== Running the installer with wine"
	# Without its windows: it has to put the app into the user's own programs, and take it away again
	wine "dist/$SETUP" /S
	INSTALLED="$(find "$WINEPREFIX/drive_c/users" -path "*/AppData/Local/Programs/$PACKAGE/$PACKAGE.exe" | head -1)"
	if [ -z "$INSTALLED" ] || [ ! -f "$(dirname "$INSTALLED")/Uninstall.exe" ]; then
		echo "error: the installer did not put the app and its uninstaller in place" >&2
		exit 1
	fi
	echo "installed to $(dirname "$INSTALLED")"
	wine "$(dirname "$INSTALLED")/Uninstall.exe" /S
	# The uninstaller hands over to a copy of itself and returns at once
	for _ in $(seq 1 60); do [ -e "$INSTALLED" ] || break; sleep 1; done
	if [ -e "$INSTALLED" ]; then
		echo "error: the uninstaller left the app behind" >&2
		exit 1
	fi
	echo "and removed again"
fi

rm -rf dist/"$PACKAGE"
ls -l dist
