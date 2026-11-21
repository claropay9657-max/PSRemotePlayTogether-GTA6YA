// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "scenemusic.h"
#include "livescenes.h"
#include "livestream.h"
#include "sounddecoder.h"

#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QRandomGenerator>

#include <algorithm>
#include <vector>

Q_DECLARE_LOGGING_CATEGORY(chiakiGui)

namespace {

/** How far the music keeps ahead of the stream, and how often it looks. */
constexpr size_t kAhead = sound::kRate / 4;
constexpr int kTickMs = 40;
/** A fade in or out takes this long (from nothing to full or back). */
constexpr double kFadeSeconds = 1.5;

} // namespace

SceneMusic::SceneMusic(LiveScenes *scenes, QObject *parent)
	: QObject(parent), scenes(scenes)
{
	timer.setInterval(kTickMs);
	connect(&timer, &QTimer::timeout, this, &SceneMusic::Tick);
	timer.start();
}

SceneMusic::~SceneMusic() = default;

bool SceneMusic::HasMusic(const QString &scene)
{
	return scene == QLatin1String("starting") || scene == QLatin1String("brb") || scene == QLatin1String("ending");
}

double SceneMusic::Wanted(QString *scene) const
{
	scene->clear();
	if(!scenes)
		return 0.0;
	const QString shown = scenes->shown();
	if(!HasMusic(shown) || !scenes->Value(QStringLiteral("music_%1_on").arg(shown)).toBool())
		return 0.0;
	*scene = shown;
	// Loudness follows the ear: half way is about a quarter, as for the alert sounds
	const double v = std::clamp(scenes->Value(QStringLiteral("music_%1_volume").arg(shown)).toInt(), 0, 100) / 100.0;
	return v * v;
}

bool SceneMusic::OpenBuiltIn()
{
	own = false;
	QFile file(QStringLiteral(":/sounds/music-%1.flac").arg(playing));
	if(!file.open(QIODevice::ReadOnly))
		return false;
	builtin_file = file.readAll();
	stream = std::make_unique<sound::Stream>(builtin_file);
	QString error;
	if(!stream->Open(&error))
	{
		qCWarning(chiakiGui).noquote() << "Scene music: the app's own track for" << playing << "cannot be played:" << error;
		stream.reset();
		return false;
	}
	return true;
}

bool SceneMusic::OpenNextOwn()
{
	for(int tries = 0; tries < order.size(); tries++)
	{
		order_at++;
		if(order_at >= order.size())
		{
			// Another round; shuffled anew if so wanted
			order_at = 0;
			if(scenes && scenes->Value(QStringLiteral("music_%1_shuffle").arg(playing)).toBool() && order.size() > 1)
				std::shuffle(order.begin(), order.end(), *QRandomGenerator::global());
		}
		const QString path = order.at(order_at);
		auto next = std::make_unique<sound::Stream>(path);
		QString error;
		if(QFileInfo::exists(path) && next->Open(&error))
		{
			stream = std::move(next);
			return true;
		}
		qCWarning(chiakiGui).noquote() << "Scene music: cannot play" << QFileInfo(path).fileName() << (error.isEmpty() ? QStringLiteral("(missing)") : error);
		if(scenes)
			scenes->SetMusicNote(playing, tr("“%1” could not be played.").arg(QFileInfo(path).fileName()));
	}
	return false;
}

bool SceneMusic::Open(const QString &scene)
{
	stream.reset();
	order.clear();
	order_at = -1;
	own = scenes && scenes->Value(QStringLiteral("music_%1_source").arg(scene)).toString() == QLatin1String("own");
	if(own)
	{
		order = scenes->Value(QStringLiteral("music_%1_files").arg(scene)).toStringList();
		order.removeAll(QString());
		if(scenes->Value(QStringLiteral("music_%1_shuffle").arg(scene)).toBool() && order.size() > 1)
			std::shuffle(order.begin(), order.end(), *QRandomGenerator::global());
		if(OpenNextOwn())
			return true;
		// None of them: the app's own track, and the player is told (in the app, not on the stream)
		if(scenes && !order.isEmpty())
			scenes->SetMusicNote(scene, tr("None of your files could be played; the built-in track plays instead."));
	}
	return OpenBuiltIn();
}

void SceneMusic::Tick()
{
	live::Engine &engine = live::Engine::Get();
	if(!engine.Running())
	{
		stream.reset();
		playing.clear();
		gain = 0.0;
		return;
	}

	QString wanted_scene;
	double target = Wanted(&wanted_scene);
	if(wanted_scene != playing)
	{
		if(!playing.isEmpty() && gain > 0.0)
		{
			// The old music fades out first
			target = 0.0;
		}
		else
		{
			playing = wanted_scene;
			stream.reset();
			if(!playing.isEmpty())
				Open(playing);
		}
	}
	if(!stream && gain <= 0.0)
		return;

	const size_t queued = engine.MusicQueued();
	if(queued >= kAhead)
		return;
	const size_t need = kAhead - queued + static_cast<size_t>(sound::kRate) * kTickMs / 1000;
	std::vector<int16_t> buf(need * 2, 0);
	size_t got = 0;
	int ends = 0;
	while(stream && got < need)
	{
		const size_t n = stream->Read(buf.data() + got * 2, need - got);
		got += n;
		if(n > 0)
			continue;
		// The end of a file: the same once more (seamless for FLAC and WAV), or the next one
		if(++ends > 2)
		{
			stream.reset();
			break;
		}
		bool going_on = false;
		if(own && order.size() > 1)
			going_on = OpenNextOwn();
		else
			going_on = stream->Rewind();
		if(!going_on)
			stream.reset();
	}

	// The level, moving to where it should be: a fade
	const double step = 1.0 / (kFadeSeconds * sound::kRate);
	for(size_t i = 0; i < need; i++)
	{
		if(gain < target)
			gain = std::min(target, gain + step);
		else if(gain > target)
			gain = std::max(target, gain - step);
		buf[i * 2] = static_cast<int16_t>(buf[i * 2] * gain);
		buf[i * 2 + 1] = static_cast<int16_t>(buf[i * 2 + 1] * gain);
	}
	engine.PushMusic(buf.data(), need);
}
