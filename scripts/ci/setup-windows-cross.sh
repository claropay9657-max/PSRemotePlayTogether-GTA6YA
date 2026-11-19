#!/bin/bash
# Installs everything needed to cross-compile the Windows build on Linux.
#
# Runs as root inside a stock debian:trixie container (x86_64 or arm64) and
# fills $PSRPT_WIN_ROOT (default /opt/psrpt-win) with:
#
#   llvm-mingw/   the compiler: clang + lld + mingw-w64 (UCRT), targets x86_64-w64-mingw32
#   qt/           Qt for Windows (the official llvm-mingw build) and the same Qt
#                 version for this machine, whose moc/rcc/qml tools the build runs
#   msys2/        the other libraries, taken prebuilt from MSYS2's CLANG64 repository
#   ffmpeg/       FFmpeg built here from source with only the decoders the app uses
#
# scripts/ci/build-windows.sh then builds and packages the app against it.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="${PSRPT_WIN_ROOT:-/opt/psrpt-win}"

LLVM_MINGW_VERSION=20261117
QT_VERSION=6.9.3
AQT_VERSION=3.3.0
# Same FFmpeg and NVIDIA header versions as scripts/build-ffmpeg-windows.sh
FFMPEG_TAG=n9.0.2
NV_CODEC_TAG=n13.1.15.0
# Extra Qt modules beyond qtbase, qtdeclarative and qtsvg
QT_MODULES="${PSRPT_QT_MODULES-qtmultimedia}"

# Libraries taken from MSYS2 (names without the mingw-w64-clang-x86_64- prefix)
MSYS2_PACKAGES="libplacebo sdl2-compat opus openssl json-c miniupnpc libevent speexdsp hidapi fftw vulkan-headers vulkan-loader"
# The compiler and its runtime come from llvm-mingw
MSYS2_SKIP="clang,llvm,llvm-libs,lld,compiler-rt,crt,headers,winpthreads,python"
MSYS2_RUNTIME_ONLY="libc++,libunwind,libwinpthread"

HOST_ARCH="$(uname -m)"
echo "== Setting up the Windows cross-build on linux-$HOST_ARCH in $ROOT"
mkdir -p "$ROOT"

if command -v apt-get >/dev/null; then
	export DEBIAN_FRONTEND=noninteractive
	apt-get update -qq
	apt-get install -y -qq --no-install-recommends \
		ca-certificates curl git xz-utils zstd unzip zip file patch perl make \
		cmake ninja-build pkgconf nasm gcc libc6-dev \
		python3 python3-venv python3-protobuf protobuf-compiler \
		libglib2.0-0t64 libdbus-1-3 >/dev/null
fi

download() { # url dest
	curl --fail --location --silent --show-error --retry 5 --retry-delay 3 --retry-all-errors -o "$2" "$1"
}

### llvm-mingw ###############################################################
if [ ! -x "$ROOT/llvm-mingw/bin/x86_64-w64-mingw32-clang" ]; then
	case "$HOST_ARCH" in
		x86_64)
			LLVM_MINGW_FILE="llvm-mingw-${LLVM_MINGW_VERSION}-ucrt-ubuntu-22.04-x86_64.tar.xz"
			LLVM_MINGW_SHA256="e7a15a49a2ea95e9f3d8a9d0cfabe5052a4b10fb4777cc54de5a8c27b64bb701"
			;;
		aarch64)
			LLVM_MINGW_FILE="llvm-mingw-${LLVM_MINGW_VERSION}-ucrt-ubuntu-22.04-aarch64.tar.xz"
			LLVM_MINGW_SHA256="6d9442ead9ace3b3e4c3389bfef69f93f873305e0705e6cc60978ac895320872"
			;;
		*) echo "error: no llvm-mingw build for $HOST_ARCH" >&2; exit 1 ;;
	esac
	echo "== llvm-mingw $LLVM_MINGW_VERSION"
	download "https://github.com/mstorsjo/llvm-mingw/releases/download/${LLVM_MINGW_VERSION}/${LLVM_MINGW_FILE}" "/tmp/${LLVM_MINGW_FILE}"
	echo "${LLVM_MINGW_SHA256}  /tmp/${LLVM_MINGW_FILE}" | sha256sum -c -
	rm -rf "$ROOT/llvm-mingw" && mkdir -p "$ROOT/llvm-mingw"
	tar -xJf "/tmp/${LLVM_MINGW_FILE}" -C "$ROOT/llvm-mingw" --strip-components=1
	rm -f "/tmp/${LLVM_MINGW_FILE}"
	# Only the x86_64 target is used
	rm -rf "$ROOT"/llvm-mingw/{aarch64,armv7,i686,arm64ec}-w64-mingw32
fi
export PATH="$ROOT/llvm-mingw/bin:$PATH"
x86_64-w64-mingw32-clang --version | head -1

### Qt #######################################################################
QT_ROOT="$ROOT/qt"
case "$HOST_ARCH" in
	x86_64) QT_HOST_OS=linux; QT_HOST_KIT=linux_gcc_64; QT_HOST_DIR=gcc_64 ;;
	aarch64) QT_HOST_OS=linux_arm64; QT_HOST_KIT=linux_gcc_arm64; QT_HOST_DIR=gcc_arm64 ;;
esac
if [ ! -d "$QT_ROOT/$QT_VERSION/llvm-mingw_64/lib/cmake/Qt6" ] || [ ! -x "$QT_ROOT/$QT_VERSION/$QT_HOST_DIR/libexec/moc" ]; then
	echo "== Qt $QT_VERSION (Windows libraries + $QT_HOST_KIT tools)"
	python3 -m venv "$ROOT/aqt-venv"
	"$ROOT/aqt-venv/bin/pip" install --quiet "aqtinstall==$AQT_VERSION"
	QT_MODULE_ARGS=()
	if [ -n "$QT_MODULES" ]; then
		# shellcheck disable=SC2206
		QT_MODULE_ARGS=(-m $QT_MODULES)
	fi
	"$ROOT/aqt-venv/bin/aqt" install-qt -O "$QT_ROOT" windows desktop "$QT_VERSION" win64_llvm_mingw \
		--archives qtbase qtdeclarative qtsvg "${QT_MODULE_ARGS[@]}"
	"$ROOT/aqt-venv/bin/aqt" install-qt -O "$QT_ROOT" "$QT_HOST_OS" desktop "$QT_VERSION" "$QT_HOST_KIT" \
		--archives qtbase qtdeclarative icu "${QT_MODULE_ARGS[@]}"
	rm -rf "$ROOT/aqt-venv" aqtinstall.log
	# Static libraries of Qt's own developer tools: over 1.5 GB that nothing here links
	rm -f "$QT_ROOT/$QT_VERSION"/*/lib/libQt6QmlLS.a "$QT_ROOT/$QT_VERSION"/*/lib/libQt6QmlDom.a \
		"$QT_ROOT/$QT_VERSION/$QT_HOST_DIR"/lib/libQt6*.a
fi
ln -sfn "$QT_VERSION/llvm-mingw_64" "$QT_ROOT/target"
ln -sfn "$QT_VERSION/$QT_HOST_DIR" "$QT_ROOT/host"
"$QT_ROOT/host/libexec/moc" -v

### Libraries from MSYS2 #####################################################
if [ ! -f "$ROOT/msys2/packages.json" ]; then
	echo "== Libraries from MSYS2"
	PYTHONUNBUFFERED=1 python3 "$SCRIPT_DIR/msys2-fetch.py" --prefix "$ROOT/msys2" \
		--skip "$MSYS2_SKIP" --runtime-only "$MSYS2_RUNTIME_ONLY" $MSYS2_PACKAGES
	rm -rf "$ROOT/msys2/.cache"
	# The build looks SDL up as sdl2; sdl2-compat provides exactly that interface on top of SDL3
	if [ ! -f "$ROOT/msys2/clang64/lib/pkgconfig/sdl2.pc" ]; then
		cp "$ROOT/msys2/clang64/lib/pkgconfig/sdl2-compat.pc" "$ROOT/msys2/clang64/lib/pkgconfig/sdl2.pc"
	fi
fi

### FFmpeg ###################################################################
if [ ! -f "$ROOT/ffmpeg/lib/pkgconfig/libavcodec.pc" ]; then
	echo "== FFmpeg $FFMPEG_TAG"
	WORK="$(mktemp -d)"
	git -c advice.detachedHead=false clone --quiet --depth 1 -b "$NV_CODEC_TAG" https://github.com/FFmpeg/nv-codec-headers.git "$WORK/nv-codec-headers"
	make -C "$WORK/nv-codec-headers" PREFIX="$ROOT/ffmpeg" install >/dev/null
	git -c advice.detachedHead=false clone --quiet --depth 1 -b "$FFMPEG_TAG" https://github.com/FFmpeg/FFmpeg.git "$WORK/ffmpeg"
	(
		cd "$WORK/ffmpeg"
		patch -p1 < "$SCRIPT_DIR/../flatpak/0001-vulkan-ignore-frames-without-hw-context.patch"
		# The decoder list is the one of scripts/build-ffmpeg-windows.sh
		PKG_CONFIG_LIBDIR="$ROOT/ffmpeg/lib/pkgconfig:$ROOT/msys2/clang64/lib/pkgconfig:$ROOT/msys2/clang64/share/pkgconfig" \
		./configure \
			--prefix="$ROOT/ffmpeg" \
			--arch=x86_64 --target-os=mingw32 --cross-prefix=x86_64-w64-mingw32- \
			--cc=x86_64-w64-mingw32-clang --cxx=x86_64-w64-mingw32-clang++ \
			--host-cc=gcc --pkg-config=pkg-config \
			--extra-cflags="-I$ROOT/msys2/clang64/include" \
			--disable-all --disable-doc --disable-debug --enable-shared --disable-static \
			--enable-avformat --enable-avcodec \
			--enable-decoder=h264 --enable-decoder=hevc \
			--enable-decoder=h264_cuvid --enable-decoder=hevc_cuvid \
			--enable-hwaccel=h264_vulkan --enable-hwaccel=hevc_vulkan \
			--enable-hwaccel=h264_d3d11va --enable-hwaccel=hevc_d3d11va \
			--enable-hwaccel=h264_nvdec --enable-hwaccel=hevc_nvdec \
			--enable-cuvid --enable-nvdec --enable-ffnvcodec
		make -j"$(nproc)" >/dev/null
		make install >/dev/null
	)
	rm -rf "$WORK"
fi

du -sh "$ROOT"/* 2>/dev/null || true
echo "== Windows cross-build environment ready in $ROOT"
