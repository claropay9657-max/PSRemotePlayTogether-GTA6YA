// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "sounddecoder.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
}

#include <algorithm>
#include <cstring>

namespace sound {

namespace {

/** One sample of a decoded frame as -1..1, whatever FFmpeg's format. */
float SampleAt(const AVFrame *f, int channel, int index)
{
	const auto fmt = static_cast<AVSampleFormat>(f->format);
	const int channels = f->ch_layout.nb_channels;
	const bool planar = av_sample_fmt_is_planar(fmt);
	const uint8_t *base = planar ? f->extended_data[channel] : f->extended_data[0];
	const int at = planar ? index : index * channels + channel;
	switch(fmt)
	{
		case AV_SAMPLE_FMT_U8:
		case AV_SAMPLE_FMT_U8P:
			return (static_cast<int>(base[at]) - 128) / 128.0f;
		case AV_SAMPLE_FMT_S16:
		case AV_SAMPLE_FMT_S16P:
			return reinterpret_cast<const int16_t *>(base)[at] / 32768.0f;
		case AV_SAMPLE_FMT_S32:
		case AV_SAMPLE_FMT_S32P:
			return static_cast<float>(reinterpret_cast<const int32_t *>(base)[at] / 2147483648.0);
		case AV_SAMPLE_FMT_FLT:
		case AV_SAMPLE_FMT_FLTP:
			return reinterpret_cast<const float *>(base)[at];
		case AV_SAMPLE_FMT_DBL:
		case AV_SAMPLE_FMT_DBLP:
			return static_cast<float>(reinterpret_cast<const double *>(base)[at]);
		default:
			return 0.0f;
	}
}

int16_t ToS16(float v)
{
	return static_cast<int16_t>(std::clamp(v, -1.0f, 1.0f) * 32767.0f);
}

/** The bytes of a file in memory, as FFmpeg reads them. */
struct Memory
{
	QByteArray data;
	int64_t pos = 0;
};

int ReadMemory(void *opaque, uint8_t *buf, int size)
{
	auto *m = static_cast<Memory *>(opaque);
	const int64_t left = m->data.size() - m->pos;
	if(left <= 0)
		return AVERROR_EOF;
	const int n = static_cast<int>(std::min<int64_t>(left, size));
	memcpy(buf, m->data.constData() + m->pos, static_cast<size_t>(n));
	m->pos += n;
	return n;
}

int64_t SeekMemory(void *opaque, int64_t offset, int whence)
{
	auto *m = static_cast<Memory *>(opaque);
	if(whence & AVSEEK_SIZE)
		return m->data.size();
	whence &= ~AVSEEK_FORCE;
	int64_t to = offset;
	if(whence == SEEK_CUR)
		to = m->pos + offset;
	else if(whence == SEEK_END)
		to = m->data.size() + offset;
	if(to < 0 || to > m->data.size())
		return -1;
	m->pos = to;
	return to;
}

} // namespace

struct Stream::Impl
{
	bool from_memory = false;
	Memory memory;
	QString path;

	AVFormatContext *fmt = nullptr;
	AVIOContext *io = nullptr;
	AVCodecContext *codec = nullptr;
	AVPacket *packet = nullptr;
	AVFrame *frame = nullptr;
	int stream = -1;
	int rate = kRate;
	bool draining = false;
	bool ended = false;

	// Decoded and brought to 48 kHz stereo, not yet handed out
	Pcm pending;
	size_t pending_at = 0;
	// Resampling to 48 kHz: where between the last two input frames the next output frame lies
	double phase = 0.0;
	float last[2] = { 0.0f, 0.0f };
	bool have_last = false;

	~Impl() { Close(); }

	void Close()
	{
		av_frame_free(&frame);
		av_packet_free(&packet);
		avcodec_free_context(&codec);
		if(fmt)
			avformat_close_input(&fmt);
		if(io)
		{
			av_freep(&io->buffer);
			avio_context_free(&io);
		}
	}

	bool Open(QString *error)
	{
		auto fail = [error](const QString &what) {
			if(error)
				*error = what;
			return false;
		};
		fmt = avformat_alloc_context();
		if(!fmt)
			return fail(QStringLiteral("out of memory"));
		QByteArray name;
		if(from_memory)
		{
			memory.pos = 0;
			constexpr int kBuf = 32768;
			auto *buf = static_cast<unsigned char *>(av_malloc(kBuf));
			io = avio_alloc_context(buf, kBuf, 0, &memory, ReadMemory, nullptr, SeekMemory);
			if(!io)
				return fail(QStringLiteral("out of memory"));
			fmt->pb = io;
		}
		else
		{
			name = path.toUtf8();
		}
		if(avformat_open_input(&fmt, from_memory ? nullptr : name.constData(), nullptr, nullptr) < 0)
		{
			fmt = nullptr; // freed by FFmpeg on failure
			return fail(QStringLiteral("not a sound file this app can read"));
		}
		if(avformat_find_stream_info(fmt, nullptr) < 0)
			return fail(QStringLiteral("not a sound file this app can read"));
		const AVCodec *decoder = nullptr;
		stream = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, &decoder, 0);
		if(stream < 0 || !decoder)
			return fail(QStringLiteral("no sound this app can play in it"));
		codec = avcodec_alloc_context3(decoder);
		if(!codec || avcodec_parameters_to_context(codec, fmt->streams[stream]->codecpar) < 0 || avcodec_open2(codec, decoder, nullptr) < 0)
			return fail(QStringLiteral("no sound this app can play in it"));
		rate = codec->sample_rate > 0 ? codec->sample_rate : kRate;
		packet = av_packet_alloc();
		frame = av_frame_alloc();
		return packet && frame;
	}

	/** Brings one decoded frame to 48 kHz stereo into `pending`. */
	void Take(const AVFrame *f)
	{
		const int channels = std::max(1, f->ch_layout.nb_channels);
		const double step = static_cast<double>(rate) / kRate;
		for(int i = 0; i < f->nb_samples; i++)
		{
			const float l = SampleAt(f, 0, i);
			const float r = channels > 1 ? SampleAt(f, 1, i) : l;
			if(rate == kRate)
			{
				pending.push_back(ToS16(l));
				pending.push_back(ToS16(r));
				continue;
			}
			if(!have_last)
			{
				last[0] = l;
				last[1] = r;
				have_last = true;
				continue;
			}
			while(phase < 1.0)
			{
				const float t = static_cast<float>(phase);
				pending.push_back(ToS16(last[0] + (l - last[0]) * t));
				pending.push_back(ToS16(last[1] + (r - last[1]) * t));
				phase += step;
			}
			phase -= 1.0;
			last[0] = l;
			last[1] = r;
		}
	}

	/** Decodes until there is something pending or the file is over. */
	bool DecodeMore()
	{
		while(!ended)
		{
			int got = avcodec_receive_frame(codec, frame);
			if(got == 0)
			{
				Take(frame);
				av_frame_unref(frame);
				return true;
			}
			if(got == AVERROR_EOF)
			{
				ended = true;
				return false;
			}
			if(got != AVERROR(EAGAIN))
			{
				ended = true;
				return false;
			}
			if(draining)
				continue;
			int read = av_read_frame(fmt, packet);
			if(read < 0)
			{
				// The end: what the decoder still holds
				draining = true;
				avcodec_send_packet(codec, nullptr);
				continue;
			}
			if(packet->stream_index == stream)
				avcodec_send_packet(codec, packet);
			av_packet_unref(packet);
		}
		return false;
	}

	size_t Read(int16_t *out, size_t frames)
	{
		size_t done = 0;
		while(done < frames)
		{
			if(pending_at >= pending.size())
			{
				pending.clear();
				pending_at = 0;
				if(!DecodeMore())
					break;
				continue;
			}
			const size_t have = (pending.size() - pending_at) / 2;
			const size_t n = std::min(have, frames - done);
			memcpy(out + done * 2, pending.data() + pending_at, n * 2 * sizeof(int16_t));
			pending_at += n * 2;
			done += n;
		}
		return done;
	}

	bool Rewind()
	{
		if(!fmt || !codec)
			return false;
		if(avformat_seek_file(fmt, stream, INT64_MIN, 0, 0, 0) < 0 && av_seek_frame(fmt, stream, 0, AVSEEK_FLAG_BACKWARD) < 0)
			return false;
		avcodec_flush_buffers(codec);
		draining = false;
		ended = false;
		pending.clear();
		pending_at = 0;
		// The resampler goes on from the end into the beginning: no click at the seam
		return true;
	}
};

Stream::Stream(const QByteArray &file) : d(std::make_unique<Impl>())
{
	d->from_memory = true;
	d->memory.data = file;
}

Stream::Stream(const QString &path) : d(std::make_unique<Impl>())
{
	d->path = path;
}

Stream::~Stream() = default;

bool Stream::Open(QString *error)
{
	return d->Open(error);
}

size_t Stream::Read(int16_t *out, size_t frames)
{
	return d->Read(out, frames);
}

bool Stream::Rewind()
{
	return d->Rewind();
}

bool DecodeAll(const QByteArray &file, Pcm *out, QString *error)
{
	Stream s(file);
	if(!s.Open(error))
		return false;
	out->clear();
	std::vector<int16_t> chunk(4096 * 2);
	for(;;)
	{
		const size_t n = s.Read(chunk.data(), 4096);
		if(n == 0)
			break;
		out->insert(out->end(), chunk.begin(), chunk.begin() + static_cast<long>(n * 2));
	}
	if(out->empty())
	{
		if(error)
			*error = QStringLiteral("no sound in it");
		return false;
	}
	return true;
}

} // namespace sound
