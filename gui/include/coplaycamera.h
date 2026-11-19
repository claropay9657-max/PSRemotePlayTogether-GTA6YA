// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_COPLAYCAMERA_H
#define CHIAKI_COPLAYCAMERA_H

#include <QByteArray>
#include <QElapsedTimer>
#include <QImage>
#include <QObject>
#include <QQuickImageProvider>
#include <QStringList>
#include <QTimer>

class QCamera;
class QMediaCaptureSession;
class QVideoSink;
class QVideoFrame;

/**
 * A player's webcam for a shared session: small JPEG pictures at a low rate,
 * ready to be sent to the others.
 */
class CoPlayCamera : public QObject
{
	Q_OBJECT

	public:
		explicit CoPlayCamera(QObject *parent = nullptr);
		~CoPlayCamera();

		/** False if the app was built without Qt Multimedia: webcams of others still show. */
		static bool Supported();
		/** Names of the cameras that can be picked. */
		static QStringList Devices();
		/** Entry of Devices() that produces a moving test picture instead of using a camera. */
		static QString TestDevice() { return QStringLiteral("Test picture"); }

		/**
		 * @param device a name from Devices(); empty for the system's default camera
		 * @param quality 0 = small, 1 = medium, 2 = large
		 */
		void Start(const QString &device, int quality);
		void Stop();
		bool Active() const { return active; }

	signals:
		/** A new picture: as it is sent, and decoded for showing it here. */
		void Frame(const QByteArray &jpeg, const QImage &image);
		void Failed(const QString &message);
		void ActiveChanged();

	private:
		void StartCamera();
		void OnVideoFrame(const QVideoFrame &frame);
		void TestTick();
		void Process(const QImage &source);
		void SetActive(bool on);

		QString device;
		int height = 180;
		int interval_ms = 100;
		int jpeg_quality = 55;
		bool active = false;
		bool wanted = false;
		QElapsedTimer since_last;
		QTimer test_timer;
		int test_frame = 0;

		QCamera *camera = nullptr;
		QMediaCaptureSession *capture = nullptr;
		QVideoSink *sink = nullptr;
};

/**
 * The latest picture of every player's webcam, for the QML side:
 * "image://coplaycam/<slot>/<anything that changes with each picture>".
 */
class CoPlayCamProvider : public QQuickImageProvider
{
	public:
		CoPlayCamProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}
		QImage requestImage(const QString &id, QSize *size, const QSize &requested_size) override;

		static void Set(int slot, const QImage &image);
		static void Clear(int slot);
		static void ClearAll();
};

#endif // CHIAKI_COPLAYCAMERA_H
