// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
//
// Capture devices as the game source: a capture card (or anything else the system lists
// as a camera) with whatever is plugged into it. Every system shows a capture card as a
// camera, and its sound as a microphone of the same name, so this goes through Qt
// Multimedia on all of them, as the players' webcams do (coplaycamera.cpp). The pictures
// are turned into what the encoder takes here and encoded like a window's (pcsource.cpp).

#include "pcsource_p.h"

#include "pcscale.h"

#include <QCoreApplication>
#include <QCryptographicHash>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <mutex>

#if CHIAKI_HAVE_MULTIMEDIA
#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSource>
#include <QCamera>
#include <QCameraDevice>
#include <QCameraFormat>
#include <QIODevice>
#include <QImage>
#include <QMediaCaptureSession>
#include <QMediaDevices>
#include <QPermissions>
#include <QThread>
#include <QVideoFrame>
#include <QVideoSink>
#endif

namespace PcDevice {

bool Supported()
{
#if CHIAKI_HAVE_MULTIMEDIA
	return true;
#else
	return false;
#endif
}

#if CHIAKI_HAVE_MULTIMEDIA

/** What names a device from one start of the app to the next: the start of a hash of its id. */
static quint64 HandleOf(const QByteArray &id)
{
	const QByteArray hash = QCryptographicHash::hash(id, QCryptographicHash::Sha1);
	quint64 handle = 0;
	for(int i = 0; i < 8; i++)
		handle = (handle << 8) | static_cast<quint8>(hash[i]);
	// Never 0, which means "none" for the other kinds
	return handle ? handle : 1;
}

/**
 * The format to capture in: as high as the picture the guests get (or the highest below),
 * at the frame rate asked for where the device has it, and pictures the device does not
 * have to compress (MJPEG) where there is the choice.
 */
static QCameraFormat BestFormat(const QCameraDevice &device, int height, int fps)
{
	QCameraFormat best;
	double best_score = 0;
	for(const QCameraFormat &f : device.videoFormats())
	{
		const int h = f.resolution().height();
		if(h <= 0)
			continue;
		double score = 0;
		// Too low costs the most, too high only work
		score += h < height ? (height - h) * 4.0 : (h - height) * 1.0;
		if(f.maxFrameRate() + 0.5f < static_cast<float>(fps))
			score += (fps - f.maxFrameRate()) * 40.0;
		if(f.pixelFormat() == QVideoFrameFormat::Format_Jpeg)
			score += 50;
		if(best.isNull() || score < best_score)
		{
			best = f;
			best_score = score;
		}
	}
	return best;
}

static QCameraDevice FindDevice(const PcSource::Target &target)
{
	const QList<QCameraDevice> devices = QMediaDevices::videoInputs();
	for(const QCameraDevice &d : devices)
	{
		if(QString::fromLatin1(d.id().toHex()) == target.token)
			return d;
	}
	// Plugged into another port since: the same name
	for(const QCameraDevice &d : devices)
	{
		if(d.description() == target.title)
			return d;
	}
	return QCameraDevice();
}

QList<PcSource::Target> Targets()
{
	QList<PcSource::Target> list;
	for(const QCameraDevice &d : QMediaDevices::videoInputs())
	{
		PcSource::Target t;
		t.kind = QStringLiteral("device");
		t.handle = HandleOf(d.id());
		t.title = d.description();
		t.token = QString::fromLatin1(d.id().toHex());
		// Its largest picture: what the Host page offers to share at most
		for(const QCameraFormat &f : d.videoFormats())
		{
			const QSize s = f.resolution();
			if(s.width() * s.height() > t.width * t.height)
			{
				t.width = s.width();
				t.height = s.height();
			}
		}
		if(t.width <= 0 || t.height <= 0)
		{
			t.width = 1920;
			t.height = 1080;
		}
		list.append(t);
	}
	return list;
}

PcSource::Target Find(const PcSource::Target &chosen)
{
	const QCameraDevice device = FindDevice(chosen);
	if(device.isNull())
		return PcSource::Target();
	for(const PcSource::Target &t : Targets())
	{
		if(t.token == QString::fromLatin1(device.id().toHex()))
			return t;
	}
	return PcSource::Target();
}

/**
 * Whether the system lets the app use a camera, or a microphone for the sound; asks once
 * where nobody has said yet. Empty when it may, else why not, in words for the host.
 */
template<typename Permission>
static std::string Allowed(const char *what)
{
#if QT_CONFIG(permissions)
	Permission permission;
	switch(qApp->checkPermission(permission))
	{
		case Qt::PermissionStatus::Granted:
			return std::string();
		case Qt::PermissionStatus::Undetermined:
			// The system's question, on the main thread; the answer counts from the next start
			QMetaObject::invokeMethod(qApp, [] { qApp->requestPermission(Permission(), qApp, [](const QPermission &) {}); },
				Qt::QueuedConnection);
			return std::string("the system asks whether this app may use the ") + what
				+ " (a capture device counts as one): allow it, then share again";
		case Qt::PermissionStatus::Denied:
			return std::string("the system does not let this app use the ") + what
				+ " (a capture device counts as one): allow it in the system's privacy settings, then share again";
	}
#endif
	(void)what;
	return std::string();
}

/** A thread with an event loop of its own for a device's objects, which want one. */
class DeviceThread
{
	public:
		DeviceThread()
		{
			thread.start();
			home = new QObject;
			home->moveToThread(&thread);
		}

		~DeviceThread()
		{
			Run([this] { delete home; home = nullptr; });
			thread.quit();
			thread.wait();
		}

		/** Runs fn there and waits for it. */
		template<typename Fn>
		void Run(Fn fn)
		{
			QMetaObject::invokeMethod(home, fn, Qt::BlockingQueuedConnection);
		}

		QObject *Home() const { return home; }

	private:
		QThread thread;
		QObject *home = nullptr;
};

/** The device's pictures, scaled into the encoder's YUV. */
class DeviceVideo : public PcFrameSource
{
	public:
		~DeviceVideo() override
		{
			if(!thread)
				return;
			thread->Run([this] {
				if(camera)
					camera->stop();
				delete session;
				delete sink;
				delete camera;
				session = nullptr;
				sink = nullptr;
				camera = nullptr;
			});
			thread.reset();
		}

		bool Open(const PcCapture::Config &config, AVPixelFormat format, std::string *error)
		{
			if(format != AV_PIX_FMT_NV12 && format != AV_PIX_FMT_YUV420P)
			{
				*error = "a capture device gives NV12 or I420";
				return false;
			}
			const std::string not_allowed = Allowed<QCameraPermission>("camera");
			if(!not_allowed.empty())
			{
				*error = not_allowed;
				return false;
			}
			const QCameraDevice device = FindDevice(config.target);
			if(device.isNull())
			{
				*error = "\"" + config.target.title.toStdString() + "\" is not connected";
				return false;
			}
			width = config.width;
			height = config.height;
			this->format = format;
			const QCameraFormat chosen = BestFormat(device, height, config.fps);
			thread = std::make_unique<DeviceThread>();
			thread->Run([this, device, chosen] {
				camera = new QCamera(device);
				if(!chosen.isNull())
					camera->setCameraFormat(chosen);
				session = new QMediaCaptureSession;
				sink = new QVideoSink;
				session->setCamera(camera);
				session->setVideoSink(sink);
				// Straight from whichever thread the system delivers on: only kept here
				QObject::connect(sink, &QVideoSink::videoFrameChanged, sink, [this](const QVideoFrame &frame) {
					std::lock_guard<std::mutex> lock(mutex);
					latest = frame;
					fresh = true;
					arrived.notify_all();
				}, Qt::DirectConnection);
				QObject::connect(camera, &QCamera::errorOccurred, camera, [this](QCamera::Error e, const QString &text) {
					if(e == QCamera::NoError)
						return;
					std::lock_guard<std::mutex> lock(mutex);
					gone = true;
					gone_why = text.isEmpty() ? std::string("the capture device stopped") : text.toStdString();
					arrived.notify_all();
				}, Qt::DirectConnection);
				camera->start();
			});
			if(!chosen.isNull())
			{
				const QSize s = chosen.resolution();
				char text[160];
				snprintf(text, sizeof(text), "%dx%d at up to %.0f fps%s", s.width(), s.height(), chosen.maxFrameRate(),
					chosen.pixelFormat() == QVideoFrameFormat::Format_Jpeg ? ", compressed by the device (MJPEG)" : "");
				description = text;
			}
			return true;
		}

		AVFrame *Next(bool *ended, std::string *error) override
		{
			QVideoFrame frame;
			{
				std::unique_lock<std::mutex> lock(mutex);
				arrived.wait_for(lock, std::chrono::milliseconds(200), [this] { return fresh || gone; });
				if(gone)
				{
					*ended = true;
					*error = gone_why;
					return nullptr;
				}
				if(!fresh)
					return nullptr;
				frame = latest;
				fresh = false;
			}
			return Convert(frame);
		}

		/** What the device sends, for the log. */
		std::string Description() const { return description; }

		/** One of the device's pictures as the encoder takes it; null if it cannot be read. */
		AVFrame *Convert(const QVideoFrame &frame)
		{
			// Qt reads every format a device may send (MJPEG too), into RGB
			QImage image = frame.toImage();
			if(image.isNull())
				return nullptr;
			PcScaler::Layout layout;
			switch(image.format())
			{
				case QImage::Format_RGB32:
				case QImage::Format_ARGB32:
				case QImage::Format_ARGB32_Premultiplied:
					// 0xAARRGGBB in memory: B, G, R, A on every system the app runs on
					layout = { 2, 1, 0 };
					break;
				default:
					image.convertTo(QImage::Format_RGBX8888);
					layout = { 0, 1, 2 };
					break;
			}
			AVFrame *f = av_frame_alloc();
			f->format = format;
			f->width = width;
			f->height = height;
			if(av_frame_get_buffer(f, 0) < 0)
			{
				av_frame_free(&f);
				return nullptr;
			}
			const bool planar = format == AV_PIX_FMT_YUV420P;
			scaler.Convert(image.constBits(), static_cast<int>(image.bytesPerLine()), image.width(), image.height(), layout,
				f->data[0], f->linesize[0], f->data[1], f->linesize[1], planar ? f->data[2] : nullptr, planar ? f->linesize[2] : 0,
				width, height);
			return f;
		}

		/** For trying the conversion without a device. */
		void SetSize(int w, int h, AVPixelFormat fmt)
		{
			width = w;
			height = h;
			format = fmt;
		}

	private:
		std::unique_ptr<DeviceThread> thread;
		QCamera *camera = nullptr;
		QMediaCaptureSession *session = nullptr;
		QVideoSink *sink = nullptr;

		std::mutex mutex;
		std::condition_variable arrived;
		QVideoFrame latest;
		bool fresh = false;
		bool gone = false;
		std::string gone_why;

		int width = 1920;
		int height = 1080;
		AVPixelFormat format = AV_PIX_FMT_NV12;
		PcScaler scaler;
		std::string description;
};

/**
 * The device's own sound: the microphone the system names like the device ("Cam Link 4K",
 * "Game Capture HD60 S+"), or one whose name contains the other's.
 */
static QAudioDevice SoundOf(const PcSource::Target &target)
{
	const QString name = target.title.trimmed();
	if(name.isEmpty())
		return QAudioDevice();
	const QList<QAudioDevice> inputs = QMediaDevices::audioInputs();
	for(const QAudioDevice &d : inputs)
	{
		if(d.description().compare(name, Qt::CaseInsensitive) == 0)
			return d;
	}
	for(const QAudioDevice &d : inputs)
	{
		const QString other = d.description().trimmed();
		if(other.size() >= 4 && (other.contains(name, Qt::CaseInsensitive) || name.contains(other, Qt::CaseInsensitive)))
			return d;
	}
	return QAudioDevice();
}

/** Any sample format, rate and number of channels into 16-bit stereo at 48 kHz. */
class SoundConverter
{
	public:
		explicit SoundConverter(const QAudioFormat &format) : format(format) {}

		void Push(const char *data, qint64 bytes, const PcSamples &out)
		{
			const int channels = std::max(1, format.channelCount());
			const int frame_bytes = format.bytesPerFrame();
			if(frame_bytes <= 0)
				return;
			const qint64 frames = bytes / frame_bytes;
			// What capture cards send: as it is
			if(format.sampleFormat() == QAudioFormat::Int16 && channels == 2
				&& format.sampleRate() == static_cast<int>(PcCapture::kAudioRate))
			{
				if(frames > 0)
					out(reinterpret_cast<const int16_t *>(data), static_cast<size_t>(frames));
				return;
			}
			const double step = static_cast<double>(format.sampleRate()) / PcCapture::kAudioRate;
			converted.clear();
			for(qint64 i = 0; i < frames; i++)
			{
				const char *frame = data + i * frame_bytes;
				const float left = Sample(frame, 0);
				const float right = channels > 1 ? Sample(frame, 1) : left;
				// Straight through at 48 kHz; otherwise in between the last sample and this one
				if(std::abs(step - 1.0) < 1e-9)
					Emit(left, right);
				else
				{
					// position: where the next output sample lies between the last input sample (0) and this one (1)
					while(position < 1.0)
					{
						Emit(previous_left + (left - previous_left) * static_cast<float>(position),
							previous_right + (right - previous_right) * static_cast<float>(position));
						position += step;
					}
					position -= 1.0;
				}
				previous_left = left;
				previous_right = right;
			}
			if(!converted.empty())
				out(converted.data(), converted.size() / 2);
		}

	private:
		float Sample(const char *frame, int channel) const
		{
			const char *p = frame + channel * format.bytesPerSample();
			switch(format.sampleFormat())
			{
				case QAudioFormat::UInt8:
					return (static_cast<float>(*reinterpret_cast<const quint8 *>(p)) - 128.0f) / 128.0f;
				case QAudioFormat::Int16:
					return static_cast<float>(*reinterpret_cast<const qint16 *>(p)) / 32768.0f;
				case QAudioFormat::Int32:
					return static_cast<float>(*reinterpret_cast<const qint32 *>(p)) / 2147483648.0f;
				case QAudioFormat::Float:
					return *reinterpret_cast<const float *>(p);
				default:
					return 0.0f;
			}
		}

		void Emit(float left, float right)
		{
			auto clip = [](float v) { return static_cast<int16_t>(std::lround(std::clamp(v, -1.0f, 1.0f) * 32767.0f)); };
			converted.push_back(clip(left));
			converted.push_back(clip(right));
		}

		QAudioFormat format;
		std::vector<int16_t> converted;
		float previous_left = 0.0f;
		float previous_right = 0.0f;
		double position = 0.0;
};

class DeviceSound : public PcSoundSource
{
	public:
		~DeviceSound() override
		{
			if(!thread)
				return;
			thread->Run([this] {
				if(source)
					source->stop();
				delete source;
				source = nullptr;
			});
			thread.reset();
		}

		bool Open(const PcCapture::Config &config, PcSamples samples, PcSoundInfo *info, std::string *error)
		{
			const QAudioDevice device = SoundOf(config.target);
			if(device.isNull())
			{
				*error = "the system has no sound input named like \"" + config.target.title.toStdString() + "\"";
				return false;
			}
			const std::string not_allowed = Allowed<QMicrophonePermission>("microphone");
			if(!not_allowed.empty())
			{
				*error = not_allowed;
				return false;
			}
			QAudioFormat format;
			format.setSampleRate(static_cast<int>(PcCapture::kAudioRate));
			format.setChannelCount(static_cast<int>(PcCapture::kAudioChannels));
			format.setSampleFormat(QAudioFormat::Int16);
			if(!device.isFormatSupported(format))
				format = device.preferredFormat();
			converter = std::make_unique<SoundConverter>(format);
			this->samples = std::move(samples);
			thread = std::make_unique<DeviceThread>();
			bool started = false;
			thread->Run([this, device, format, &started] {
				source = new QAudioSource(device, format);
				// Room for 80 ms; what comes is handed on at once
				source->setBufferSize(format.bytesForDuration(80000));
				QIODevice *io = source->start();
				if(!io)
					return;
				QObject::connect(io, &QIODevice::readyRead, io, [this, io] {
					const QByteArray data = io->readAll();
					converter->Push(data.constData(), data.size(), this->samples);
				});
				started = true;
			});
			if(!started)
			{
				*error = "the sound of \"" + device.description().toStdString() + "\" could not be opened";
				return false;
			}
			info->what = "the sound of \"" + device.description().toStdString() + "\" ("
				+ std::to_string(format.sampleRate()) + " Hz, " + std::to_string(format.channelCount()) + " channels)";
			return true;
		}

	private:
		std::unique_ptr<DeviceThread> thread;
		QAudioSource *source = nullptr;
		std::unique_ptr<SoundConverter> converter;
		PcSamples samples;
};

std::unique_ptr<PcFrameSource> OpenVideo(const PcCapture::Config &config, AVPixelFormat format, std::string *error, std::string *description)
{
	auto video = std::make_unique<DeviceVideo>();
	if(!video->Open(config, format, error))
		return nullptr;
	if(description)
		*description = video->Description();
	return video;
}

std::unique_ptr<PcSoundSource> OpenSound(const PcCapture::Config &config, PcSamples samples, PcSoundInfo *info, std::string *error)
{
	auto sound = std::make_unique<DeviceSound>();
	if(!sound->Open(config, std::move(samples), info, error))
		return nullptr;
	return sound;
}

std::vector<int16_t> ConvertSoundForTest(const QAudioFormat &format, const QByteArray &data)
{
	SoundConverter converter(format);
	std::vector<int16_t> out;
	converter.Push(data.constData(), data.size(), [&out](const int16_t *samples, size_t frames) {
		out.insert(out.end(), samples, samples + frames * 2);
	});
	return out;
}

AVFrame *ConvertForTest(const QImage &image, int width, int height, AVPixelFormat format)
{
	DeviceVideo video;
	video.SetSize(width, height, format);
	return video.Convert(QVideoFrame(image));
}

#else // no Qt Multimedia

QList<PcSource::Target> Targets()
{
	return {};
}

PcSource::Target Find(const PcSource::Target &)
{
	return PcSource::Target();
}

std::unique_ptr<PcFrameSource> OpenVideo(const PcCapture::Config &, AVPixelFormat, std::string *error, std::string *)
{
	*error = "this build cannot use capture devices";
	return nullptr;
}

std::unique_ptr<PcSoundSource> OpenSound(const PcCapture::Config &, PcSamples, PcSoundInfo *, std::string *error)
{
	*error = "this build cannot use capture devices";
	return nullptr;
}

#endif

} // namespace PcDevice
