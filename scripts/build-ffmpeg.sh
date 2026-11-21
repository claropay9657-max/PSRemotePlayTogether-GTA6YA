#!/bin/bash

export SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
cd $(dirname "${BASH_SOURCE[0]}")/..
cd "./$1"
shift
ROOT="`pwd`"

TAG=n9.0.2
NV_CODEC_TAG=n13.1.15.0

FFMPEG_NV_CODEC_FLAGS=()
if [ "$(uname -s)" = "Linux" ] && [ "$(uname -m)" = "x86_64" ]
then
    if [ ! -d "nv-codec-headers" ]; then
        git clone https://github.com/FFmpeg/nv-codec-headers.git || exit 1
    fi
    cd nv-codec-headers || exit 1
    git checkout $NV_CODEC_TAG || exit 1
    make PREFIX=/usr install || exit 1
    cd .. || exit 1

    FFMPEG_NV_CODEC_FLAGS=(
        --enable-decoder=h264_cuvid
        --enable-decoder=hevc_cuvid
        --enable-hwaccel=h264_nvdec
        --enable-hwaccel=hevc_nvdec
        --enable-cuvid
        --enable-nvdec
        --enable-ffnvcodec
        # Going live: NVIDIA's encoder
        --enable-nvenc
        --enable-encoder=h264_nvenc
    )
fi

# Going live (gui/src/livestream.cpp): H.264 in hardware (VAAPI here, NVENC above), AAC,
# FLV over RTMP or RTMPS (TLS through OpenSSL 3, which FFmpeg takes as LGPL v3)
FFMPEG_LIVE_FLAGS=(
    --enable-version3
    --enable-openssl
    --enable-encoder=aac
    --enable-encoder=h264_vaapi
    --enable-muxer=flv
    --enable-protocol=rtmp
    --enable-protocol=rtmps
    --enable-protocol=tcp
    --enable-protocol=tls
    --enable-protocol=file
)
# Sounds (gui/src/sounddecoder.cpp): the app's own alert sounds and scene music are FLAC;
# players may pick their own music as MP3, Ogg (Vorbis, Opus), FLAC or WAV
FFMPEG_LIVE_FLAGS+=(
    --enable-decoder=flac,mp3float,mp3,vorbis,opus,pcm_s16le,pcm_s24le,pcm_s32le,pcm_f32le,pcm_u8
    --enable-demuxer=flac,mp3,ogg,wav
    --enable-parser=flac,mpegaudio,opus,vorbis
)
# Cisco's OpenH264 for computers without a hardware encoder, where the system has it
if pkg-config --exists openh264
then
    FFMPEG_LIVE_FLAGS+=(--enable-libopenh264 --enable-encoder=libopenh264)
else
    echo "note: openh264 not found, live streaming needs a hardware encoder (VAAPI or NVENC)" >&2
fi

git clone https://github.com/FFmpeg/FFmpeg.git ffmpeg --depth 1 -b $TAG && cd ffmpeg || exit 1
git apply ${SCRIPT_DIR}/flatpak/0001-vulkan-ignore-frames-without-hw-context.patch || exit 1
./configure --disable-all --enable-static --enable-avformat --enable-vulkan --enable-libdrm --enable-avcodec --enable-decoder=h264 --enable-decoder=hevc --enable-hwaccel=h264_vaapi --enable-hwaccel=hevc_vaapi --enable-hwaccel=h264_vulkan --enable-hwaccel=hevc_vulkan "${FFMPEG_NV_CODEC_FLAGS[@]}" "${FFMPEG_LIVE_FLAGS[@]}" --prefix=/usr "$@" || exit 1
make -j4 || exit 1
make install || exit 1
