// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "coplaycamera.h"

#include <coplay/protocol.h>

#include <QBuffer>
#include <QCoreApplication>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QtMath>

#if CHIAKI_GUI_ENABLE_CAMERA
#include <QCamera>
#include <QCameraDevice>
#include <QMediaCaptureSession>
#include <QMediaDevices>
#include <QVideoFrame>
#include <QVideoSink>
#if QT_CONFIG(permissions)
#include <QPermissions>
#endif
#endif

// ---------------------------------------------------------------- camera

CoPlayCamera::CoPlayCamera(QObject *parent) : QObject(parent)
{
	connect(&test_timer, &QTimer::timeout, this, &CoPlayCamera::TestTick);
}

CoPlayCamera::~CoPlayCamera()
{
	Stop();
}

bool CoPlayCamera::Supported()
{
#if CHIAKI_GUI_ENABLE_CAMERA
	return true;
#else
	return false;
#endif
}

QStringList CoPlayCamera::Devices()
{
	QStringList names;
#if CHIAKI_GUI_ENABLE_CAMERA
	const QList<QCameraDevice> devices = QMediaDevices::videoInputs();
	for(const QCameraDevice &d : devices)
		names.append(d.description());
#endif
	names.append(TestDevice());
	return names;
}

void CoPlayCamera::SetActive(bool on)
{
	if(active == on)
		return;
	active = on;
	emit ActiveChanged();
}

void CoPlayCamera::Start(const QString &device_name, int quality)
{
	Stop();
	device = device_name;
	switch(quality)
	{
		case 0:
			height = 180;
			interval_ms = 125;
			jpeg_quality = 50;
			break;
		case 2:
			height = 360;
			interval_ms = 66;
			jpeg_quality = 60;
			break;
		default:
			height = 270;
			interval_ms = 83;
			jpeg_quality = 55;
			break;
	}
	wanted = true;
	since_last.invalidate();

	if(device == TestDevice())
	{
		test_frame = 0;
		test_timer.start(interval_ms);
		SetActive(true);
		return;
	}

#if CHIAKI_GUI_ENABLE_CAMERA
#if QT_CONFIG(permissions)
	QCameraPermission permission;
	switch(qApp->checkPermission(permission))
	{
		case Qt::PermissionStatus::Undetermined:
			// Asks the user; comes back here with the answer
			qApp->requestPermission(permission, this, [this](const QPermission &p) {
				if(!wanted)
					return;
				if(p.status() == Qt::PermissionStatus::Granted)
					StartCamera();
				else
				{
					wanted = false;
					emit Failed(tr("The camera cannot be used without your permission."));
				}
			});
			return;
		case Qt::PermissionStatus::Denied:
			wanted = false;
			emit Failed(tr("This app is not allowed to use the camera. You can change that in the system's privacy settings."));
			return;
		case Qt::PermissionStatus::Granted:
			break;
	}
#endif
	StartCamera();
#else
	wanted = false;
	emit Failed(tr("This build of the app cannot use cameras."));
#endif
}

void CoPlayCamera::StartCamera()
{
#if CHIAKI_GUI_ENABLE_CAMERA
	QCameraDevice chosen = QMediaDevices::defaultVideoInput();
	const QList<QCameraDevice> devices = QMediaDevices::videoInputs();
	for(const QCameraDevice &d : devices)
	{
		if(d.description() == device)
		{
			chosen = d;
			break;
		}
	}
	if(chosen.isNull())
	{
		wanted = false;
		emit Failed(tr("No camera found."));
		return;
	}

	// The smallest format that is still large enough: less work for every picture
	QCameraFormat best;
	const QList<QCameraFormat> formats = chosen.videoFormats();
	for(const QCameraFormat &f : formats)
	{
		if(f.resolution().height() < height)
			continue;
		if(best.isNull() || f.resolution().height() < best.resolution().height()
			|| (f.resolution().height() == best.resolution().height() && f.maxFrameRate() < best.maxFrameRate()
				&& f.maxFrameRate() >= 1000.0f / interval_ms))
			best = f;
	}

	camera = new QCamera(chosen, this);
	if(!best.isNull())
		camera->setCameraFormat(best);
	capture = new QMediaCaptureSession(this);
	sink = new QVideoSink(this);
	capture->setCamera(camera);
	capture->setVideoSink(sink);
	connect(sink, &QVideoSink::videoFrameChanged, this, &CoPlayCamera::OnVideoFrame);
	connect(camera, &QCamera::errorOccurred, this, [this](QCamera::Error error, const QString &text) {
		if(error == QCamera::NoError)
			return;
		Stop();
		emit Failed(text.isEmpty() ? tr("The camera stopped working.") : text);
	});
	camera->start();
	SetActive(true);
#endif
}

void CoPlayCamera::Stop()
{
	wanted = false;
	test_timer.stop();
#if CHIAKI_GUI_ENABLE_CAMERA
	if(camera)
	{
		camera->stop();
		// Not while one of them may be the caller
		sink->disconnect(this);
		camera->disconnect(this);
		sink->deleteLater();
		capture->deleteLater();
		camera->deleteLater();
		sink = nullptr;
		capture = nullptr;
		camera = nullptr;
	}
#endif
	SetActive(false);
}

void CoPlayCamera::OnVideoFrame(const QVideoFrame &frame)
{
#if CHIAKI_GUI_ENABLE_CAMERA
	if(!active || !frame.isValid())
		return;
	if(since_last.isValid() && since_last.elapsed() < interval_ms - 5)
		return;
	since_last.start();
	QImage image = frame.toImage();
	if(!image.isNull())
		Process(image);
#else
	Q_UNUSED(frame);
#endif
}

void CoPlayCamera::TestTick()
{
	// A picture that visibly moves, so that a frozen or missing stream is easy to tell
	const int w = height * 16 / 9;
	QImage image(w, height, QImage::Format_RGB32);
	QPainter p(&image);
	p.setRenderHint(QPainter::Antialiasing);
	QLinearGradient background(0, 0, w, height);
	background.setColorAt(0.0, QColor(0x1b, 0x12, 0x3d));
	background.setColorAt(1.0, QColor(0x0b, 0x2a, 0x4a));
	p.fillRect(image.rect(), background);
	double t = test_frame * interval_ms / 1000.0;
	double x = w * (0.5 + 0.38 * qCos(t * 1.7));
	double y = height * (0.5 + 0.3 * qSin(t * 2.3));
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(0xa8, 0x55, 0xf7));
	p.drawEllipse(QPointF(x, y), height * 0.14, height * 0.14);
	p.setPen(Qt::white);
	QFont font = p.font();
	font.setPixelSize(qMax(10, height / 9));
	font.setBold(true);
	p.setFont(font);
	p.drawText(image.rect().adjusted(8, 6, -8, -6), Qt::AlignLeft | Qt::AlignTop, tr("Test picture"));
	p.drawText(image.rect().adjusted(8, 6, -8, -6), Qt::AlignRight | Qt::AlignBottom, QString::number(test_frame));
	p.end();
	test_frame++;
	Process(image);
}

void CoPlayCamera::Process(const QImage &source)
{
	QImage image = source;
	if(image.height() != height)
		image = image.scaledToHeight(height, Qt::SmoothTransformation);
	if(image.format() != QImage::Format_RGB32)
		image = image.convertToFormat(QImage::Format_RGB32);

	QByteArray jpeg;
	for(int quality = jpeg_quality; quality >= 20; quality -= 15)
	{
		jpeg.clear();
		QBuffer buffer(&jpeg);
		buffer.open(QIODevice::WriteOnly);
		if(!image.save(&buffer, "JPEG", quality))
		{
			jpeg.clear();
			break;
		}
		if(jpeg.size() <= static_cast<int>(coplay::kMaxCamFrameSize))
			break;
		jpeg.clear(); // too detailed for one picture: try again coarser
	}
	if(jpeg.isEmpty())
		return;
	emit Frame(jpeg, image);
}

// ---------------------------------------------------------------- pictures for QML

namespace {
struct CamStore
{
	QMutex mutex;
	QHash<int, QImage> images;
};
CamStore &Store()
{
	static CamStore store;
	return store;
}
} // namespace

void CoPlayCamProvider::Set(int slot, const QImage &image)
{
	QMutexLocker locker(&Store().mutex);
	Store().images.insert(slot, image);
}

void CoPlayCamProvider::Clear(int slot)
{
	QMutexLocker locker(&Store().mutex);
	Store().images.remove(slot);
}

void CoPlayCamProvider::ClearAll()
{
	QMutexLocker locker(&Store().mutex);
	Store().images.clear();
}

QImage CoPlayCamProvider::requestImage(const QString &id, QSize *size, const QSize &requested_size)
{
	Q_UNUSED(requested_size);
	int slot = id.section('/', 0, 0).toInt();
	QImage image;
	{
		QMutexLocker locker(&Store().mutex);
		image = Store().images.value(slot);
	}
	if(image.isNull())
	{
		// QML complains about an empty picture; hand out a see-through pixel instead
		image = QImage(1, 1, QImage::Format_ARGB32);
		image.fill(Qt::transparent);
	}
	if(size)
		*size = image.size();
	return image;
}
