#!/bin/bash
# Builds the macOS release: a self-contained PSRemotePlayTogether.app in a .dmg.
#
#   scripts/release-macos.sh             build dist/PSRemotePlayTogether-<version>-macos-<arch>.dmg
#   scripts/release-macos.sh --upload    ... and attach it to the Forgejo release of the tag
#
# Needs a Mac with Homebrew and:
#   brew install cmake ninja pkgconf nasm qtbase qtdeclarative qtsvg ffmpeg libplacebo \
#       sdl2-compat opus openssl@3 protobuf@29 speexdsp json-c miniupnpc libevent
#
# macOS can't be built on the Linux CI runner, so this is run by hand on a Mac
# (or by the optional macOS job, see docs/releasing.md).
#
# --upload needs FORGEJO_TOKEN (an access token that may write to the
# repository) and a v* tag on the commit being built; the server and repository
# are taken from the "origin" remote.
#
# The app bundles Homebrew's libraries, which only run on the macOS version
# they were installed on or newer. Build on the oldest macOS you want to support.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

UPLOAD=0
for arg in "$@"; do
	case "$arg" in
		--upload) UPLOAD=1 ;;
		*) echo "usage: $0 [--upload]" >&2; exit 1 ;;
	esac
done

VERSION="$(scripts/ci/version.sh)"
ARCH="$(uname -m)"
BP="$(brew --prefix)"
BUILD_DIR="${BUILD_DIR:-build_release_macos}"
APP_NAME=PSRemotePlayTogether
MOLTENVK_VERSION=v1.4.2
MOLTENVK_SHA256=f95765a6229cb7b915990a2890ce12ebe36a730b021545d3d52ae69ce4c4024e

# Everything --upload needs is checked now, not after a ten-minute build
if [ "$UPLOAD" = 1 ]; then
	if [ -z "${FORGEJO_TOKEN:-}" ]; then
		echo "error: --upload needs FORGEJO_TOKEN" >&2
		exit 1
	fi
	if [ -z "${GITHUB_SERVER_URL:-}" ] || [ -z "${GITHUB_REPOSITORY:-}" ]; then
		ORIGIN="$(git remote get-url origin)"
		case "$ORIGIN" in
			http://*|https://*) ;;
			*)
				echo "error: the origin remote is not an http(s) address; set GITHUB_SERVER_URL (https://your-forgejo) and GITHUB_REPOSITORY (owner/name)" >&2
				exit 1
				;;
		esac
		ORIGIN="${ORIGIN%.git}"
		GITHUB_SERVER_URL="$(printf '%s' "$ORIGIN" | sed -E 's#^(https?://)([^/@]*@)?([^/]+)/.*#\1\3#')"
		GITHUB_REPOSITORY="$(printf '%s' "$ORIGIN" | sed -E 's#^https?://[^/]+/##')"
		export GITHUB_SERVER_URL GITHUB_REPOSITORY
	fi
	if [ -z "${RELEASE_TAG:-}${GITHUB_REF_NAME:-}" ]; then
		case "$VERSION" in
			dev*)
				echo "error: this commit has no v* tag, so there is no release to attach the build to" >&2
				exit 1
				;;
		esac
		export RELEASE_TAG="v$VERSION"
	fi
fi

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
"$BUILD_DIR/coplay/coplay-tests" | tail -2

echo "== Making the app self-contained"
STAGE="$BUILD_DIR/stage"
APP="$STAGE/$APP_NAME.app"
rm -rf "$STAGE"
mkdir -p "$STAGE"
cp -a "$BUILD_DIR/gui/chiaki.app" "$APP"

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
SDL2_REF="$(otool -L "$MACOS_DIR/chiaki" | awk '/libSDL2/ { print $1; exit }')"
cp -L "$BP/lib/libSDL3.dylib" "$MACOS_DIR/libSDL3.dylib"
cp -L "$BP/lib/libSDL2-2.0.0.dylib" "$MACOS_DIR/libSDL2-2.0.0.dylib"
chmod u+w "$MACOS_DIR/libSDL3.dylib" "$MACOS_DIR/libSDL2-2.0.0.dylib"
install_name_tool -id @executable_path/libSDL3.dylib "$MACOS_DIR/libSDL3.dylib"
install_name_tool -id @executable_path/libSDL2-2.0.0.dylib "$MACOS_DIR/libSDL2-2.0.0.dylib"
if [ -n "$SDL2_REF" ]; then
	install_name_tool -change "$SDL2_REF" @executable_path/libSDL2-2.0.0.dylib "$MACOS_DIR/chiaki"
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

codesign --force --entitlements gui/entitlements.xml --deep --sign - "$APP"

echo "== Checking that the packaged app starts"
# "list" prints the registered consoles and exits; every bundled library has to load for that
"$APP/Contents/MacOS/chiaki" list

DMG="dist/$APP_NAME-$VERSION-macos-$ARCH.dmg"
rm -f "$DMG"
hdiutil create -quiet -volname "$APP_NAME" -srcfolder "$STAGE" -format UDZO "$DMG"
codesign --force --sign - "$DMG"
( cd dist && shasum -a 256 "$(basename "$DMG")" > "$(basename "$DMG").sha256" )
ls -lh "$DMG"

if [ "$UPLOAD" = 1 ]; then
	scripts/ci/publish-release.sh "$DMG" "$DMG.sha256"
fi
