// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Renders a QML scene that is a pure function of its `t` property (seconds), such as the
// intro film, without a window and with one scene graph that stays (QQuickRenderControl, as
// the app itself draws its QML): single frames, a contact sheet of several moments, or raw
// frames for ffmpeg. See README.md next to this file.
//
//   filmshot <scene.qml> <w> <h> shot <out.png> <t> [name=value ...]
//   filmshot <scene.qml> <w> <h> sheet <out.png> <cols> <t1> <t2> ... [name=value ...]
//   filmshot <scene.qml> <w> <h> raw <fps> <t0> <t1> [name=value ...]   (RGBA frames to stdout)
//
// <w> x <h> are logical; pictures come out at twice that (as on a HiDPI screen) unless
// FILMSHOT_DPR says otherwise. name=value sets a property of the scene. Every *.rcc next to
// the binary is registered (the app's icons, fonts, sprites).
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickRenderControl>
#include <QQuickRenderTarget>
#include <QQuickWindow>
#include <QQuickGraphicsDevice>
#include <QResource>
#include <rhi/qrhi.h>

#include <cstdio>
#include <memory>

struct Stage
{
	QQuickRenderControl control;
	std::unique_ptr<QQuickWindow> window;
	QQmlEngine engine;
	QQuickItem *root = nullptr;
	QRhi *rhi = nullptr;
	std::unique_ptr<QRhiTexture> texture;
	std::unique_ptr<QRhiRenderBuffer> depth;
	std::unique_ptr<QRhiTextureRenderTarget> target;
	std::unique_ptr<QRhiRenderPassDescriptor> pass;
	QSize pixels;

	bool Open(const QString &file, int w, int h, qreal dpr)
	{
		window = std::make_unique<QQuickWindow>(&control);
		window->setColor(Qt::black);
		if(!engine.incubationController())
			engine.setIncubationController(window->incubationController());
		QQmlComponent component(&engine, QUrl::fromLocalFile(file));
		QObject *object = component.create();
		if(!object)
		{
			for(const auto &e : component.errors())
				fprintf(stderr, "%s\n", qPrintable(e.toString()));
			return false;
		}
		root = qobject_cast<QQuickItem *>(object);
		if(!root)
			return false;
		root->setParentItem(window->contentItem());
		root->setSize(QSizeF(w, h));
		window->contentItem()->setSize(QSizeF(w, h));
		window->setGeometry(0, 0, w, h);
		if(!control.initialize())
		{
			fprintf(stderr, "the scene graph could not be set up\n");
			return false;
		}
		rhi = control.rhi();
		pixels = QSize(qRound(w * dpr), qRound(h * dpr));
		texture.reset(rhi->newTexture(QRhiTexture::RGBA8, pixels, 1, QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource));
		if(!texture->create())
			return false;
		depth.reset(rhi->newRenderBuffer(QRhiRenderBuffer::DepthStencil, pixels, 1));
		if(!depth->create())
			return false;
		QRhiTextureRenderTargetDescription desc((QRhiColorAttachment(texture.get())));
		desc.setDepthStencilBuffer(depth.get());
		target.reset(rhi->newTextureRenderTarget(desc));
		pass.reset(target->newCompatibleRenderPassDescriptor());
		target->setRenderPassDescriptor(pass.get());
		if(!target->create())
			return false;
		QQuickRenderTarget rt = QQuickRenderTarget::fromRhiRenderTarget(target.get());
		rt.setDevicePixelRatio(dpr);
		window->setRenderTarget(rt);
		return true;
	}

	QImage Grab(double t)
	{
		root->setProperty("t", t);
		QCoreApplication::processEvents();
		QCoreApplication::sendPostedEvents();
		control.polishItems();
		control.beginFrame();
		control.sync();
		control.render();
		QRhiReadbackResult result;
		bool done = false;
		QImage image;
		result.completed = [&]() {
			QImage wrapped(reinterpret_cast<const uchar *>(result.data.constData()), result.pixelSize.width(), result.pixelSize.height(),
				QImage::Format_RGBA8888_Premultiplied);
			image = rhi->isYUpInFramebuffer() ? wrapped.mirrored() : wrapped.copy();
			done = true;
		};
		QRhiResourceUpdateBatch *batch = rhi->nextResourceUpdateBatch();
		batch->readBackTexture(QRhiReadbackDescription(texture.get()), &result);
		control.commandBuffer()->resourceUpdate(batch);
		control.endFrame();
		if(!done)
			rhi->finish();
		return image;
	}
};

int main(int argc, char **argv)
{
	qputenv("QT_MAC_DISABLE_FOREGROUND_APPLICATION_TRANSFORM", "1");
	QGuiApplication app(argc, argv);
	// name=value anywhere after the mode: properties of the scene, not times
	QStringList args;
	QList<QPair<QByteArray, QString>> props;
	const QStringList all = app.arguments();
	for(int i = 0; i < all.size(); i++)
	{
		const int eq = all[i].indexOf('=');
		if(i > 4 && eq > 0 && all[i][0].isLetter())
			props.append({ all[i].left(eq).toUtf8(), all[i].mid(eq + 1) });
		else
			args.append(all[i]);
	}
	if(args.size() < 6)
	{
		fprintf(stderr, "usage: see the top of main.cpp\n");
		return 2;
	}
	const QDir here(QCoreApplication::applicationDirPath());
	for(const QString &rcc : here.entryList({ "*.rcc" }))
		QResource::registerResource(here.filePath(rcc));

	const int w = args[2].toInt(), h = args[3].toInt();
	const qreal dpr = qEnvironmentVariableIsSet("FILMSHOT_DPR") ? qEnvironmentVariable("FILMSHOT_DPR").toDouble() : 2.0;
	Stage stage;
	if(!stage.Open(args[1], w, h, dpr))
		return 1;
	for(const auto &prop : props)
		stage.root->setProperty(prop.first.constData(), prop.second);
	const QString mode = args[4];
	QElapsedTimer timer;
	timer.start();
	if(mode == "shot")
	{
		stage.Grab(args[6].toDouble()); // twice: what is loaded in the first frame shows in the second
		QImage img = stage.Grab(args[6].toDouble());
		if(img.isNull() || !img.save(args[5]))
		{
			fprintf(stderr, "no picture\n");
			return 1;
		}
	}
	else if(mode == "sheet")
	{
		const int cols = args[6].toInt();
		QList<double> times;
		for(int i = 7; i < args.size(); i++)
			times.append(args[i].toDouble());
		const int rows = (times.size() + cols - 1) / cols;
		const int gap = 8, label = 26;
		const int pw = stage.pixels.width(), ph = stage.pixels.height();
		QImage sheet(cols * (pw + gap) + gap, rows * (ph + label + gap) + gap, QImage::Format_RGB32);
		sheet.fill(QColor(40, 40, 46));
		QPainter p(&sheet);
		QFont f = p.font();
		f.setPixelSize(18);
		p.setFont(f);
		p.setPen(Qt::white);
		stage.Grab(times.isEmpty() ? 0 : times[0]);
		for(int i = 0; i < times.size(); i++)
		{
			QImage img = stage.Grab(times[i]);
			const int x = gap + (i % cols) * (pw + gap), y = gap + (i / cols) * (ph + label + gap);
			p.drawText(x + 2, y + 19, QString("t = %1").arg(times[i], 0, 'f', 2));
			p.drawImage(QRect(x, y + label, pw, ph), img);
		}
		p.end();
		if(!sheet.save(args[5]))
			return 1;
	}
	else if(mode == "raw")
	{
		const double fps = args[5].toDouble(), t0 = args[6].toDouble(), t1 = args[7].toDouble();
		const int frames = static_cast<int>((t1 - t0) * fps + 0.5);
		const int pw = stage.pixels.width(), ph = stage.pixels.height();
		stage.Grab(t0);
		for(int i = 0; i < frames; i++)
		{
			QImage img = stage.Grab(t0 + i / fps).convertToFormat(QImage::Format_RGBA8888);
			for(int y = 0; y < ph; y++)
				fwrite(img.constScanLine(y), 1, static_cast<size_t>(pw) * 4, stdout);
			if(i % 120 == 0)
				fprintf(stderr, "frame %d of %d (%.1f s)\n", i, frames, timer.elapsed() / 1000.0);
		}
	}
	fprintf(stderr, "%s done in %.2f s\n", qPrintable(mode), timer.elapsed() / 1000.0);
	return 0;
}
