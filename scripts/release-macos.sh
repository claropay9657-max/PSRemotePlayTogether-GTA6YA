#!/bin/bash
# Builds the macOS release: a self-contained PSRemotePlayTogether.app in a .dmg,
# dist/PSRemotePlayTogether-<version>-macos-<arch>.dmg
#
# The "macOS dmg" job of .github/workflows/build.yml runs this on GitHub's
# macos-15 runners for every push and every release. To build on your own Mac:
#
#   brew install cmake ninja pkgconf nasm qtbase qtdeclarative qtsvg qtmultimedia ffmpeg libplacebo \
#       sdl2-compat opus openssl@3 protobuf@29 speexdsp json-c miniupnpc libevent
#   scripts/release-macos.sh
#
# (qtmultimedia is optional: without it the app has no webcam switch)
#
# The app bundles Homebrew's libraries, which only run on the macOS version
# they were installed on or newer. Build on the oldest macOS you want to support.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

if [ "$#" -ne 0 ]; then
	echo "usage: $0" >&2
	exit 1
fi

VERSION="$(scripts/ci/version.sh)"
ARCH="$(uname -m)"
BP="$(brew --prefix)"
BUILD_DIR="${BUILD_DIR:-build_release_macos}"
APP_NAME=PSRemotePlayTogether
MOLTENVK_VERSION=v1.4.2
MOLTENVK_SHA256=f95765a6229cb7b915990a2890ce12ebe36a730b021545d3d52ae69ce4c4024e

echo "== Building $APP_NAME $VERSION for macos-$ARCH"
mkdir -p "$BUILD_DIR" dist

# nanopb's generator needs the protobuf python module
if [ ! -x "$BUILD_DIR/.venv/bin/python" ]; then
	python3 -m venv "$BUILD_DIR/.venv"
	"$BUILD_DIR/.venv/bin/pip" install --quiet 'protobuf>=5,<6' setuptools
fi

CXX_FLAGS=""
PROJECT_INCLUDE=""

# Homebrew's openssl@4 headers in $BP/include would shadow openssl@3, which the
# bundled curl needs. The shim has to come before every other include directory
# (some targets add $BP/include with a plain -I), so it is injected as a
# directory-level include_directories(BEFORE ...).
if readlink "$BP/include/openssl" 2>/dev/null | grep -q 'openssl@' \
	&& ! readlink "$BP/include/openssl" | grep -q 'openssl@3'; then
	mkdir -p "$BUILD_DIR/openssl3-include"
	ln -sfn "$BP/opt/openssl@3/include/openssl" "$BUILD_DIR/openssl3-include/openssl"
	echo "include_directories(BEFORE \"$PWD/$BUILD_DIR/openssl3-include\")" > "$BUILD_DIR/openssl3-shim.cmake"
	PROJECT_INCLUDE="$PWD/$BUILD_DIR/openssl3-shim.cmake"
	echo "note: Homebrew links a newer OpenSSL, building against openssl@3 through an include shim" >&2
fi

# Some Command Line Tools installs carry a stale, empty
# /Library/Developer/CommandLineTools/usr/include/c++/v1 that hides the SDK's libc++ headers
if ! printf '#include <vector>\n' | c++ -std=c++17 -x c++ -fsyntax-only - 2>/dev/null; then
	SDK="$(xcrun --show-sdk-path)"
	echo "note: default libc++ headers are broken on this machine, using the SDK's copy" >&2
	CXX_FLAGS="$CXX_FLAGS -nostdinc++ -isystem $SDK/usr/include/c++/v1"
fi

cmake -S . -B "$BUILD_DIR" -G Ninja \
	-DCMAKE_BUILD_TYPE=Release \
	-DCHIAKI_ENABLE_CLI=OFF \
	-DCHIAKI_ENABLE_TESTS=OFF \
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
cmake --build "$BUILD_DIR" --target chiaki coplay-tests

echo "== Co-play tests"
# The network tests run against real timers, and on a loaded build machine one of them can
# miss its deadline. Say which, and try once more: failing twice in a row is a real failure.
TEST_LOG="$BUILD_DIR/coplay-tests.log"
if ! "$BUILD_DIR/coplay/coplay-tests" > "$TEST_LOG" 2>&1; then
	awk '/^[a-z_0-9]+$/ { name = $0 } /FAILED/ { print name ": " $0 }' "$TEST_LOG" >&2
	tail -1 "$TEST_LOG" >&2
	echo "note: running the co-play tests once more" >&2
	if ! "$BUILD_DIR/coplay/coplay-tests" > "$TEST_LOG" 2>&1; then
		awk '/^[a-z_0-9]+$/ { name = $0 } /FAILED/ { print name ": " $0 }' "$TEST_LOG" >&2
		tail -1 "$TEST_LOG" >&2
		exit 1
	fi
fi
tail -1 "$TEST_LOG"

echo "== Making the app self-contained"
STAGE="$BUILD_DIR/stage"
APP="$STAGE/$APP_NAME.app"
rm -rf "$STAGE"
mkdir -p "$STAGE"
cp -a "$BUILD_DIR/gui/$APP_NAME.app" "$APP"

MACDEPLOYQT="$(command -v macdeployqt || true)"
if [ -z "$MACDEPLOYQT" ]; then
	for candidate in "$BP/opt/qtbase/bin/macdeployqt" "$BP/opt/qt/bin/macdeployqt" "$BP/opt/qt@6/bin/macdeployqt"; do
		if [ -x "$candidate" ]; then MACDEPLOYQT="$candidate"; break; fi
	done
fi
if [ -z "$MACDEPLOYQT" ]; then
	echo "error: macdeployqt not found (brew install qtbase)" >&2
	exit 1
fi

# The PSN login page needs Qt WebEngine's QML module, when Qt has WebEngine at all
QML_DIR="$BUILD_DIR/qml-for-deploy"
rm -rf "$QML_DIR"
cp -R gui/src/qml "$QML_DIR"
if [ -d "$BP/opt/qtwebengine" ]; then
	cp scripts/qtwebengine_import.qml "$QML_DIR/"
fi

"$MACDEPLOYQT" "$APP" -qmldir="$PWD/$QML_DIR" -libpath="$BP/lib" 2> "$BUILD_DIR/macdeployqt.log" \
	|| { cat "$BUILD_DIR/macdeployqt.log" >&2; exit 1; }

# Vulkan on macOS is MoltenVK; the Vulkan loader looks for it in the bundle's Resources
MOLTENVK_TAR="$BUILD_DIR/MoltenVK-macos-$MOLTENVK_VERSION.tar"
if [ ! -f "$MOLTENVK_TAR" ]; then
	curl --fail --location --silent --show-error --retry 5 -o "$MOLTENVK_TAR.part" \
		"https://github.com/KhronosGroup/MoltenVK/releases/download/$MOLTENVK_VERSION/MoltenVK-macos.tar"
	echo "$MOLTENVK_SHA256  $MOLTENVK_TAR.part" | shasum -a 256 -c -
	mv "$MOLTENVK_TAR.part" "$MOLTENVK_TAR"
fi
rm -rf "$BUILD_DIR/MoltenVK"
tar -xf "$MOLTENVK_TAR" -C "$BUILD_DIR" MoltenVK/MoltenVK/dynamic/dylib/macOS
mkdir -p "$APP/Contents/Resources/vulkan/icd.d"
cp "$BUILD_DIR"/MoltenVK/MoltenVK/dynamic/dylib/macOS/* "$APP/Contents/Resources/vulkan/icd.d/"
# A second pass picks up whatever the first one's plugins brought in
"$MACDEPLOYQT" "$APP" -qmldir="$PWD/$QML_DIR" -libpath="$BP/lib" 2>> "$BUILD_DIR/macdeployqt.log" \
	|| { cat "$BUILD_DIR/macdeployqt.log" >&2; exit 1; }
if [ -d "$APP/Contents/Frameworks/QtWebEngineCore.framework/Helpers/QtWebEngineProcess.app" ]; then
	ln -sfn ../../../../../../../Frameworks \
		"$APP/Contents/Frameworks/QtWebEngineCore.framework/Helpers/QtWebEngineProcess.app/Contents/Frameworks"
fi

# SDL2 here is sdl2-compat, which opens SDL3 by name at run time: nothing links
# against SDL3, so macdeployqt doesn't see it. Put both next to the executable.
MACOS_DIR="$APP/Contents/MacOS"
SDL2_REF="$(otool -L "$MACOS_DIR/$APP_NAME" | awk '/libSDL2/ { print $1; exit }')"
cp -L "$BP/lib/libSDL3.dylib" "$MACOS_DIR/libSDL3.dylib"
cp -L "$BP/lib/libSDL2-2.0.0.dylib" "$MACOS_DIR/libSDL2-2.0.0.dylib"
chmod u+w "$MACOS_DIR/libSDL3.dylib" "$MACOS_DIR/libSDL2-2.0.0.dylib"
install_name_tool -id @executable_path/libSDL3.dylib "$MACOS_DIR/libSDL3.dylib"
install_name_tool -id @executable_path/libSDL2-2.0.0.dylib "$MACOS_DIR/libSDL2-2.0.0.dylib"
if [ -n "$SDL2_REF" ]; then
	install_name_tool -change "$SDL2_REF" @executable_path/libSDL2-2.0.0.dylib "$MACOS_DIR/$APP_NAME"
fi
rm -f "$APP"/Contents/Frameworks/libSDL2*.dylib
ln -sfn libvulkan.1.dylib "$APP/Contents/Frameworks/vulkan"

# Nothing in the bundle may still point into Homebrew: it would work here and fail elsewhere
LEFTOVER="$(find "$APP" -type f \( -name '*.dylib' -o -perm +111 \) -print0 \
	| xargs -0 otool -L 2>/dev/null \
	| awk -v bp="$BP" '/:$/ { file = $0 } index($1, bp) == 1 || index($1, "/usr/local/") == 1 { print file " " $1 }' || true)"
if [ -n "$LEFTOVER" ]; then
	echo "error: the app still refers to libraries outside the bundle:" >&2
	echo "$LEFTOVER" >&2
	exit 1
fi

# The signature. With a certificate of our own (the CI secrets PSRPT_MAC_SIGN_P12, base64 of a
# .p12, and PSRPT_MAC_SIGN_PASSWORD; docs/releasing.md says how to make one) every version is
# signed by the same identity, so macOS takes an update for the same app: its permissions stay
# allowed and it keeps one entry under Local Network. Without it the signature is ad hoc, which
# macOS takes for a new app every time.
SIGN_ID="-"
if [ -n "${PSRPT_MAC_SIGN_P12:-}" ]; then
	SIGN_KEYCHAIN="$PWD/build/psrpt-sign.keychain-db"
	SIGN_KEYCHAIN_PASS="$(openssl rand -hex 16)"
	OLD_KEYCHAINS="$(security list-keychains -d user | tr -d '"')"
	sign_cleanup() {
		# shellcheck disable=SC2086
		security list-keychains -d user -s $OLD_KEYCHAINS >/dev/null 2>&1 || true
		security delete-keychain "$SIGN_KEYCHAIN" >/dev/null 2>&1 || true
		rm -f "$PWD/build/psrpt-sign.p12"
	}
	trap sign_cleanup EXIT
	mkdir -p build
	printf '%s' "$PSRPT_MAC_SIGN_P12" | base64 --decode > "$PWD/build/psrpt-sign.p12"
	security create-keychain -p "$SIGN_KEYCHAIN_PASS" "$SIGN_KEYCHAIN"
	security set-keychain-settings -lut 3600 "$SIGN_KEYCHAIN"
	security unlock-keychain -p "$SIGN_KEYCHAIN_PASS" "$SIGN_KEYCHAIN"
	security import "$PWD/build/psrpt-sign.p12" -k "$SIGN_KEYCHAIN" -P "${PSRPT_MAC_SIGN_PASSWORD:-}" -T /usr/bin/codesign >/dev/null
	rm -f "$PWD/build/psrpt-sign.p12"
	security set-key-partition-list -S apple-tool:,apple:,codesign: -s -k "$SIGN_KEYCHAIN_PASS" "$SIGN_KEYCHAIN" >/dev/null
	# shellcheck disable=SC2086
	security list-keychains -d user -s "$SIGN_KEYCHAIN" $OLD_KEYCHAINS
	# Not "-v": a certificate of our own is not trusted by Apple, and need not be for this
	SIGN_ID="$(security find-identity -p codesigning "$SIGN_KEYCHAIN" | awk '/^ *1\)/ { print $2; exit }')"
	if [ -z "$SIGN_ID" ]; then
		echo "error: PSRPT_MAC_SIGN_P12 holds no certificate for signing code" >&2
		exit 1
	fi
	echo "== Signing with the project's own identity ($SIGN_ID)"
else
	echo "== Signing ad hoc (no PSRPT_MAC_SIGN_P12): macOS takes this build for a new app"
fi

codesign --force --entitlements gui/entitlements.xml --deep --sign "$SIGN_ID" "$APP"
if [ "$SIGN_ID" != "-" ]; then
	codesign --verify --deep --strict "$APP"
	# What macOS will recognise the app by from now on: the same for every version
	codesign --display --requirements - "$APP" 2>&1 | sed -n 's/^designated => /== Designated requirement: /p'
fi

echo "== Checking that the packaged app starts"
# "list" prints the registered consoles and exits; every bundled library has to load for that
"$APP/Contents/MacOS/$APP_NAME" list

DMG="dist/$APP_NAME-$VERSION-macos-$ARCH.dmg"
rm -f "$DMG"
hdiutil create -quiet -volname "$APP_NAME" -srcfolder "$STAGE" -format UDZO "$DMG"
codesign --force --sign "$SIGN_ID" "$DMG"
( cd dist && shasum -a 256 "$(basename "$DMG")" > "$(basename "$DMG").sha256" )
ls -lh "$DMG"
