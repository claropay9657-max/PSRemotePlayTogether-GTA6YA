// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// What the Twitch elements show (TwitchElements::data), by key:
//   follower    {name, at}                       latest follower; at: when, ms since 1970
//   subscriber  {name, tier, months}
//   gift        {name, count, total}
//   bits        {name, bits}
//   raid        {name, viewers}
//   redemption  {name, reward, cost}
//   topcheer    {name, bits, avatar, stream}     stream: of this stream, else of all time
//   session     {followers, subs, bits}          this stream
//   viewers     number watching, -1 unknown
//   since       when the stream started, ms; 0 when not live
//   goal_follow, goal_sub  {current, target, text}
//   hype        {level, progress, goal, top, ends}
//   poll        {title, choices: [{title, votes}], total, ends, done}
//   prediction  {title, outcomes: [{title, points, users}], locks, locked, winner, done}
//   charity     {name, current, target}          amounts already written out: "€1,240"
//   adbreak     {next, until}                    the next ad, the end of the one running, ms
//   recent      [{kind, name, text}]             newest first
//   chat        [{id, uid, name, color, text, badges}]
// Live ones are missing while nothing runs.

#include "twitchelements.h"
#include "livealerts.h"
#include "livecontroller.h"
#include "settings.h"
#include "twitch.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocale>
#include <QRegularExpression>
#include <QTimeZone>
#include <QUrl>

#include <algorithm>

namespace {

struct Element
{
	const char *id;
	const char *group;
	const char *icon;
	bool show;
	double x;
	double y;
};

// First places: as on the design "Twitch elements on the game" (1920 x 1080, 32 from the edges)
const Element kElements[] = {
	{ "follower", "latest", "heart", true, 1.0, 0.076 },
	{ "subscriber", "latest", "star", true, 1.0, 0.165 },
	{ "gift", "latest", "gift", false, 0.0, 0.358 },
	{ "bits", "latest", "gem", true, 1.0, 0.254 },
	{ "raid", "latest", "raid", false, 0.0, 0.443 },
	{ "redemption", "latest", "points", false, 0.0, 0.528 },
	{ "topcheer", "counters", "crown", false, 0.5, 0.85 },
	{ "session", "counters", "star", false, 0.5, 0.74 },
	{ "viewers", "counters", "viewer", true, 1.0, 0.0 },
	{ "uptime", "counters", "clock", true, 1.0, 0.06 },
	{ "goal_follow", "live", "target", true, 0.5, 0.0 },
	{ "goal_sub", "live", "star", true, 0.5, 0.137 },
	{ "hype", "live", "hype", true, 0.0, 0.227 },
	{ "poll", "live", "poll", true, 1.0, 0.5 },
	{ "prediction", "live", "prediction", true, 1.0, 0.9 },
	{ "charity", "live", "charity", true, 0.5, 0.275 },
	{ "adbreak", "live", "ad", true, 0.5, 0.9 },
	{ "recent", "all", "recent", true, 0.0, 1.0 },
	{ "chat", "all", "chat", true, 0.0, 0.873 },
};

const Element *Find(const QString &id)
{
	for(const Element &e : kElements)
	{
		if(id == QLatin1String(e.id))
			return &e;
	}
	return nullptr;
}

// Remembered across streams
const char *const kRemembered[] = { "follower", "subscriber", "gift", "bits", "raid", "redemption", "top_all" };

qint64 NowMs()
{
	return QDateTime::currentMSecsSinceEpoch();
}

qint64 TimeMs(const QJsonValue &v)
{
	// Twitch writes times as RFC 3339; the ad schedule has also been seen with seconds since 1970
	if(v.isDouble())
		return static_cast<qint64>(v.toDouble()) * 1000;
	QString s = v.toString();
	if(s.isEmpty())
		return 0;
	bool number = false;
	const qint64 secs = s.toLongLong(&number);
	if(number)
		return secs > 0 ? secs * 1000 : 0;
	// Twitch sends up to nine places of a second: milliseconds are plenty
	static const QRegularExpression fraction(QStringLiteral("(\\.\\d{3})\\d+"));
	s.replace(fraction, QStringLiteral("\\1"));
	const QDateTime t = QDateTime::fromString(s, Qt::ISODateWithMs);
	return t.isValid() ? t.toMSecsSinceEpoch() : 0;
}

QString TierName(const QString &tier)
{
	if(tier == QLatin1String("2000"))
		return QObject::tr("Tier 2");
	if(tier == QLatin1String("3000"))
		return QObject::tr("Tier 3");
	return QObject::tr("Tier 1");
}

/** An amount of money as Twitch sends it ({value, decimal_places, currency}), written out. */
QString Money(const QJsonObject &amount)
{
	const int places = std::clamp(amount.value("decimal_places").toInt(2), 0, 4);
	double value = amount.value("value").toDouble();
	for(int i = 0; i < places; i++)
		value /= 10.0;
	const QString code = amount.value("currency").toString().left(3).toUpper();
	static const QHash<QString, QString> symbols = {
		{ "EUR", QStringLiteral("€") }, { "USD", QStringLiteral("$") }, { "GBP", QStringLiteral("£") },
		{ "JPY", QStringLiteral("¥") }, { "CAD", QStringLiteral("CA$") }, { "AUD", QStringLiteral("A$") },
	};
	const bool whole = value == static_cast<double>(static_cast<qint64>(value));
	const QString number = QLocale(QLocale::English).toString(value, 'f', whole ? 0 : 2);
	if(symbols.contains(code))
		return symbols.value(code) + number;
	return code.isEmpty() ? number : number + QLatin1Char(' ') + code;
}

bool ValidColor(const QString &c)
{
	static const QRegularExpression re(QStringLiteral("^#[0-9A-Fa-f]{6}$"));
	return re.match(c).hasMatch();
}

} // namespace

TwitchElements::TwitchElements(Settings *settings, TwitchClient *twitch, LiveController *live, LiveAlerts *alerts, QObject *parent)
	: QObject(parent), settings(settings), twitch(twitch), live(live), alerts(alerts)
{
	auto load = [this, settings](const QString &key, const QVariant &def) {
		QVariant v = settings->GetValue(QStringLiteral("twitch_elements/%1").arg(key), def);
		if(def.typeId() == QMetaType::Bool)
			v = v.toBool();
		else if(def.typeId() == QMetaType::Int)
			v = v.toInt();
		else if(def.typeId() == QMetaType::Double)
			v = std::clamp(v.toDouble(), 0.0, 1.0);
		else
			v = v.toString();
		map.insert(key, v);
	};
	for(const Element &e : kElements)
	{
		const QString id = QString::fromLatin1(e.id);
		load(id + QStringLiteral("_show"), e.show);
		load(id + QStringLiteral("_x"), e.x);
		load(id + QStringLiteral("_y"), e.y);
		load(id + QStringLiteral("_scale"), 100);
	}
	// Viewers and uptime in one pill, where the viewers are
	load(QStringLiteral("uptime_joined"), true);
	// The top cheerer of "stream" or of "all" time
	load(QStringLiteral("topcheer_span"), QStringLiteral("stream"));
	connect(&map, &QQmlPropertyMap::valueChanged, this, [this](const QString &key, const QVariant &value) { Store(key, value); });

	Load();

	poll_timer.setInterval(60000);
	connect(&poll_timer, &QTimer::timeout, this, &TwitchElements::Poll);
	if(twitch)
		connect(twitch, &TwitchClient::changed, this, &TwitchElements::OnTwitchChanged);
	if(live)
	{
		connect(live, &LiveController::statsChanged, this, [this]() {
			const bool on_air = this->live->state() == QLatin1String("live");
			// Live with the app before Twitch says so: the uptime counts from now, and Twitch is asked sooner
			if(on_air && current.value("since").toLongLong() == 0)
			{
				current["since"] = NowMs();
				Changed();
			}
			else if(!on_air && !twitch_live && current.value("since").toLongLong() != 0)
			{
				current["since"] = 0;
				Changed();
			}
			poll_timer.setInterval(on_air && !twitch_live ? 20000 : 60000);
			UpdateActive();
		});
	}
	OnTwitchChanged();
	UpdatePage();
}

TwitchElements::~TwitchElements() = default;

QVariantList TwitchElements::elements() const
{
	QVariantList list;
	auto add = [&list](const char *id, const QString &name, const QString &what) {
		const Element *e = Find(QString::fromLatin1(id));
		QVariantMap m;
		m["id"] = QString::fromLatin1(id);
		m["group"] = QString::fromLatin1(e->group);
		m["icon"] = QString::fromLatin1(e->icon);
		m["name"] = name;
		m["what"] = what;
		list.append(m);
	};
	add("follower", tr("Latest follower"), tr("The newest follower’s name"));
	add("subscriber", tr("Latest subscriber"), tr("Newest sub with tier and months"));
	add("gift", tr("Latest gift subs"), tr("Who gifted last, and how many"));
	add("bits", tr("Latest bits"), tr("Who cheered last, and how much"));
	add("raid", tr("Latest raid"), tr("Who raided last, with how many"));
	add("redemption", tr("Latest redemption"), tr("Last reward and who took it"));
	add("topcheer", tr("Top cheerer"), tr("Most bits this stream or all time"));
	add("session", tr("This stream"), tr("New followers, subs and bits this stream"));
	add("viewers", tr("Viewers"), tr("How many watch right now"));
	add("uptime", tr("Uptime"), tr("How long you have been live"));
	add("goal_follow", tr("Follower goal"), tr("Your follower goal as a bar"));
	add("goal_sub", tr("Sub goal"), tr("Your sub goal as a bar"));
	add("hype", tr("Hype train"), tr("Level, progress and time left"));
	add("poll", tr("Poll"), tr("The question and live results"));
	add("prediction", tr("Prediction"), tr("Both outcomes with points and odds"));
	add("charity", tr("Charity"), tr("Raised so far and the target"));
	add("adbreak", tr("Ad break"), tr("Countdown, and when you are back"));
	add("recent", tr("Recent events"), tr("The last events in one line"));
	add("chat", tr("Twitch chat"), tr("Chat messages on the game picture"));
	return list;
}

QVariantMap TwitchElements::sample() const
{
	// The made-up channel of the designs. Its times are spans, not moments ("sample"): a
	// countdown shows what is left, the uptime how long, "at" how long ago
	QVariantMap d;
	d["sample"] = true;
	d["follower"] = QVariantMap { { "name", "luna_plays" }, { "at", 120000 } };
	d["subscriber"] = QVariantMap { { "name", "pixelwolf" }, { "tier", tr("Tier 1") }, { "months", 6 } };
	d["gift"] = QVariantMap { { "name", "nightowl" }, { "count", 5 }, { "total", 25 } };
	d["bits"] = QVariantMap { { "name", "sunny_k" }, { "bits", 500 } };
	d["raid"] = QVariantMap { { "name", "tobi_tv" }, { "viewers", 42 } };
	d["redemption"] = QVariantMap { { "name", "mira" }, { "reward", tr("Jump!") }, { "cost", 500 } };
	d["topcheer"] = QVariantMap { { "name", "sunny_k" }, { "bits", 2500 }, { "avatar", QString() }, { "stream", true } };
	d["session"] = QVariantMap { { "followers", 12 }, { "subs", 3 }, { "bits", 2400 } };
	d["viewers"] = 1284;
	d["since"] = (2 * 3600 + 14 * 60 + 5) * 1000LL;
	d["goal_follow"] = QVariantMap { { "current", 412 }, { "target", 500 }, { "text", tr("next goal: a full no-hit run") } };
	d["goal_sub"] = QVariantMap { { "current", 37 }, { "target", 50 }, { "text", tr("at 50 we play the hardest mode") } };
	d["hype"] = QVariantMap { { "level", 3 }, { "progress", 720 }, { "goal", 1000 }, { "top", "nightowl" }, { "ends", 161000 } };
	QVariantList choices;
	choices << QVariantMap { { "title", tr("Fire Golem") }, { "votes", 553 } }
		<< QVariantMap { { "title", tr("Ice Queen") }, { "votes", 317 } }
		<< QVariantMap { { "title", tr("Shadow Twins") }, { "votes", 154 } };
	d["poll"] = QVariantMap { { "title", tr("Which boss do we fight next?") }, { "choices", choices }, { "total", 1024 }, { "ends", 48000 }, { "done", false } };
	QVariantList outcomes;
	outcomes << QVariantMap { { "title", tr("Yes") }, { "points", 48200 }, { "users", 310 } }
		<< QVariantMap { { "title", tr("No") }, { "points", 29500 }, { "users", 190 } };
	d["prediction"] = QVariantMap { { "title", tr("Will Nova beat the boss first try?") }, { "outcomes", outcomes }, { "locks", 72000 },
		{ "locked", false }, { "winner", -1 }, { "done", false } };
	d["charity"] = QVariantMap { { "name", tr("[YOUR CAUSE]") }, { "current", QStringLiteral("€1,240") }, { "target", QStringLiteral("€2,000") }, { "progress", 0.62 } };
	d["adbreak"] = QVariantMap { { "next", 260000 }, { "until", 0 } };
	QVariantList recent;
	recent << QVariantMap { { "kind", "follow" }, { "name", "luna_plays" }, { "text", tr("followed") } }
		<< QVariantMap { { "kind", "sub" }, { "name", "pixelwolf" }, { "text", tr("subscribed · %1 months").arg(6) } }
		<< QVariantMap { { "kind", "bits" }, { "name", "sunny_k" }, { "text", tr("cheered %1").arg(500) } }
		<< QVariantMap { { "kind", "raid" }, { "name", "tobi_tv" }, { "text", tr("raided with %1").arg(42) } }
		<< QVariantMap { { "kind", "points" }, { "name", "mira" }, { "text", tr("redeemed %1").arg(tr("Jump!")) } }
		<< QVariantMap { { "kind", "gift" }, { "name", "nightowl" }, { "text", tr("gifted %1 subs").arg(5) } };
	d["recent"] = recent;
	QVariantList chat;
	chat << QVariantMap { { "id", "s1" }, { "name", "kirobyte" }, { "color", "#FF7F50" }, { "text", tr("that dodge was clean") }, { "badges", QStringList { "sub" } } }
		<< QVariantMap { { "id", "s2" }, { "name", "luna_plays" }, { "color", "#1E90FF" }, { "text", tr("first time here, love the co-op setup!") }, { "badges", QStringList() } }
		<< QVariantMap { { "id", "s3" }, { "name", "sunny_k" }, { "color", "#DAA520" }, { "text", tr("do the jump again") }, { "badges", QStringList { "sub", "bits" } } }
		<< QVariantMap { { "id", "s4" }, { "name", "modbot" }, { "color", "#34D399" }, { "text", tr("Welcome luna_plays! Be nice, have fun.") }, { "badges", QStringList { "mod" } } }
		<< QVariantMap { { "id", "s5" }, { "name", "pixelwolf" }, { "color", "#FF69B4" }, { "text", tr("Mia carrying this run") }, { "badges", QStringList { "sub" } } };
	d["chat"] = chat;
	return d;
}

bool TwitchElements::active() const
{
	const bool app_live = live && live->state() == QLatin1String("live");
	return logged_in && (twitch_live || app_live);
}

// ------------------------------------------------------------------ settings

void TwitchElements::set(const QString &key, const QVariant &value)
{
	if(map.value(key) == value)
		return;
	map.insert(key, value);
	Store(key, value);
}

void TwitchElements::Store(const QString &key, const QVariant &value)
{
	settings->SetValue(QStringLiteral("twitch_elements/%1").arg(key), value);
	emit valuesChanged();
	UpdatePage();
	// The ad schedule is only asked for while its element is on
	if(key == QLatin1String("adbreak_show") && value.toBool())
		Poll();
	if(key == QLatin1String("topcheer_span") || key == QLatin1String("topcheer_show"))
	{
		Changed();
		FetchLeaderboard();
	}
}

bool TwitchElements::SetPlace(const QString &key, double value)
{
	static const QRegularExpression re(QStringLiteral("^([a-z_]+)_(x|y)$"));
	const auto m = re.match(key);
	if(!m.hasMatch() || !Find(m.captured(1)) || value < 0.0 || value > 1.0)
		return false;
	set(key, value);
	return true;
}

void TwitchElements::resetPlaces()
{
	for(const Element &e : kElements)
	{
		const QString id = QString::fromLatin1(e.id);
		set(id + QStringLiteral("_x"), e.x);
		set(id + QStringLiteral("_y"), e.y);
		set(id + QStringLiteral("_scale"), 100);
	}
	set(QStringLiteral("uptime_joined"), true);
}

// ------------------------------------------------------------------ data

void TwitchElements::Load()
{
	for(const char *key : kRemembered)
	{
		const QByteArray json = settings->GetValue(QStringLiteral("twitch_latest/%1").arg(QLatin1String(key))).toByteArray();
		const QJsonObject o = QJsonDocument::fromJson(json).object();
		if(!o.isEmpty())
			current[QString::fromLatin1(key)] = o.toVariantMap();
	}
	// This stream's counts, in case the app was started again during a stream
	const QJsonObject s = QJsonDocument::fromJson(settings->GetValue(QStringLiteral("twitch_latest/session")).toByteArray()).object();
	if(!s.isEmpty())
	{
		current["session"] = s.toVariantMap();
		const QJsonObject top = s.value("top").toObject();
		if(!top.value("uid").toString().isEmpty())
		{
			stream_bits.insert(top.value("uid").toString(), top.value("bits").toInt());
			stream_bits_names.insert(top.value("uid").toString(), top.value("name").toString());
		}
	}
	current["viewers"] = -1;
	current["since"] = 0;
}

void TwitchElements::Remember(const QString &id)
{
	const QJsonObject o = QJsonObject::fromVariantMap(current.value(id).toMap());
	settings->SetValue(QStringLiteral("twitch_latest/%1").arg(id), QJsonDocument(o).toJson(QJsonDocument::Compact));
}

void TwitchElements::Changed()
{
	// The top cheerer of the chosen span
	QVariantMap top;
	if(map.value("topcheer_span").toString() == QLatin1String("all"))
	{
		top = current.value("top_all").toMap();
		top["stream"] = false;
	}
	else
	{
		QString best;
		int most = 0;
		for(auto it = stream_bits.constBegin(); it != stream_bits.constEnd(); ++it)
		{
			if(it.value() > most)
			{
				most = it.value();
				best = it.key();
			}
		}
		if(!best.isEmpty())
		{
			top["uid"] = best;
			top["name"] = stream_bits_names.value(best);
			top["bits"] = most;
			top["stream"] = true;
		}
	}
	if(!top.value("name").toString().isEmpty())
	{
		const QString uid = top.value("uid").toString();
		if(uid == avatar_for)
			top["avatar"] = current.value("avatar").toString();
		else if(!uid.isEmpty() && map.value("topcheer_show").toBool())
			FetchAvatar(uid);
		current["topcheer"] = top;
	}
	else
	{
		current.remove("topcheer");
	}
	UpdatePage();
	emit dataChanged();
}

void TwitchElements::UpdatePage()
{
	QJsonObject places;
	for(const Element &e : kElements)
	{
		const QString id = QString::fromLatin1(e.id);
		QJsonObject p;
		p["on"] = map.value(id + QStringLiteral("_show")).toBool();
		p["x"] = map.value(id + QStringLiteral("_x")).toDouble();
		p["y"] = map.value(id + QStringLiteral("_y")).toDouble();
		p["scale"] = std::clamp(map.value(id + QStringLiteral("_scale")).toInt(), 40, 250);
		places[id] = p;
	}
	QJsonObject o;
	// The OBS page draws them in the overlay's own look ("transparent", "glass" or "solid")
	o["look"] = settings->GetValue(QStringLiteral("overlay/style"), QStringLiteral("transparent")).toString();
	o["joined"] = map.value("uptime_joined").toBool();
	o["places"] = places;
	o["active"] = active();
	o["data"] = QJsonObject::fromVariantMap(current);
	static const QJsonObject made_up = QJsonObject::fromVariantMap(sample());
	o["sample"] = made_up;
	page_json = QJsonDocument(o).toJson(QJsonDocument::Compact);
}

void TwitchElements::UpdateActive()
{
	const bool now = active();
	UpdatePage();
	if(now != was_active)
	{
		was_active = now;
		emit activeChanged();
	}
}

void TwitchElements::OnTwitchChanged()
{
	const bool now = twitch && twitch->state() == QLatin1String("logged_in");
	if(now == logged_in)
		return;
	logged_in = now;
	if(logged_in)
	{
		poll_timer.start();
		FetchStart();
	}
	else
	{
		poll_timer.stop();
		twitch_live = false;
		for(const char *key : { "goal_follow", "goal_sub", "hype", "poll", "prediction", "charity", "adbreak", "chat" })
			current.remove(QString::fromLatin1(key));
		current["viewers"] = -1;
		Changed();
	}
	UpdateActive();
}

QString TwitchElements::Clean(const QString &text, int max) const
{
	if(alerts)
		return alerts->Clean(text, max);
	return text.simplified().left(max);
}

QString TwitchElements::Name(const QJsonObject &e, const char *key) const
{
	if(e.value("is_anonymous").toBool())
		return tr("Anonymous");
	QString n = e.value(QLatin1String(key)).toString();
	if(n.isEmpty())
		n = e.value(QStringLiteral("user_login")).toString();
	return Clean(n.isEmpty() ? tr("Somebody") : n, 25);
}

void TwitchElements::AddRecent(const QString &kind, const QString &name, const QString &text)
{
	QVariantList list = current.value("recent").toList();
	list.prepend(QVariantMap { { "kind", kind }, { "name", name }, { "text", text } });
	while(list.size() > 12)
		list.removeLast();
	current["recent"] = list;
}

void TwitchElements::ClearLater(const QString &key, int ms)
{
	const quint64 mine = ++serial;
	serials[key] = mine;
	QTimer::singleShot(ms, this, [this, key, mine]() {
		if(serials.value(key) != mine)
			return;
		current.remove(key);
		Changed();
	});
}

void TwitchElements::SetStreamStart(qint64 ms)
{
	if(ms <= 0)
		return;
	current["since"] = ms;
	// A new stream: its own counts
	QVariantMap s = current.value("session").toMap();
	if(s.value("since").toLongLong() != ms)
	{
		s.clear();
		s["since"] = ms;
		s["followers"] = 0;
		s["subs"] = 0;
		s["bits"] = 0;
		current["session"] = s;
		stream_bits.clear();
		stream_bits_names.clear();
		settings->SetValue(QStringLiteral("twitch_latest/session"), QJsonDocument(QJsonObject::fromVariantMap(s)).toJson(QJsonDocument::Compact));
	}
}

// ------------------------------------------------------------------ from Twitch

void TwitchElements::OnTwitchEvent(const QString &type, const QJsonObject &e)
{
	const qint64 now = NowMs();
	QVariantMap session = current.value("session").toMap();
	auto count = [&session](const char *key, int add) {
		session[QString::fromLatin1(key)] = session.value(QString::fromLatin1(key)).toInt() + add;
	};
	bool session_changed = false;

	if(type == QLatin1String("channel.follow"))
	{
		const QString name = Name(e);
		current["follower"] = QVariantMap { { "name", name }, { "at", now } };
		Remember("follower");
		count("followers", 1);
		session_changed = true;
		AddRecent("follow", name, tr("followed"));
	}
	else if(type == QLatin1String("channel.subscribe"))
	{
		if(e.value("is_gift").toBool())
			return;
		const QString name = Name(e);
		current["subscriber"] = QVariantMap { { "name", name }, { "tier", TierName(e.value("tier").toString()) }, { "months", 1 } };
		Remember("subscriber");
		count("subs", 1);
		session_changed = true;
		AddRecent("sub", name, tr("subscribed · %1").arg(TierName(e.value("tier").toString())));
	}
	else if(type == QLatin1String("channel.subscription.message"))
	{
		const QString name = Name(e);
		const int months = std::max(1, e.value("cumulative_months").toInt(1));
		current["subscriber"] = QVariantMap { { "name", name }, { "tier", TierName(e.value("tier").toString()) }, { "months", months } };
		Remember("subscriber");
		count("subs", 1);
		session_changed = true;
		AddRecent("sub", name, tr("subscribed · %1 months").arg(months));
	}
	else if(type == QLatin1String("channel.subscription.gift"))
	{
		const QString name = Name(e);
		const int total = std::max(1, e.value("total").toInt(1));
		current["gift"] = QVariantMap { { "name", name }, { "count", total }, { "total", e.value("cumulative_total").toInt(0) } };
		Remember("gift");
		count("subs", total);
		session_changed = true;
		AddRecent("gift", name, total == 1 ? tr("gifted a sub") : tr("gifted %1 subs").arg(total));
	}
	else if(type == QLatin1String("channel.cheer"))
	{
		const QString name = Name(e);
		const int bits = std::max(0, e.value("bits").toInt());
		current["bits"] = QVariantMap { { "name", name }, { "bits", bits } };
		Remember("bits");
		count("bits", bits);
		session_changed = true;
		if(!e.value("is_anonymous").toBool())
		{
			QString uid = e.value("user_id").toString();
			if(uid.isEmpty())
				uid = e.value("user_login").toString(name);
			stream_bits[uid] += bits;
			stream_bits_names[uid] = name;
			// Who leads this stream, kept with its counts
			QString best;
			for(auto it = stream_bits.constBegin(); it != stream_bits.constEnd(); ++it)
				if(best.isEmpty() || it.value() > stream_bits.value(best))
					best = it.key();
			session["top"] = QVariantMap { { "uid", best }, { "name", stream_bits_names.value(best) }, { "bits", stream_bits.value(best) } };
		}
		AddRecent("bits", name, tr("cheered %1").arg(bits));
		// The leaderboard of all time, once Twitch has counted it
		QTimer::singleShot(15000, this, &TwitchElements::FetchLeaderboard);
	}
	else if(type == QLatin1String("channel.raid"))
	{
		const QString name = Name(e, "from_broadcaster_user_name");
		const int viewers = std::max(0, e.value("viewers").toInt());
		current["raid"] = QVariantMap { { "name", name }, { "viewers", viewers } };
		Remember("raid");
		AddRecent("raid", name, tr("raided with %1").arg(viewers));
	}
	else if(type == QLatin1String("channel.channel_points_custom_reward_redemption.add")
		|| type == QLatin1String("channel.channel_points_automatic_reward_redemption.add"))
	{
		const QString name = Name(e);
		const QJsonObject reward = e.value("reward").toObject();
		QString title = reward.value("title").toString();
		if(title.isEmpty())
			title = reward.value("type").toString().replace(QLatin1Char('_'), QLatin1Char(' '));
		title = Clean(title, 40);
		current["redemption"] = QVariantMap { { "name", name }, { "reward", title }, { "cost", reward.value("cost").toInt(0) } };
		Remember("redemption");
		AddRecent("points", name, tr("redeemed %1").arg(title));
	}
	else if(type.startsWith(QLatin1String("channel.hype_train.")))
		OnHype(e, type.endsWith(QLatin1String(".end")));
	else if(type.startsWith(QLatin1String("channel.poll.")))
		OnPoll(e, type.endsWith(QLatin1String(".end")));
	else if(type.startsWith(QLatin1String("channel.prediction.")))
		OnPrediction(e, type.mid(19));
	else if(type.startsWith(QLatin1String("channel.goal.")))
		OnGoal(e, type.endsWith(QLatin1String(".end")));
	else if(type == QLatin1String("channel.charity_campaign.donate"))
	{
		AddRecent("charity", Name(e), tr("donated %1").arg(Money(e.value("amount").toObject())));
	}
	else if(type.startsWith(QLatin1String("channel.charity_campaign.")))
		OnCharity(e, type.endsWith(QLatin1String(".stop")));
	else if(type == QLatin1String("channel.shoutout.receive"))
	{
		AddRecent("shoutout", Name(e, "from_broadcaster_user_name"), tr("gave you a shoutout"));
	}
	else if(type == QLatin1String("channel.ad_break.begin"))
	{
		const int secs = std::max(0, e.value("duration_seconds").toInt());
		qint64 start = TimeMs(e.value("started_at"));
		if(start <= 0)
			start = now;
		QVariantMap ad = current.value("adbreak").toMap();
		ad["until"] = start + secs * 1000LL;
		ad["next"] = 0;
		current["adbreak"] = ad;
		// Asks for the next one once this one is over
		QTimer::singleShot(secs * 1000 + 5000, this, &TwitchElements::Poll);
	}
	else if(type == QLatin1String("stream.online"))
	{
		twitch_live = true;
		SetStreamStart(TimeMs(e.value("started_at")) > 0 ? TimeMs(e.value("started_at")) : now);
		poll_timer.setInterval(60000);
		QTimer::singleShot(2000, this, &TwitchElements::Poll);
		UpdateActive();
	}
	else if(type == QLatin1String("stream.offline"))
	{
		twitch_live = false;
		current["viewers"] = -1;
		if(!(live && live->state() == QLatin1String("live")))
			current["since"] = 0;
		current.remove("adbreak");
		UpdateActive();
	}
	else if(type == QLatin1String("channel.chat.message"))
		OnChat(e);
	else if(type == QLatin1String("channel.chat.message_delete") || type == QLatin1String("channel.chat.clear_user_messages")
		|| type == QLatin1String("channel.chat.clear"))
	{
		// What the moderators take away goes from the stream too
		const QString id = e.value("message_id").toString();
		const QString uid = e.value("target_user_id").toString();
		QVariantList kept;
		if(type != QLatin1String("channel.chat.clear"))
		{
			for(const QVariant &v : current.value("chat").toList())
			{
				const QVariantMap m = v.toMap();
				if((!id.isEmpty() && m.value("id").toString() == id) || (!uid.isEmpty() && m.value("uid").toString() == uid))
					continue;
				kept.append(m);
			}
		}
		current["chat"] = kept;
	}
	else
	{
		return;
	}

	if(session_changed)
	{
		current["session"] = session;
		settings->SetValue(QStringLiteral("twitch_latest/session"), QJsonDocument(QJsonObject::fromVariantMap(session)).toJson(QJsonDocument::Compact));
	}
	Changed();
}

void TwitchElements::OnHype(const QJsonObject &e, bool ended)
{
	if(ended)
	{
		current.remove("hype");
		serials.remove("hype");
		return;
	}
	QStringList top;
	for(const QJsonValue &c : e.value("top_contributions").toArray())
		top << Clean(c.toObject().value("user_name").toString(), 25);
	top.removeAll(QString());
	QVariantMap h;
	h["level"] = std::max(1, e.value("level").toInt(1));
	h["progress"] = std::max(0, e.value("progress").toInt());
	h["goal"] = std::max(1, e.value("goal").toInt(1));
	h["top"] = top.isEmpty() ? QString() : top.first();
	h["ends"] = TimeMs(e.value("expires_at"));
	current["hype"] = h;
	serials.remove("hype");
}

void TwitchElements::OnPoll(const QJsonObject &e, bool ended)
{
	QVariantList choices;
	int total = 0;
	for(const QJsonValue &v : e.value("choices").toArray())
	{
		const QJsonObject c = v.toObject();
		// Votes in all: the event counts the ones with points and bits in "votes" already
		const int votes = std::max(0, c.value("votes").toInt());
		total += votes;
		choices.append(QVariantMap { { "title", Clean(c.value("title").toString(), 40) }, { "votes", votes } });
	}
	if(ended && e.value("status").toString() == QLatin1String("archived"))
	{
		current.remove("poll");
		return;
	}
	QVariantMap p;
	p["title"] = Clean(e.value("title").toString(), 80);
	p["choices"] = choices;
	p["total"] = total;
	p["ends"] = TimeMs(e.value("ends_at"));
	p["done"] = ended;
	current["poll"] = p;
	serials.remove("poll");
	// The result stays a little, then the poll is gone
	if(ended)
		ClearLater("poll", 20000);
}

void TwitchElements::OnPrediction(const QJsonObject &e, const QString &stage)
{
	const bool ended = stage == QLatin1String("end");
	if(ended && e.value("status").toString() == QLatin1String("canceled"))
	{
		current.remove("prediction");
		return;
	}
	QVariantList outcomes;
	int winner = -1;
	const QString winning = e.value("winning_outcome_id").toString();
	for(const QJsonValue &v : e.value("outcomes").toArray())
	{
		const QJsonObject o = v.toObject();
		if(!winning.isEmpty() && o.value("id").toString() == winning)
			winner = static_cast<int>(outcomes.size());
		outcomes.append(QVariantMap { { "title", Clean(o.value("title").toString(), 25) },
			{ "points", static_cast<double>(o.value("channel_points").toDouble()) }, { "users", o.value("users").toInt() } });
	}
	QVariantMap p;
	p["title"] = Clean(e.value("title").toString(), 80);
	p["outcomes"] = outcomes;
	p["locks"] = TimeMs(e.value("locks_at"));
	p["locked"] = stage == QLatin1String("lock") || ended || !e.value("locked_at").toString().isEmpty();
	p["winner"] = winner;
	p["done"] = ended;
	current["prediction"] = p;
	serials.remove("prediction");
	if(ended)
		ClearLater("prediction", 20000);
}

void TwitchElements::OnGoal(const QJsonObject &e, bool ended)
{
	// Follower goals and sub goals; Twitch's newer kinds (bits, cheerers) have no element
	const QString kind = e.value("type").toString();
	QString key;
	if(kind == QLatin1String("follow") || kind == QLatin1String("follower"))
		key = QStringLiteral("goal_follow");
	else if(kind.contains(QLatin1String("subscription")))
		key = QStringLiteral("goal_sub");
	else
		return;
	const int got = std::max(0, e.value("current_amount").toInt());
	const int target = std::max(1, e.value("target_amount").toInt(1));
	current[key] = QVariantMap { { "current", got }, { "target", target }, { "text", Clean(e.value("description").toString(), 60) },
		{ "reached", ended && e.value("is_achieved").toBool() } };
	serials.remove(key);
	if(ended)
		ClearLater(key, e.value("is_achieved").toBool() ? 20000 : 0);
}

void TwitchElements::OnCharity(const QJsonObject &e, bool ended)
{
	if(ended)
	{
		current.remove("charity");
		return;
	}
	const QJsonObject got = e.value("current_amount").toObject();
	const QJsonObject target = e.value("target_amount").toObject();
	const double a = got.value("value").toDouble();
	const double b = target.value("value").toDouble();
	QVariantMap c;
	c["name"] = Clean(e.value("charity_name").toString(), 40);
	c["current"] = Money(got);
	c["target"] = b > 0 ? Money(target) : QString();
	// Both in the same currency and places: their ratio needs no conversion
	c["progress"] = b > 0 ? std::clamp(a / b, 0.0, 1.0) : 0.0;
	current["charity"] = c;
}

void TwitchElements::OnChat(const QJsonObject &e)
{
	const QString text = Clean(e.value("message").toObject().value("text").toString(), 200);
	if(text.isEmpty())
		return;
	QStringList badges;
	for(const QJsonValue &v : e.value("badges").toArray())
	{
		const QString set = v.toObject().value("set_id").toString();
		QString b;
		if(set == QLatin1String("broadcaster"))
			b = QStringLiteral("broadcaster");
		else if(set == QLatin1String("moderator"))
			b = QStringLiteral("mod");
		else if(set == QLatin1String("vip"))
			b = QStringLiteral("vip");
		else if(set == QLatin1String("subscriber") || set == QLatin1String("founder"))
			b = QStringLiteral("sub");
		else if(set == QLatin1String("bits"))
			b = QStringLiteral("bits");
		if(!b.isEmpty() && !badges.contains(b) && badges.size() < 3)
			badges << b;
	}
	QString color = e.value("color").toString();
	if(!ValidColor(color))
	{
		// As Twitch does for names without a colour of their own
		static const char *const palette[] = { "#FF0000", "#0000FF", "#008000", "#B22222", "#FF7F50", "#9ACD32", "#FF4500",
			"#2E8B57", "#DAA520", "#D2691E", "#5F9EA0", "#1E90FF", "#FF69B4", "#8A2BE2", "#00FF7F" };
		const QString login = e.value("chatter_user_login").toString();
		color = QString::fromLatin1(palette[qHash(login) % (sizeof(palette) / sizeof(palette[0]))]);
	}
	QVariantList chat = current.value("chat").toList();
	chat.append(QVariantMap {
		{ "id", e.value("message_id").toString() },
		{ "uid", e.value("chatter_user_id").toString() },
		{ "name", Clean(e.value("chatter_user_name").toString(e.value("chatter_user_login").toString()), 25) },
		{ "color", color },
		{ "text", text },
		{ "badges", badges },
	});
	while(chat.size() > 6)
		chat.removeFirst();
	current["chat"] = chat;
}

void TwitchElements::OnAds(const QJsonObject &reply)
{
	const QJsonArray list = reply.value("data").toArray();
	if(list.isEmpty())
		return;
	const QJsonObject o = list.first().toObject();
	QVariantMap ad = current.value("adbreak").toMap();
	const qint64 next = TimeMs(o.value("next_ad_at"));
	ad["next"] = next > NowMs() ? next : 0;
	if(ad.value("until").toLongLong() < NowMs())
		ad["until"] = 0;
	current["adbreak"] = ad;
	Changed();
}

// ------------------------------------------------------------------ Twitch's API

void TwitchElements::Poll()
{
	if(!twitch || !logged_in)
		return;
	const QString uid = twitch->UserId();
	// Gently, once a minute: only while the viewers or the uptime show, or to find out when
	// the app's own stream has reached Twitch (FetchStart asks once at the start in any case)
	const bool app_live = live && live->state() == QLatin1String("live");
	const bool wanted = map.value("viewers_show").toBool() || map.value("uptime_show").toBool() || (app_live && !twitch_live);
	if(!wanted && polled_once)
		return;
	polled_once = true;
	twitch->Helix("GET", QStringLiteral("/streams?user_id=%1").arg(uid), QJsonObject(), [this](int status, const QJsonObject &r) {
		if(status != 200)
			return;
		const QJsonArray list = r.value("data").toArray();
		if(list.isEmpty())
		{
			if(twitch_live)
			{
				twitch_live = false;
				current["viewers"] = -1;
				if(!(live && live->state() == QLatin1String("live")))
					current["since"] = 0;
			}
		}
		else
		{
			const QJsonObject s = list.first().toObject();
			twitch_live = true;
			current["viewers"] = s.value("viewer_count").toInt(-1);
			SetStreamStart(TimeMs(s.value("started_at")));
			poll_timer.setInterval(60000);
		}
		UpdateActive();
		Changed();
	});
	if(twitch_live && map.value("adbreak_show").toBool())
	{
		twitch->Helix("GET", QStringLiteral("/channels/ads?broadcaster_id=%1").arg(uid), QJsonObject(), [this](int status, const QJsonObject &r) {
			if(status == 200)
				OnAds(r);
		});
	}
}

void TwitchElements::FetchStart()
{
	if(!twitch)
		return;
	const QString uid = twitch->UserId();
	Poll();
	FetchLeaderboard();
	// What already runs when the app starts
	twitch->Helix("GET", QStringLiteral("/goals?broadcaster_id=%1").arg(uid), QJsonObject(), [this](int status, const QJsonObject &r) {
		if(status != 200)
			return;
		for(const QJsonValue &v : r.value("data").toArray())
			OnGoal(v.toObject(), false);
		Changed();
	});
	twitch->Helix("GET", QStringLiteral("/polls?broadcaster_id=%1&first=1").arg(uid), QJsonObject(), [this](int status, const QJsonObject &r) {
		const QJsonArray list = r.value("data").toArray();
		if(status != 200 || list.isEmpty())
			return;
		QJsonObject p = list.first().toObject();
		if(p.value("status").toString() != QLatin1String("ACTIVE"))
			return;
		const qint64 start = TimeMs(p.value("started_at"));
		p["ends_at"] = QDateTime::fromMSecsSinceEpoch(start + p.value("duration").toInt() * 1000LL, QTimeZone::UTC).toString(Qt::ISODate);
		OnPoll(p, false);
		Changed();
	});
	twitch->Helix("GET", QStringLiteral("/predictions?broadcaster_id=%1&first=1").arg(uid), QJsonObject(), [this](int status, const QJsonObject &r) {
		const QJsonArray list = r.value("data").toArray();
		if(status != 200 || list.isEmpty())
			return;
		QJsonObject p = list.first().toObject();
		const QString state = p.value("status").toString();
		if(state != QLatin1String("ACTIVE") && state != QLatin1String("LOCKED"))
			return;
		const qint64 start = TimeMs(p.value("created_at"));
		p["locks_at"] = QDateTime::fromMSecsSinceEpoch(start + p.value("prediction_window").toInt() * 1000LL, QTimeZone::UTC).toString(Qt::ISODate);
		OnPrediction(p, state == QLatin1String("LOCKED") ? QStringLiteral("lock") : QStringLiteral("progress"));
		Changed();
	});
	twitch->Helix("GET", QStringLiteral("/charity/campaigns?broadcaster_id=%1").arg(uid), QJsonObject(), [this](int status, const QJsonObject &r) {
		const QJsonArray list = r.value("data").toArray();
		if(status != 200 || list.isEmpty())
			return;
		OnCharity(list.first().toObject(), false);
		Changed();
	});
}

void TwitchElements::FetchLeaderboard()
{
	if(!twitch || !logged_in || !map.value("topcheer_show").toBool() || map.value("topcheer_span").toString() != QLatin1String("all"))
		return;
	twitch->Helix("GET", QStringLiteral("/bits/leaderboard?count=1&period=all"), QJsonObject(), [this](int status, const QJsonObject &r) {
		const QJsonArray list = r.value("data").toArray();
		if(status != 200 || list.isEmpty())
			return;
		const QJsonObject o = list.first().toObject();
		current["top_all"] = QVariantMap { { "uid", o.value("user_id").toString() }, { "name", Clean(o.value("user_name").toString(), 25) },
			{ "bits", o.value("score").toInt() } };
		Remember("top_all");
		Changed();
	});
}

void TwitchElements::FetchAvatar(const QString &user_id)
{
	if(!twitch || !logged_in || user_id == avatar_for)
		return;
	avatar_for = user_id;
	current["avatar"] = QString();
	twitch->Helix("GET", QStringLiteral("/users?id=%1").arg(QString::fromLatin1(QUrl::toPercentEncoding(user_id))), QJsonObject(),
		[this, user_id](int status, const QJsonObject &r) {
			const QJsonArray list = r.value("data").toArray();
			if(status != 200 || list.isEmpty() || avatar_for != user_id)
				return;
			// Pictures only from Twitch's own image servers
			const QUrl url(list.first().toObject().value("profile_image_url").toString());
			if(url.scheme() != QLatin1String("https") || url.host() != QLatin1String("static-cdn.jtvnw.net"))
				return;
			current["avatar"] = url.toString();
			Changed();
		});
}
