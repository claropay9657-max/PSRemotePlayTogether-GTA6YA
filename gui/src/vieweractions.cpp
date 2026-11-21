// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "vieweractions.h"
#include "settings.h"
#include "streamsession.h"
#include "twitch.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUrl>
#include <QUuid>

#include <memory>

namespace
{

const QStringList kTriggers = { "points", "bits", "sub", "follow", "raid" };
const QStringList kInputs = { "cross", "circle", "square", "triangle", "l1", "r1", "l2", "r2", "l3", "r3", "options",
	"up", "down", "left", "right", "ls_up", "ls_down", "ls_left", "ls_right", "rs_up", "rs_down", "rs_left", "rs_right" };

QString InputLabel(const QString &input)
{
	static const QHash<QString, QString> names = {
		{ "cross", QStringLiteral("✕") }, { "circle", QStringLiteral("○") }, { "square", QStringLiteral("□") },
		{ "triangle", QStringLiteral("△") }, { "l1", "L1" }, { "r1", "R1" }, { "l2", "L2" }, { "r2", "R2" },
		{ "l3", "L3" }, { "r3", "R3" }, { "options", "Options" }, { "up", QStringLiteral("↑") }, { "down", QStringLiteral("↓") },
		{ "left", QStringLiteral("←") }, { "right", QStringLiteral("→") },
	};
	return names.value(input, input);
}

/** What one action does to the controller while it is held. */
void Apply(const QString &input, ChiakiControllerState *s)
{
	static const QHash<QString, uint32_t> buttons = {
		{ "cross", CHIAKI_CONTROLLER_BUTTON_CROSS }, { "circle", CHIAKI_CONTROLLER_BUTTON_MOON },
		{ "square", CHIAKI_CONTROLLER_BUTTON_BOX }, { "triangle", CHIAKI_CONTROLLER_BUTTON_PYRAMID },
		{ "l1", CHIAKI_CONTROLLER_BUTTON_L1 }, { "r1", CHIAKI_CONTROLLER_BUTTON_R1 },
		{ "l3", CHIAKI_CONTROLLER_BUTTON_L3 }, { "r3", CHIAKI_CONTROLLER_BUTTON_R3 },
		{ "options", CHIAKI_CONTROLLER_BUTTON_OPTIONS },
		{ "up", CHIAKI_CONTROLLER_BUTTON_DPAD_UP }, { "down", CHIAKI_CONTROLLER_BUTTON_DPAD_DOWN },
		{ "left", CHIAKI_CONTROLLER_BUTTON_DPAD_LEFT }, { "right", CHIAKI_CONTROLLER_BUTTON_DPAD_RIGHT },
	};
	const int16_t full = 0x7fff;
	if(buttons.contains(input))
		s->buttons |= buttons.value(input);
	else if(input == QLatin1String("l2"))
		s->l2_state = 0xff;
	else if(input == QLatin1String("r2"))
		s->r2_state = 0xff;
	else if(input == QLatin1String("ls_up"))
		s->left_y = -full;
	else if(input == QLatin1String("ls_down"))
		s->left_y = full;
	else if(input == QLatin1String("ls_left"))
		s->left_x = -full;
	else if(input == QLatin1String("ls_right"))
		s->left_x = full;
	else if(input == QLatin1String("rs_up"))
		s->right_y = -full;
	else if(input == QLatin1String("rs_down"))
		s->right_y = full;
	else if(input == QLatin1String("rs_left"))
		s->right_x = -full;
	else if(input == QLatin1String("rs_right"))
		s->right_x = full;
}

QVariantMap Clean(const QVariantMap &in)
{
	QVariantMap a;
	const QString id = in.value("id").toString();
	a["id"] = id.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : id;
	const QString input = kInputs.contains(in.value("input").toString()) ? in.value("input").toString() : QStringLiteral("cross");
	a["input"] = input;
	a["inputLabel"] = InputLabel(input);
	QString title = in.value("title").toString().simplified().left(45); // Twitch's longest reward title
	a["title"] = title.isEmpty() ? QObject::tr("Press %1").arg(InputLabel(input)) : title;
	a["trigger"] = kTriggers.contains(in.value("trigger").toString()) ? in.value("trigger").toString() : QStringLiteral("points");
	a["cost"] = qBound(1, in.value("cost", 500).toInt(), 1000000);
	a["min"] = qBound(1, in.value("min", 100).toInt(), 1000000);
	a["cooldown"] = qBound(0, in.value("cooldown", 30).toInt(), 3600);
	a["hold"] = qBound(50, in.value("hold", 200).toInt(), 5000);
	a["on"] = in.value("on", true).toBool();
	a["reward"] = in.value("reward").toString();
	return a;
}

qint64 Now() { return QDateTime::currentMSecsSinceEpoch(); }

} // namespace

ViewerActions::ViewerActions(Settings *settings, TwitchClient *twitch, QObject *parent)
	: QObject(parent), settings(settings), twitch(twitch)
{
	on = settings->GetValue(QStringLiteral("twitch_actions/enabled"), false).toBool();
	per_minute = qBound(1, settings->GetValue(QStringLiteral("twitch_actions/per_minute"), 6).toInt(), 60);
	const QJsonArray saved = QJsonDocument::fromJson(settings->GetValue(QStringLiteral("twitch_actions/list")).toByteArray()).array();
	for(const QJsonValue &v : saved)
		list.append(Clean(v.toObject().toVariantMap()));
	sync_soon.setSingleShot(true);
	sync_soon.setInterval(1500);
	connect(&sync_soon, &QTimer::timeout, this, &ViewerActions::syncRewards);
	// Once logged in, the rewards follow the list
	connect(twitch, &TwitchClient::changed, this, [this]() {
		if(this->twitch->state() == QLatin1String("logged_in") && !this->twitch->UserId().isEmpty() && !list.isEmpty() && status_text.isEmpty())
			sync_soon.start();
	});
}

void ViewerActions::Save()
{
	QJsonArray out;
	for(const QVariant &a : list)
	{
		QVariantMap m = a.toMap();
		m.remove("inputLabel");
		out.append(QJsonObject::fromVariantMap(m));
	}
	settings->SetValue(QStringLiteral("twitch_actions/enabled"), on);
	settings->SetValue(QStringLiteral("twitch_actions/per_minute"), per_minute);
	settings->SetValue(QStringLiteral("twitch_actions/list"), QJsonDocument(out).toJson(QJsonDocument::Compact));
}

void ViewerActions::SetStatus(const QString &text)
{
	status_text = text;
	emit changed();
}

int ViewerActions::IndexOfId(const QString &id) const
{
	for(int i = 0; i < list.size(); i++)
		if(list[i].toMap().value("id").toString() == id)
			return i;
	return -1;
}

void ViewerActions::setEnabled(bool enabled)
{
	if(enabled == on)
		return;
	on = enabled;
	if(!on)
	{
		for(const Job &j : queue)
			Finish(j, false);
		queue.clear();
		Release();
	}
	Save();
	emit changed();
	// Rewards can only be redeemed while actions are on
	sync_soon.start();
}

void ViewerActions::setPerMinute(int n)
{
	n = qBound(1, n, 60);
	if(n == per_minute)
		return;
	per_minute = n;
	Save();
	emit changed();
}

void ViewerActions::addAction()
{
	if(list.size() >= 20)
		return;
	list.append(Clean({ { "input", "cross" }, { "trigger", "points" } }));
	Save();
	emit changed();
	sync_soon.start();
}

void ViewerActions::setAction(int index, const QVariantMap &values)
{
	if(index < 0 || index >= list.size())
		return;
	QVariantMap a = list[index].toMap();
	for(auto it = values.begin(); it != values.end(); ++it)
		if(it.key() != QLatin1String("id") && it.key() != QLatin1String("reward"))
			a[it.key()] = it.value();
	// A title that was made from the old button follows the new one
	if(values.contains("input") && !values.contains("title") && a.value("title").toString() == QObject::tr("Press %1").arg(list[index].toMap().value("inputLabel").toString()))
		a["title"] = QString();
	list[index] = Clean(a);
	Save();
	emit changed();
	sync_soon.start();
}

void ViewerActions::removeAction(int index)
{
	if(index < 0 || index >= list.size())
		return;
	const QString reward = list[index].toMap().value("reward").toString();
	list.removeAt(index);
	Save();
	emit changed();
	if(!reward.isEmpty() && !twitch->UserId().isEmpty())
		twitch->Helix("DELETE", QStringLiteral("/channel_points/custom_rewards?broadcaster_id=%1&id=%2").arg(twitch->UserId(), reward), QJsonObject(),
			[](int, const QJsonObject &) {});
}

void ViewerActions::syncRewards()
{
	if(twitch->state() != QLatin1String("logged_in") || twitch->UserId().isEmpty())
	{
		bool any = false;
		for(const QVariant &a : list)
			any = any || a.toMap().value("trigger").toString() == QLatin1String("points");
		if(any)
			SetStatus(tr("Log in with Twitch to make the channel point rewards."));
		return;
	}
	const QString user = twitch->UserId();
	SetStatus(tr("Updating the rewards on Twitch…"));
	auto pending = std::make_shared<int>(0);
	auto problem = std::make_shared<QString>();
	auto done = [this, pending, problem]() {
		if(--*pending > 0)
			return;
		SetStatus(problem->isEmpty() ? tr("Rewards on Twitch are up to date.") : *problem);
	};
	for(const QVariant &v : list)
	{
		const QVariantMap a = v.toMap();
		const QString id = a.value("id").toString();
		const QString reward = a.value("reward").toString();
		if(a.value("trigger").toString() != QLatin1String("points"))
		{
			// Not a channel point action any more: its reward goes
			if(!reward.isEmpty())
			{
				++*pending;
				twitch->Helix("DELETE", QStringLiteral("/channel_points/custom_rewards?broadcaster_id=%1&id=%2").arg(user, reward), QJsonObject(),
					[this, id, done](int, const QJsonObject &) {
						const int i = IndexOfId(id);
						if(i >= 0)
						{
							QVariantMap m = list[i].toMap();
							m["reward"] = QString();
							list[i] = m;
							Save();
						}
						done();
					});
			}
			continue;
		}
		QJsonObject body;
		body["title"] = a.value("title").toString();
		body["cost"] = a.value("cost").toInt();
		body["prompt"] = tr("Presses %1 in the game, on the streamer's controller.").arg(a.value("inputLabel").toString());
		body["is_enabled"] = on && a.value("on").toBool();
		const int cooldown = a.value("cooldown").toInt();
		body["is_global_cooldown_enabled"] = cooldown > 0;
		if(cooldown > 0)
			body["global_cooldown_seconds"] = cooldown;
		++*pending;
		auto create = [this, user, id, body, done, problem]() {
			twitch->Helix("POST", QStringLiteral("/channel_points/custom_rewards?broadcaster_id=%1").arg(user), body,
				[this, id, done, problem](int status, const QJsonObject &reply) {
					const QString reward = reply.value("data").toArray().first().toObject().value("id").toString();
					const int i = IndexOfId(id);
					if(status / 100 == 2 && !reward.isEmpty() && i >= 0)
					{
						QVariantMap m = list[i].toMap();
						m["reward"] = reward;
						list[i] = m;
						Save();
					}
					else if(status / 100 != 2)
						*problem = status == 403 ? tr("Twitch says no: channel point rewards need an Affiliate or Partner channel.")
							: tr("Twitch did not take a reward: %1").arg(reply.value("message").toString(QString::number(status)));
					done();
				});
		};
		if(reward.isEmpty())
		{
			create();
			continue;
		}
		twitch->Helix("PATCH", QStringLiteral("/channel_points/custom_rewards?broadcaster_id=%1&id=%2").arg(user, reward), body,
			[this, id, create, done, problem](int status, const QJsonObject &reply) {
				if(status == 404)
				{
					// Deleted on Twitch: made again
					const int i = IndexOfId(id);
					if(i >= 0)
					{
						QVariantMap m = list[i].toMap();
						m["reward"] = QString();
						list[i] = m;
					}
					create();
					return;
				}
				if(status / 100 != 2)
					*problem = tr("Twitch did not take a reward: %1").arg(reply.value("message").toString(QString::number(status)));
				done();
			});
	}
	if(*pending == 0)
		SetStatus(QString());
}

void ViewerActions::stopAll()
{
	for(const Job &j : queue)
		Finish(j, false);
	queue.clear();
	Release();
	last_text = tr("Stopped");
	setEnabled(false);
	emit changed();
}

void ViewerActions::tryAction(int index)
{
	if(index < 0 || index >= list.size())
		return;
	Enqueue(list[index].toMap(), tr("You"), QString(), QString(), true);
}

void ViewerActions::SetSession(StreamSession *s)
{
	if(session && session != s)
		Release();
	session = s;
	if(!session)
	{
		for(const Job &j : queue)
			Finish(j, false);
		queue.clear();
		pressing = false;
		emit changed();
	}
}

void ViewerActions::OnTwitchEvent(const QString &type, const QJsonObject &event)
{
	const QString who = event.value("user_name").toString(event.value("from_broadcaster_user_name").toString()).left(25);
	if(type == QLatin1String("channel.channel_points_custom_reward_redemption.add"))
	{
		const QString reward = event.value("reward").toObject().value("id").toString();
		for(const QVariant &v : list)
		{
			const QVariantMap a = v.toMap();
			if(!reward.isEmpty() && a.value("reward").toString() == reward)
			{
				Enqueue(a, who, reward, event.value("id").toString(), false);
				return;
			}
		}
		return; // a reward of somebody else's
	}
	QString trigger;
	int bits = 0;
	if(type == QLatin1String("channel.cheer"))
	{
		trigger = QStringLiteral("bits");
		bits = event.value("bits").toInt();
	}
	else if(type == QLatin1String("channel.subscribe") || type == QLatin1String("channel.subscription.message")
		|| type == QLatin1String("channel.subscription.gift"))
		trigger = QStringLiteral("sub");
	else if(type == QLatin1String("channel.follow"))
		trigger = QStringLiteral("follow");
	else if(type == QLatin1String("channel.raid"))
		trigger = QStringLiteral("raid");
	else
		return;
	// The action that fits best: for bits the one with the highest minimum reached
	QVariantMap best;
	for(const QVariant &v : list)
	{
		const QVariantMap a = v.toMap();
		if(a.value("trigger").toString() != trigger || !a.value("on").toBool())
			continue;
		if(trigger == QLatin1String("bits") && a.value("min").toInt() > bits)
			continue;
		if(best.isEmpty() || (trigger == QLatin1String("bits") && a.value("min").toInt() > best.value("min").toInt()))
			best = a;
	}
	if(best.isEmpty())
		return;
	const QString id = best.value("id").toString();
	const qint64 cooldown_ms = best.value("cooldown").toInt() * 1000LL;
	if(cooldown_ms > 0 && Now() - last_run_ms.value(id, 0) < cooldown_ms)
		return;
	Enqueue(best, who, QString(), QString(), false);
}

void ViewerActions::Enqueue(const QVariantMap &action, const QString &who, const QString &reward_id, const QString &redemption_id, bool test)
{
	Job job { action, who, reward_id, redemption_id };
	if(!test)
	{
		const qint64 now = Now();
		while(!accepted_ms.isEmpty() && now - accepted_ms.first() > 60000)
			accepted_ms.removeFirst();
		QString no;
		if(!on)
			no = tr("Viewer actions are off");
		else if(!action.value("on").toBool())
			no = tr("This action is off");
		else if(!session)
			no = tr("No game running");
		else if(accepted_ms.size() >= per_minute || queue.size() >= 20)
			no = tr("Too many at once");
		if(!no.isEmpty())
		{
			last_text = tr("%1: %2 (not run: %3)").arg(who, action.value("title").toString(), no);
			Finish(job, false);
			emit changed();
			return;
		}
		accepted_ms.append(now);
		last_run_ms[action.value("id").toString()] = now;
	}
	else if(!session)
	{
		last_text = tr("Start a game first to try it");
		emit changed();
		return;
	}
	queue.append(job);
	emit changed();
	Next();
}

void ViewerActions::Next()
{
	if(pressing || queue.isEmpty())
		return;
	const Job job = queue.takeFirst();
	if(!session)
	{
		Finish(job, false);
		Next();
		return;
	}
	ChiakiControllerState state;
	chiaki_controller_state_set_idle(&state);
	Apply(job.action.value("input").toString(), &state);
	session->SetViewerInput(state);
	pressing = true;
	last_text = tr("%1: %2").arg(job.who, job.action.value("title").toString());
	emit changed();
	QTimer::singleShot(job.action.value("hold").toInt(), this, [this, job]() {
		Release();
		Finish(job, true);
		// A short gap, so two presses of the same button stay two
		QTimer::singleShot(120, this, [this]() {
			pressing = false;
			emit changed();
			Next();
		});
	});
}

void ViewerActions::Release()
{
	if(!session)
		return;
	ChiakiControllerState idle;
	chiaki_controller_state_set_idle(&idle);
	session->SetViewerInput(idle);
}

void ViewerActions::Finish(const Job &job, bool ran)
{
	if(job.redemption_id.isEmpty() || job.reward_id.isEmpty() || twitch->UserId().isEmpty())
		return;
	// Done, or the points go back to the viewer
	QJsonObject body;
	body["status"] = ran ? QStringLiteral("FULFILLED") : QStringLiteral("CANCELED");
	twitch->Helix("PATCH", QStringLiteral("/channel_points/custom_rewards/redemptions?broadcaster_id=%1&reward_id=%2&id=%3")
		.arg(twitch->UserId(), job.reward_id, job.redemption_id), body, [](int, const QJsonObject &) {});
}
