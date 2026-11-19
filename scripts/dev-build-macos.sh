#!/bin/bash
# Local developer build for macOS (Homebrew). Not used by CI.
#
#   scripts/dev-build-macos.sh            configure + build the app and the co-play tools
#   scripts/dev-build-macos.sh test       ... and run the co-play tests
#
# Works around two machine-specific problems without changing the system:
#  * Homebrew's openssl@4 headers in $(brew --prefix)/include shadow openssl@3,
#    which the bundled curl needs. Upstream CI unlinks openssl@4; here a small
#    include shim is put in front instead.
#  * Some Command Line Tools installs carry a stale, empty
#    /Library/Developer/CommandLineTools/usr/include/c++/v1 that hides the
#    SDK's libc++ headers ("'vector' file not found").

set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

BP="$(brew --prefix)"
BUILD_DIR="${BUILD_DIR:-build}"
BUILD_TYPE="${BUILD_TYPE:-RelWithDebInfo}"
mkdir -p "$BUILD_DIR"

# nanopb's generator needs the protobuf python module
if [ ! -x "$BUILD_DIR/.venv/bin/python" ]; then
	python3 -m venv "$BUILD_DIR/.venv"
	"$BUILD_DIR/.venv/bin/pip" install --quiet 'protobuf>=5,<6' setuptools
fi

CXX_FLAGS=""
PROJECT_INCLUDE=""

# The shim has to come before every other include directory (some targets add
# $BP/include with a plain -I), so it is injected as a directory-level
# include_directories(BEFORE ...) instead of through the compiler flags.
if readlink "$BP/include/openssl" 2>/dev/null | grep -q 'openssl@' \
	&& ! readlink "$BP/include/openssl" | grep -q 'openssl@3'; then
	mkdir -p "$BUILD_DIR/openssl3-include"
	ln -sfn "$BP/opt/openssl@3/include/openssl" "$BUILD_DIR/openssl3-include/openssl"
	echo "include_directories(BEFORE \"$PWD/$BUILD_DIR/openssl3-include\")" > "$BUILD_DIR/openssl3-shim.cmake"
	PROJECT_INCLUDE="$PWD/$BUILD_DIR/openssl3-shim.cmake"
	echo "note: Homebrew links a newer OpenSSL, building against openssl@3 through an include shim" >&2
fi

if ! printf '#include <vector>\n' | c++ -std=c++17 -x c++ -fsyntax-only - 2>/dev/null; then
	SDK="$(xcrun --show-sdk-path)"
	echo "note: default libc++ headers are broken on this machine, using the SDK's copy" >&2
	CXX_FLAGS="$CXX_FLAGS -nostdinc++ -isystem $SDK/usr/include/c++/v1"
fi

cmake -S . -B "$BUILD_DIR" -G Ninja \
	-DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
	-DCHIAKI_ENABLE_CLI=OFF \
	-DCHIAKI_ENABLE_STEAMDECK_NATIVE=OFF \
	-DPYTHON_EXECUTABLE="$PWD/$BUILD_DIR/.venv/bin/python" \
	-DOPENSSL_ROOT_DIR="$BP/opt/openssl@3" \
	-DOPENSSL_INCLUDE_DIR="$BP/opt/openssl@3/include" \
	-DOPENSSL_CRYPTO_LIBRARY="$BP/opt/openssl@3/lib/libcrypto.dylib" \
	-DOPENSSL_SSL_LIBRARY="$BP/opt/openssl@3/lib/libssl.dylib" \
	-DCMAKE_PREFIX_PATH="$BP/opt/openssl@3;$BP;$BP/opt/protobuf@29" \
	-DCMAKE_PROJECT_INCLUDE="$PROJECT_INCLUDE" \
	-DCMAKE_C_FLAGS="" \
	-DCMAKE_OBJC_FLAGS="" \
	-DCMAKE_CXX_FLAGS="$CXX_FLAGS" \
	-DCMAKE_OBJCXX_FLAGS="$CXX_FLAGS"

export CPATH="$BP/opt/ffmpeg/include"
cmake --build "$BUILD_DIR" --target chiaki
if grep -q '^coplay' <(cmake --build "$BUILD_DIR" --target help 2>/dev/null | sed 's/: .*//'); then
	cmake --build "$BUILD_DIR" --target coplay-tests coplay-sim
	if [ "${1:-}" = "test" ]; then
		"$BUILD_DIR/coplay/coplay-tests"
	fi
fi

echo "Built: $BUILD_DIR/gui/chiaki.app"
