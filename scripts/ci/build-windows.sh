#!/bin/bash
# CI entry point: cross-compiles the Windows build on Linux and packages it.
#
# Meant to run as root inside a stock debian:trixie container. To run the same
# thing locally:
#
#   docker run --rm -v "$PWD:/work" -w /work debian:trixie scripts/ci/build-windows.sh
#
# Result: dist/PSRemotePlayTogether-<version>-windows-x64.zip
#
# With wine installed, the co-play tests and the packaged app are also run.
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
	--exe "$BUILD_DIR/gui/chiaki.exe" --exe-name "$PACKAGE.exe" \
	--search-dir "$BUILD_DIR/third-party/cpp-steam-tools" \
	--qml-dir gui/src/qml \
	--out "dist/$PACKAGE"
cp COPYING "dist/$PACKAGE/LICENSE.txt"

if command -v wine >/dev/null; then
	echo "== Running the Windows binaries with wine"
	export WINEPREFIX="${WINEPREFIX:-$(mktemp -d)/wine}"
	# Wine's "fixme" chatter hides real problems; its errors (a DLL that fails to load) stay visible
	export WINEDEBUG="${WINEDEBUG:-fixme-all}"
	# The tests only need the C++ runtime next to them
	TEST_DIR="$(mktemp -d)"
	cp "$BUILD_DIR/coplay/coplay-tests.exe" "$TEST_DIR/"
	cp "dist/$PACKAGE"/libc++.dll "dist/$PACKAGE"/libunwind.dll "dist/$PACKAGE"/libcrypto-*.dll "$TEST_DIR/"
	cp "dist/$PACKAGE"/libwinpthread-1.dll "$TEST_DIR/" 2>/dev/null || true
	( cd "$TEST_DIR" && wine coplay-tests.exe )
	# "list" prints the registered consoles and exits; every DLL has to load for that
	( cd "dist/$PACKAGE" && wine "$PACKAGE.exe" list )
	rm -rf "$TEST_DIR"
else
	echo "note: wine is not installed, the Windows binaries were not run"
fi

OUT="$PACKAGE-$VERSION-windows-x64.zip"
( cd dist && rm -f "$OUT" && zip -r -q -9 "$OUT" "$PACKAGE" && sha256sum "$OUT" > "$OUT.sha256" )
rm -rf dist/"$PACKAGE"
ls -l dist
