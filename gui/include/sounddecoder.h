// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_SOUNDDECODER_H
#define CHIAKI_SOUNDDECODER_H

#include <QByteArray>
#include <QString>

#include <cstdint>
#include <memory>
#include <vector>

/**
 * Sound files for the live stream: the app's own alert sounds and scene music (FLAC, in its
 * resources) and the player's own files (MP3, Ogg Vorbis or Opus, FLAC, WAV), decoded with
 * FFmpeg into 48 kHz stereo 16 bit, the format of the stream's mix (livestream.h).
 */
namespace sound {

constexpr int kRate = 48000;

/** Samples, two channels interleaved, 48 kHz. */
using Pcm = std::vector<int16_t>;

/** A whole file at once: for the short sounds and the app's own loops. */
bool DecodeAll(const QByteArray &file, Pcm *out, QString *error = nullptr);

/**
 * A file read piece by piece, for music of any length: either bytes (the app's own) or a
 * file on disk. Read() hands out the next samples and 0 at the end; Rewind() starts over,
 * sample-exact for FLAC and WAV, which makes a seamless loop.
 */
class Stream
{
	public:
		explicit Stream(const QByteArray &file);
		explicit Stream(const QString &path);
		~Stream();
		Stream(const Stream &) = delete;
		Stream &operator=(const Stream &) = delete;

		bool Open(QString *error = nullptr);
		/** Up to `frames` frames (two samples each) into out; 0 at the end or on an error. */
		size_t Read(int16_t *out, size_t frames);
		bool Rewind();

	private:
		struct Impl;
		std::unique_ptr<Impl> d;
};

} // namespace sound

#endif // CHIAKI_SOUNDDECODER_H
