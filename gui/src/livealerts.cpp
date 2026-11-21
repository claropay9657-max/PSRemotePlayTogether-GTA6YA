// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "livealerts.h"
#include "livescenes.h"
#include "livestream.h"
#include "settings.h"

#include <QApplication>
#include <QDateTime>
#include <QFileDialog>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocale>
#include <QRegularExpression>
#include <QUrl>

#ifdef CHIAKI_HAVE_MULTIMEDIA
#include <QAudioFormat>
#include <QAudioSink>
#include <QBuffer>
#include <QMediaDevices>
#endif

#include "sounddecoder.h"

#include <QFileInfo>
#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(chiakiGui)

#include <algorithm>
#include <cstring>

namespace {

struct Kind
{
	const char *id;
	/** Its colour, a second one for gradients, and the light one of its label (the designs' "All alerts"). */
	const char *color;
	const char *color2;
	const char *ink;
	/** Its emblem (res/twitch/<icon>.svg) and how the card shows who it is about. */
	const char *icon;
	/** "initial": the viewer's initial with the emblem as a badge; "emblem": the emblem large; "level": a hype train's level. */
	const char *face;
	/** The large emblem of its own card (the design "All alerts"), and the two colours of the card's bar. */
	const char *emblem;
	const char *bar1;
	const char *bar2;
	int seconds;
	/** The setting of its minimum, or nullptr; and the default. */
	const char *min_key;
	int min_default;
	/** Which of the app's sounds (res/sounds/<sound>.flac). */
	const char *sound;
};

const Kind kKinds[] = {
	{ "follow", "#00f2ff", "#8c52ff", "#8ff7ff", "heart", "initial", "follow-big", "#00f2ff", "#00f2ff", 6, nullptr, 0, "follow" },
	{ "sub", "#a77bff", "#f0469b", "#d8c6ff", "star-badge", "initial", "sub-big", "#c4a6ff", "#8c52ff", 8, nullptr, 0, "subscribe" },
	{ "resub", "#a77bff", "#f0469b", "#d8c6ff", "star-badge", "initial", "resub-big", "#c4a6ff", "#8c52ff", 9, nullptr, 0, "resub" },
	{ "gift", "#f0469b", "#a77bff", "#ffc3dd", "gift", "initial", "gift-big", "#f0469b", "#f0469b", 8, "gift_min", 1, "giftsubs" },
	{ "bits", "#f5b942", "#f0469b", "#fde4a8", "gem", "initial", "gem-big", "#f5b942", "#f5b942", 8, "bits_min", 100, "bits" },
	{ "raid", "#34d399", "#00f2ff", "#c6f6e2", "raid", "initial", "raid-big", "#34d399", "#34d399", 9, "raid_min", 2, "raid" },
	{ "points", "#8c52ff", "#f0469b", "#cdb6ff", "points", "initial", "points-big", "#8c52ff", "#f0469b", 6, nullptr, 0, "channelpoints" },
	{ "hype", "#f0469b", "#f5b942", "#ffc3dd", "hype", "level", "", "#f0469b", "#f5b942", 8, nullptr, 0, "hypetrain" },
	{ "poll", "#60a5fa", "#a9b0ff", "#cfe3ff", "poll", "emblem", "poll", "#60a5fa", "#60a5fa", 7, nullptr, 0, "poll" },
	{ "prediction", "#818cf8", "#f0469b", "#d6dbff", "prediction", "emblem", "prediction", "#3b82f6", "#f0469b", 7, nullptr, 0, "prediction" },
	{ "goal", "#00f2ff", "#34d399", "#8ff7ff", "target", "emblem", "target", "#8ff7ff", "#00f2ff", 7, nullptr, 0, "goal" },
	{ "charity", "#fb7185", "#f0469b", "#ffd1d9", "charity", "initial", "charity", "#fda4af", "#fb7185", 8, "charity_min", 1, "charity" },
	{ "shoutout", "#00f2ff", "#8c52ff", "#8ff7ff", "shoutout", "initial", "shoutout", "#00f2ff", "#00f2ff", 7, nullptr, 0, "shoutout" },
	{ "ad", "#fbbf24", "#f5b942", "#fde68a", "ad", "emblem", "ad", "#fbbf24", "#fbbf24", 6, nullptr, 0, "adbreak" },
};

const Kind *FindKind(const QString &id)
{
	for(const Kind &k : kKinds)
	{
		if(id == QLatin1String(k.id))
			return &k;
	}
	return nullptr;
}

qint64 NowMs()
{
	return QDateTime::currentMSecsSinceEpoch();
}

QString Tier(const QString &tier)
{
	if(tier == QLatin1String("2000"))
		return QObject::tr("Tier 2");
	if(tier == QLatin1String("3000"))
		return QObject::tr("Tier 3");
	return QObject::tr("Tier 1");
}

/** A level 0..100 as a factor: loudness follows the ear, so half way is about a quarter (-12 dB). */
double Gain(int level)
{
	const double v = std::clamp(level, 0, 100) / 100.0;
	return v * v;
}

#ifdef CHIAKI_HAVE_MULTIMEDIA
/** Plays sounds on this computer, each on its own output until it is over. */
class LocalSounds : public QObject
{
	public:
		using QObject::QObject;

		void Play(const std::vector<int16_t> &pcm, int volume)
		{
			if(pcm.empty() || volume <= 0)
				return;
			QAudioFormat format;
			format.setSampleRate(sound::kRate);
			format.setChannelCount(2);
			format.setSampleFormat(QAudioFormat::Int16);
			const QAudioDevice device = QMediaDevices::defaultAudioOutput();
			if(device.isNull() || !device.isFormatSupported(format))
				return;
			QByteArray bytes(reinterpret_cast<const char *>(pcm.data()), static_cast<qsizetype>(pcm.size() * sizeof(int16_t)));
			auto *buffer = new QBuffer(this);
			buffer->setData(bytes);
			buffer->open(QIODevice::ReadOnly);
			auto *sink = new QAudioSink(device, format, this);
			sink->setVolume(Gain(volume));
			connect(sink, &QAudioSink::stateChanged, this, [sink, buffer](QAudio::State state) {
				if(state == QAudio::IdleState || state == QAudio::StoppedState)
				{
					sink->disconnect();
					sink->stop();
					sink->deleteLater();
					buffer->deleteLater();
				}
			});
			sink->start(buffer);
		}
};
#endif

} // namespace

LiveAlerts::LiveAlerts(Settings *settings, LiveScenes *scenes, QObject *parent)
	: QObject(parent), settings(settings), scenes(scenes)
{
	auto load = [this, settings](const QString &key, const QVariant &def) {
		QVariant v = settings->GetValue(QStringLiteral("alerts/%1").arg(key), def);
		if(def.typeId() == QMetaType::Bool)
			v = v.toBool();
		else if(def.typeId() == QMetaType::Int)
			v = v.toInt();
		else
			v = v.toString();
		map.insert(key, v);
	};
	for(const Kind &k : kKinds)
	{
		const QString id = QString::fromLatin1(k.id);
		// Ad breaks: only for those who want to warn their viewers. Polls, predictions and goals
		// have elements of their own on the game picture (twitchelements.h): no card at first
		static const QStringList quiet = { "ad", "poll", "prediction", "goal" };
		load(id + QStringLiteral("_on"), !quiet.contains(id));
		load(id + QStringLiteral("_sound"), QStringLiteral("app"));
		load(id + QStringLiteral("_volume"), 80);
		load(id + QStringLiteral("_seconds"), k.seconds);
		if(k.min_key)
			load(QString::fromLatin1(k.min_key), k.min_default);
	}
	// Also on the streamer's own screen (they see it in their stream anyway)
	load(QStringLiteral("on_screen"), false);
	// All alert sounds: one level on top of each kind's own, and a switch
	load(QStringLiteral("sound_on"), true);
	load(QStringLiteral("sound_volume"), 80);
	// A sound for new chat messages (the room's and Twitch's): here, and only if wanted on the stream
	load(QStringLiteral("chat_sound"), true);
	load(QStringLiteral("chat_volume"), 60);
	load(QStringLiteral("chat_stream"), false);
	// Wait with alerts while Be right back or Technical difficulties is up
	load(QStringLiteral("hold_scenes"), true);
	// Words hidden in names and messages, separated by commas
	load(QStringLiteral("blocked"), QString());
	connect(&map, &QQmlPropertyMap::valueChanged, this, [this](const QString &key, const QVariant &value) { Store(key, value); });

	timer.setInterval(50);
	connect(&timer, &QTimer::timeout, this, &LiveAlerts::Tick);

#ifdef CHIAKI_HAVE_MULTIMEDIA
	local = new LocalSounds(this);
#endif
	UpdatePage();
}

LiveAlerts::~LiveAlerts() = default;

QVariantList LiveAlerts::kinds() const
{
	QVariantList list;
	auto add = [&list](const char *id, const QString &name, const QString &what, const QString &min_name) {
		QVariantMap m;
		m["id"] = QString::fromLatin1(id);
		m["name"] = name;
		m["what"] = what;
		const Kind *k = FindKind(m["id"].toString());
		m["hasMin"] = k && k->min_key;
		m["minKey"] = k && k->min_key ? QString::fromLatin1(k->min_key) : QString();
		m["minName"] = min_name;
		list.append(m);
	};
	add("follow", tr("Follows"), tr("Somebody follows your channel."), QString());
	add("sub", tr("Subscriptions"), tr("A new subscriber, or a gifted sub arrives."), QString());
	add("resub", tr("Resubs"), tr("A subscriber shares how many months, with their message."), QString());
	add("gift", tr("Gift subs"), tr("Somebody gifts subs to your community."), tr("From this many subs"));
	add("bits", tr("Bits"), tr("Somebody cheers with bits, with their message."), tr("From this many bits"));
	add("raid", tr("Raids"), tr("Another channel raids yours."), tr("From this many viewers"));
	add("points", tr("Channel points"), tr("Somebody redeems a reward of your channel."), QString());
	add("hype", tr("Hype train"), tr("A hype train starts, reaches a new level or ends."), QString());
	add("poll", tr("Polls"), tr("A poll starts or ends."), QString());
	add("prediction", tr("Predictions"), tr("A prediction starts or ends."), QString());
	add("goal", tr("Goals"), tr("A goal starts or is reached."), QString());
	add("charity", tr("Charity"), tr("Somebody donates to your charity campaign."), tr("From this much"));
	add("shoutout", tr("Shoutouts"), tr("Another channel gives you a shoutout."), QString());
	add("ad", tr("Ad breaks"), tr("An ad break starts: tells your viewers you are back right after."), QString());
	return list;
}

void LiveAlerts::set(const QString &key, const QVariant &value)
{
	if(map.value(key) == value)
		return;
	map.insert(key, value);
	Store(key, value);
}

void LiveAlerts::Store(const QString &key, const QVariant &value)
{
	settings->SetValue(QStringLiteral("alerts/%1").arg(key), value);
	emit valuesChanged();
}

void LiveAlerts::chooseSoundFile(const QString &kind)
{
	const QString file = QFileDialog::getOpenFileName(QApplication::focusWidget(), tr("A sound for this alert"), QString(),
		tr("Sounds (*.wav *.flac *.mp3 *.ogg *.opus)"), nullptr, QFileDialog::DontUseNativeDialog);
	if(!file.isEmpty())
		setSound(kind, file);
}

void LiveAlerts::setSound(const QString &kind, const QString &sound)
{
	set(kind + QStringLiteral("_sound"), sound.isEmpty() ? QStringLiteral("app") : sound);
	sound_cache.remove(kind);
	preview(kind);
}

void LiveAlerts::setPaused(bool paused)
{
	if(is_paused == paused)
		return;
	is_paused = paused;
	emit currentChanged();
	if(!paused)
		Next();
}

// ------------------------------------------------------------------ what viewers write

QString LiveAlerts::Clean(const QString &text, int max) const
{
	QString out;
	out.reserve(text.size());
	for(const QChar c : text)
	{
		// No control characters, no line breaks: one line of plain text
		if(c.category() == QChar::Other_Control || c.category() == QChar::Other_Format)
			out += QLatin1Char(' ');
		else
			out += c;
	}
	out = out.simplified();
	const QStringList blocked = map.value("blocked").toString().split(QLatin1Char(','), Qt::SkipEmptyParts);
	for(QString word : blocked)
	{
		word = word.trimmed();
		if(word.isEmpty())
			continue;
		const QRegularExpression re(QRegularExpression::escape(word), QRegularExpression::CaseInsensitiveOption);
		out.replace(re, QString(word.size(), QLatin1Char('*')));
	}
	if(out.size() > max)
		out = out.left(max - 1).trimmed() + QChar(0x2026);
	return out;
}

// ------------------------------------------------------------------ from Twitch

void LiveAlerts::OnTwitchEvent(const QString &type, const QJsonObject &e)
{
	if(type == QLatin1String("channel.chat.message"))
	{
		// Not for what the streamer writes themselves
		if(e.value("chatter_user_id").toString() != e.value("broadcaster_user_id").toString())
			OnChatMessage();
		return;
	}
	auto name = [this, &e](const char *key = "user_name") {
		if(e.value("is_anonymous").toBool())
			return tr("Anonymous");
		QString n = e.value(QLatin1String(key)).toString();
		if(n.isEmpty())
			n = e.value(QStringLiteral("user_login")).toString();
		return Clean(n.isEmpty() ? tr("Somebody") : n, 25);
	};
	QVariantMap d;
	QString kind;
	if(type == QLatin1String("channel.follow"))
	{
		kind = QStringLiteral("follow");
		d["label"] = tr("New follower");
		d["name"] = name();
		d["detail"] = tr("Welcome to the room!");
	}
	else if(type == QLatin1String("channel.subscribe"))
	{
		// A gifted sub comes with its gift alert; this one is for the one who subscribes
		if(e.value("is_gift").toBool())
			return;
		kind = QStringLiteral("sub");
		d["label"] = tr("Subscribed");
		d["chip"] = Tier(e.value("tier").toString());
		d["setLabel"] = tr("Subscribed · %1").arg(d["chip"].toString());
		d["name"] = name();
		d["detail"] = tr("First month – welcome aboard");
		d["count"] = 1;
	}
	else if(type == QLatin1String("channel.subscription.message"))
	{
		kind = QStringLiteral("resub");
		const int months = e.value("cumulative_months").toInt(1);
		const int streak = e.value("streak_months").toInt(0);
		d["label"] = tr("Resub · %1 months").arg(months);
		d["chip"] = Tier(e.value("tier").toString());
		d["name"] = name();
		d["detail"] = streak > 1 ? tr("%1 months in a row").arg(streak) : tr("%1 months").arg(months);
		d["bars"] = std::clamp(streak > 1 ? streak : months, 0, 12);
		d["count"] = months;
		d["message"] = Clean(e.value("message").toObject().value("text").toString(), 200);
	}
	else if(type == QLatin1String("channel.subscription.gift"))
	{
		kind = QStringLiteral("gift");
		const int total = e.value("total").toInt(1);
		if(total < map.value("gift_min").toInt())
			return;
		d["label"] = tr("Gifted subs");
		d["chip"] = Tier(e.value("tier").toString());
		d["name"] = name();
		const int cumulative = e.value("cumulative_total").toInt(0);
		d["detail"] = cumulative > total ? tr("%1 subs to the community · %2 in total").arg(total).arg(cumulative)
			: (total == 1 ? tr("1 sub to the community") : tr("%1 subs to the community").arg(total));
		d["count"] = total;
	}
	else if(type == QLatin1String("channel.cheer"))
	{
		kind = QStringLiteral("bits");
		const int bits = e.value("bits").toInt();
		if(bits < map.value("bits_min").toInt())
			return;
		d["label"] = tr("Cheer");
		d["big"] = QLocale(QLocale::English).toString(bits);
		d["bigUnit"] = bits == 1 ? tr("bit") : tr("bits");
		d["name"] = name();
		d["detail"] = tr("from %1").arg(d["name"].toString());
		d["message"] = Clean(e.value("message").toString(), 200);
		d["messageTint"] = true;
	}
	else if(type == QLatin1String("channel.raid"))
	{
		kind = QStringLiteral("raid");
		const int viewers = e.value("viewers").toInt();
		if(viewers < map.value("raid_min").toInt())
			return;
		d["label"] = tr("Raid incoming");
		d["name"] = name("from_broadcaster_user_name");
		d["stat"] = QLocale(QLocale::English).toString(viewers);
		d["statUnit"] = viewers == 1 ? tr("viewer coming in") : tr("viewers coming in");
	}
	else if(type == QLatin1String("channel.channel_points_custom_reward_redemption.add")
		|| type == QLatin1String("channel.channel_points_automatic_reward_redemption.add"))
	{
		kind = QStringLiteral("points");
		const QJsonObject reward = e.value("reward").toObject();
		const int cost = reward.value("cost").toInt(0);
		d["label"] = cost > 0 ? tr("Channel points · %1").arg(cost) : tr("Channel points");
		QString title = reward.value("title").toString();
		if(title.isEmpty())
			title = reward.value("type").toString().replace(QLatin1Char('_'), QLatin1Char(' '));
		d["title"] = Clean(title, 40);
		d["titleGradient"] = true;
		d["name"] = name();
		d["detail"] = tr("%1 redeemed it").arg(d["name"].toString());
		QString input = e.value("user_input").toString();
		if(input.isEmpty())
			input = e.value("message").toObject().value("text").toString();
		d["message"] = Clean(input, 200);
	}
	else if(type.startsWith(QLatin1String("channel.hype_train.")))
	{
		kind = QStringLiteral("hype");
		const int level = e.value("level").toInt(1);
		const bool end = type.endsWith(QLatin1String(".end"));
		const bool begin = type.endsWith(QLatin1String(".begin"));
		// Progress only when it reaches a new level
		if(!begin && !end && level <= hype_level)
			return;
		hype_level = end ? 0 : level;
		const int progress = e.value("progress").toInt();
		const int goal = std::max(1, e.value("goal").toInt(1));
		d["label"] = end ? tr("Hype train done") : tr("Hype train");
		d["level"] = level;
		// Five steps: the levels reached full, the next one as far as it got
		d["segments"] = end ? 1.0 : std::clamp(static_cast<double>(progress) / goal, 0.0, 1.0);
		d["title"] = end ? tr("Level %1 reached!").arg(level) : (begin ? tr("All aboard!") : tr("Level %1!").arg(level));
		d["detail"] = end ? tr("Thank you all!") : tr("%1 % to level %2").arg(qRound(100.0 * progress / goal)).arg(level + 1);
		QStringList top;
		for(const QJsonValue &c : e.value("top_contributions").toArray())
			top << Clean(c.toObject().value("user_name").toString(), 25);
		top.removeAll(QString());
		if(!top.isEmpty())
			d["note"] = tr("Top: %1").arg(top.mid(0, 3).join(QStringLiteral(", ")));
	}
	else if(type == QLatin1String("channel.poll.begin") || type == QLatin1String("channel.poll.end"))
	{
		kind = QStringLiteral("poll");
		const bool end = type.endsWith(QLatin1String(".end"));
		d["label"] = end ? tr("Poll closed") : tr("New poll");
		d["title"] = Clean(e.value("title").toString(), 60);
		QStringList choices;
		QString winner;
		int best = -1;
		for(const QJsonValue &c : e.value("choices").toArray())
		{
			const QJsonObject o = c.toObject();
			choices << Clean(o.value("title").toString(), 25);
			if(o.value("votes").toInt() > best)
			{
				best = o.value("votes").toInt();
				winner = Clean(o.value("title").toString(), 25);
			}
		}
		d["detail"] = end ? tr("Winner: %1").arg(winner) : choices.join(QStringLiteral(" · "));
	}
	else if(type == QLatin1String("channel.prediction.begin") || type == QLatin1String("channel.prediction.end"))
	{
		kind = QStringLiteral("prediction");
		const bool end = type.endsWith(QLatin1String(".end"));
		d["label"] = end ? tr("Prediction decided") : tr("New prediction");
		d["title"] = Clean(e.value("title").toString(), 60);
		QStringList outcomes;
		QString won;
		const QString winning = e.value("winning_outcome_id").toString();
		for(const QJsonValue &c : e.value("outcomes").toArray())
		{
			const QJsonObject o = c.toObject();
			outcomes << Clean(o.value("title").toString(), 25);
			if(o.value("id").toString() == winning)
				won = Clean(o.value("title").toString(), 25);
		}
		d["detail"] = end ? (won.isEmpty() ? tr("Cancelled") : tr("It's “%1”").arg(won)) : outcomes.join(QStringLiteral(" or "));
	}
	else if(type == QLatin1String("channel.goal.begin") || type == QLatin1String("channel.goal.end"))
	{
		kind = QStringLiteral("goal");
		const bool end = type.endsWith(QLatin1String(".end"));
		const bool achieved = e.value("is_achieved").toBool();
		if(end && !achieved)
			return;
		const int current_amount = e.value("current_amount").toInt();
		const int target = std::max(1, e.value("target_amount").toInt(1));
		d["label"] = end ? tr("Goal reached") : tr("New goal");
		QString what = Clean(e.value("description").toString(), 60);
		if(what.isEmpty())
			what = e.value("type").toString().replace(QLatin1Char('_'), QLatin1Char(' '));
		d["title"] = what;
		d["detail"] = tr("%1 of %2").arg(current_amount).arg(target);
		d["progress"] = std::clamp(static_cast<double>(current_amount) / target, 0.0, 1.0);
	}
	else if(type == QLatin1String("channel.charity_campaign.donate"))
	{
		kind = QStringLiteral("charity");
		const QJsonObject amount = e.value("amount").toObject();
		const int places = amount.value("decimal_places").toInt(2);
		double value = amount.value("value").toDouble();
		for(int i = 0; i < places; i++)
			value /= 10.0;
		if(value < map.value("charity_min").toInt())
			return;
		d["label"] = tr("Charity donation");
		d["name"] = name();
		d["stat"] = QLocale(QLocale::English).toString(value, 'f', value == static_cast<int>(value) ? 0 : 2);
		d["statUnit"] = Clean(amount.value("currency").toString(), 3) + QLatin1Char(' ') + tr("for %1").arg(Clean(e.value("charity_name").toString(), 40));
	}
	else if(type == QLatin1String("channel.shoutout.receive"))
	{
		kind = QStringLiteral("shoutout");
		d["label"] = tr("Shoutout");
		d["name"] = name("from_broadcaster_user_name");
		const int viewers = e.value("viewer_count").toInt();
		d["detail"] = viewers > 0 ? tr("gave you a shoutout in front of %1 viewers").arg(viewers) : tr("gave you a shoutout");
	}
	else if(type == QLatin1String("channel.ad_break.begin"))
	{
		kind = QStringLiteral("ad");
		const int secs = e.value("duration_seconds").toInt();
		d["label"] = tr("Ad break");
		d["title"] = tr("Back right after");
		d["detail"] = secs > 0 ? tr("%1 s of ads – stay with us").arg(secs) : tr("Stay with us");
	}
	else
	{
		return;
	}
	Push(kind, d);
}

// ------------------------------------------------------------------ the queue

void LiveAlerts::test(const QString &kind)
{
	// What each alert looks like, without Twitch
	QJsonObject e;
	QString type;
	if(kind == QLatin1String("follow")) { type = "channel.follow"; e["user_name"] = "luna_plays"; }
	else if(kind == QLatin1String("sub")) { type = "channel.subscribe"; e["user_name"] = "pixelwolf"; e["tier"] = "1000"; }
	else if(kind == QLatin1String("resub"))
	{
		type = "channel.subscription.message"; e["user_name"] = "kirobyte"; e["tier"] = "1000";
		e["cumulative_months"] = 14; e["streak_months"] = 6;
		QJsonObject m; m["text"] = tr("Still the best co-op nights."); e["message"] = m;
	}
	else if(kind == QLatin1String("gift")) { type = "channel.subscription.gift"; e["user_name"] = "nightowl"; e["total"] = 5; e["cumulative_total"] = 25; e["tier"] = "1000"; }
	else if(kind == QLatin1String("bits")) { type = "channel.cheer"; e["user_name"] = "sunny_k"; e["bits"] = std::max(500, map.value("bits_min").toInt()); e["message"] = tr("Do the jump again!"); }
	else if(kind == QLatin1String("raid")) { type = "channel.raid"; e["from_broadcaster_user_name"] = "tobi_tv"; e["viewers"] = std::max(42, map.value("raid_min").toInt()); }
	else if(kind == QLatin1String("points"))
	{
		type = "channel.channel_points_custom_reward_redemption.add"; e["user_name"] = "mira";
		QJsonObject r; r["title"] = tr("Hydrate!"); r["cost"] = 500; e["reward"] = r;
	}
	else if(kind == QLatin1String("hype"))
	{
		hype_level = 0;
		type = "channel.hype_train.begin"; e["level"] = 3; e["progress"] = 720; e["goal"] = 1000;
		QJsonArray top; QJsonObject a; a["user_name"] = "nightowl"; top.append(a); QJsonObject b; b["user_name"] = "sunny_k"; top.append(b);
		e["top_contributions"] = top;
	}
	else if(kind == QLatin1String("poll"))
	{
		type = "channel.poll.begin"; e["title"] = tr("Which boss next?");
		QJsonArray c; for(const char *t : { "Warden", "Hydra", "Mimic" }) { QJsonObject o; o["title"] = QString::fromLatin1(t); c.append(o); }
		e["choices"] = c;
	}
	else if(kind == QLatin1String("prediction"))
	{
		type = "channel.prediction.begin"; e["title"] = tr("Do we beat it first try?");
		QJsonArray c; for(const char *t : { "Yes", "No" }) { QJsonObject o; o["title"] = QString::fromLatin1(t); c.append(o); }
		e["outcomes"] = c;
	}
	else if(kind == QLatin1String("goal")) { type = "channel.goal.begin"; e["description"] = tr("100 followers"); e["current_amount"] = 72; e["target_amount"] = 100; }
	else if(kind == QLatin1String("charity"))
	{
		type = "channel.charity_campaign.donate"; e["user_name"] = "kindheart"; e["charity_name"] = tr("Your charity");
		QJsonObject a; a["value"] = 1000; a["decimal_places"] = 2; a["currency"] = "EUR"; e["amount"] = a;
	}
	else if(kind == QLatin1String("shoutout")) { type = "channel.shoutout.receive"; e["from_broadcaster_user_name"] = "bigstreamer"; e["viewer_count"] = 840; }
	else if(kind == QLatin1String("ad")) { type = "channel.ad_break.begin"; e["duration_seconds"] = 90; }
	if(type.isEmpty())
		return;
	// A test shows even when the kind is switched off, and is heard here at the levels set now
	const QString on_key = kind + QStringLiteral("_on");
	const bool was_on = map.value(on_key).toBool();
	map.insert(on_key, true);
	test_next = true;
	OnTwitchEvent(type, e);
	test_next = false;
	map.insert(on_key, was_on);
}

void LiveAlerts::Push(const QString &kind, QVariantMap d)
{
	const Kind *k = FindKind(kind);
	if(!k || !map.value(kind + QStringLiteral("_on")).toBool())
		return;
	d["kind"] = kind;
	d["id"] = static_cast<qulonglong>(++serial);
	if(test_next)
		d["test"] = true;
	d["color"] = QString::fromLatin1(k->color);
	d["color2"] = QString::fromLatin1(k->color2);
	d["ink"] = QString::fromLatin1(k->ink);
	d["icon"] = QString::fromLatin1(k->icon);
	d["face"] = QString::fromLatin1(k->face);
	d["emblem"] = QString::fromLatin1(k->emblem);
	d["bar1"] = QString::fromLatin1(k->bar1);
	d["bar2"] = QString::fromLatin1(k->bar2);
	// Something to read (a resub's or a cheer's message, a reward's text): the wide card of
	// "Alert on stream"; everything else: the kind's own card of "All alerts"
	d["wide"] = !d.value("message").toString().isEmpty();
	if(!d.contains("setLabel"))
		d["setLabel"] = d.value("label");
	d["seconds"] = std::clamp(map.value(kind + QStringLiteral("_seconds")).toInt(), 2, 30);
	const QString who = d.value("name").toString();
	d["initial"] = who.isEmpty() ? QStringLiteral("?") : who.left(1).toUpper();
	// Never more than this many waiting: a raid of bots must not keep the stream busy for an hour
	if(queue.size() >= 50)
		queue.pop_front();
	queue.push_back({ d, kind });
	if(shown.isEmpty())
		Next();
	else
		emit currentChanged();
	UpdatePage();
}

bool LiveAlerts::Held() const
{
	if(is_paused)
		return true;
	if(!map.value("hold_scenes").toBool() || !scenes)
		return false;
	const QString scene = scenes->shown();
	return scene == QLatin1String("brb") || scene == QLatin1String("difficulties");
}

void LiveAlerts::Next()
{
	shown.clear();
	if(!queue.empty() && !Held())
	{
		Alert a = queue.front();
		queue.pop_front();
		shown = a.data;
		shown_since_ms = NowMs();
		shown_ms = shown.value("seconds").toInt() * 1000LL;
		PlaySound(a.kind, map.value("on_screen").toBool() || shown.value("test").toBool(), true);
	}
	if(!shown.isEmpty() || !queue.empty())
		timer.start();
	else
		timer.stop();
	emit currentChanged();
	emit leftChanged();
	UpdatePage();
}

void LiveAlerts::Tick()
{
	if(shown.isEmpty())
	{
		// Waiting for a scene to go, or for the pause to end
		if(!queue.empty() && !Held())
			Next();
		return;
	}
	if(NowMs() - shown_since_ms >= shown_ms)
		Next();
	else
		emit leftChanged();
}

QVariantList LiveAlerts::next() const
{
	QVariantList list;
	for(size_t i = 0; i < queue.size() && i < 3; i++)
	{
		const QVariantMap &d = queue[i].data;
		list.append(QVariantMap { { "initial", d.value("initial") }, { "color", d.value("color") }, { "icon", d.value("icon") },
			{ "face", d.value("face") } });
	}
	return list;
}

qreal LiveAlerts::left() const
{
	if(shown.isEmpty() || shown_ms <= 0)
		return 0.0;
	return std::clamp(1.0 - static_cast<double>(NowMs() - shown_since_ms) / shown_ms, 0.0, 1.0);
}

void LiveAlerts::skip()
{
	Next();
}

void LiveAlerts::clear()
{
	queue.clear();
	Next();
}

void LiveAlerts::UpdatePage()
{
	QJsonObject o;
	o["current"] = QJsonObject::fromVariantMap(shown);
	o["shownMs"] = shown.isEmpty() ? 0 : static_cast<qint64>(NowMs() - shown_since_ms);
	o["waiting"] = static_cast<int>(queue.size());
	o["next"] = QJsonArray::fromVariantList(next());
	const QString kind = shown.value("kind").toString();
	// The level of its sound (with the master level and mute), for the page to play it at
	o["volume"] = kind.isEmpty() ? 0 : Level(kind);
	// Chat sounds that go to the stream: the page plays each new one once
	o["chatSounds"] = chat_count;
	o["chatVolume"] = Level(QStringLiteral("chat"));
	page_json = QJsonDocument(o).toJson(QJsonDocument::Compact);
}

// ------------------------------------------------------------------ sound

QByteArray LiveAlerts::SoundFile(const QString &kind, QByteArray *type) const
{
	QString chosen = QStringLiteral("app");
	QString app_sound;
	if(kind == QLatin1String("chat"))
	{
		app_sound = QStringLiteral("chat");
	}
	else
	{
		const Kind *k = FindKind(kind);
		if(!k)
			return QByteArray();
		chosen = map.value(kind + QStringLiteral("_sound")).toString();
		if(chosen == QLatin1String("none"))
			return QByteArray();
		app_sound = QString::fromLatin1(k->sound);
	}
	if(chosen != QLatin1String("app"))
	{
		QFile own(chosen);
		if(own.open(QIODevice::ReadOnly) && own.size() < 16 * 1024 * 1024)
		{
			if(type)
			{
				const QString ext = QFileInfo(chosen).suffix().toLower();
				*type = ext == QLatin1String("wav") ? "audio/wav" : ext == QLatin1String("mp3") ? "audio/mpeg"
					: ext == QLatin1String("flac") ? "audio/flac" : "audio/ogg";
			}
			return own.readAll();
		}
	}
	QFile file(QStringLiteral(":/sounds/%1.flac").arg(app_sound));
	if(type)
		*type = "audio/flac";
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

int LiveAlerts::Level(const QString &kind) const
{
	if(!map.value("sound_on").toBool())
		return 0;
	const bool chat = kind == QLatin1String("chat");
	if(!chat && map.value(kind + QStringLiteral("_sound")).toString() == QLatin1String("none"))
		return 0;
	const int own = chat ? map.value("chat_volume").toInt() : map.value(kind + QStringLiteral("_volume")).toInt();
	return std::clamp(own, 0, 100) * std::clamp(map.value("sound_volume").toInt(), 0, 100) / 100;
}

void LiveAlerts::PlaySound(const QString &kind, bool here, bool stream)
{
	const int volume = Level(kind);
	if(volume <= 0)
		return;
	if(!sound_cache.contains(kind))
	{
		auto pcm = std::make_shared<std::vector<int16_t>>();
		QString error;
		if(!sound::DecodeAll(SoundFile(kind), pcm.get(), &error))
		{
			qCWarning(chiakiGui).noquote() << "Alerts: the sound of" << kind << "cannot be played:" << error;
			pcm->clear();
		}
		sound_cache.insert(kind, pcm);
	}
	const auto pcm = sound_cache.value(kind);
	if(!pcm || pcm->empty())
		return;

	const double gain = Gain(volume);

	// Into the live stream
	if(stream)
	{
		std::vector<int16_t> scaled(*pcm);
		for(int16_t &v : scaled)
			v = static_cast<int16_t>(v * gain);
		live::Engine::Get().PushEffects(scaled.data(), scaled.size() / 2, 2, sound::kRate);
	}

	// And on this computer
#ifdef CHIAKI_HAVE_MULTIMEDIA
	if(here && local)
		static_cast<LocalSounds *>(local)->Play(*pcm, volume);
#else
	Q_UNUSED(here);
#endif
}

void LiveAlerts::preview(const QString &kind)
{
	PlaySound(kind, true, false);
}

void LiveAlerts::OnRoomChat(const QVariantMap &message)
{
	Q_UNUSED(message);
	OnChatMessage();
}

void LiveAlerts::OnChatMessage()
{
	if(!map.value("chat_sound").toBool())
		return;
	// A busy chat is no drum roll
	const qint64 now = NowMs();
	if(now - chat_sound_ms < 1500)
		return;
	chat_sound_ms = now;
	const bool stream = map.value("chat_stream").toBool();
	PlaySound(QStringLiteral("chat"), true, stream);
	if(stream)
	{
		chat_count++;
		UpdatePage();
	}
}


