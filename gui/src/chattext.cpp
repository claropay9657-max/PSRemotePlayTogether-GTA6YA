// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Chat text as the app shows it: in a session's chat and in the friends' chats.

#include "chattext.h"

#include <QList>
#include <QPair>
#include <QRegularExpression>

namespace chattext {

/** The text as rich text for a Text item: escaped, links clickable, line breaks kept. */
/** How long the mention of one of names at text[at] ('@') is, with the '@'; 0 if none. */
static qsizetype MentionAt(const QString &text, qsizetype at, const QStringList &names)
{
	if(text.at(at) != QLatin1Char('@') || (at > 0 && text.at(at - 1).isLetterOrNumber()))
		return 0;
	qsizetype best = 0;
	for(const QString &name : names)
	{
		const qsizetype end = at + 1 + name.size();
		if(name.isEmpty() || end > text.size() || name.size() + 1 <= best)
			continue;
		if(QStringView(text).mid(at + 1, name.size()).compare(name, Qt::CaseInsensitive) != 0)
			continue;
		if(end < text.size() && text.at(end).isLetterOrNumber())
			continue;
		best = name.size() + 1;
	}
	return best;
}

bool Mentions(const QString &text, const QString &name)
{
	const QString who = name.trimmed();
	if(who.isEmpty())
		return false;
	for(qsizetype at = text.indexOf(QLatin1Char('@')); at >= 0; at = text.indexOf(QLatin1Char('@'), at + 1))
	{
		if(MentionAt(text, at, { who }))
			return true;
	}
	return false;
}

/** Plain text (no links in it) escaped, with the mentions of names drawn as such. */
static QString PlainHtml(const QString &text, const QStringList &names)
{
	if(names.isEmpty())
		return text.toHtmlEscaped();
	QString out;
	qsizetype last = 0;
	for(qsizetype at = text.indexOf(QLatin1Char('@')); at >= 0; at = text.indexOf(QLatin1Char('@'), at + 1))
	{
		const qsizetype n = MentionAt(text, at, names);
		if(!n)
			continue;
		out += text.mid(last, at - last).toHtmlEscaped();
		// What a Text item's StyledText knows (it has no <span style>)
		out += QStringLiteral("<font color=\"#b18cff\"><b>%1</b></font>").arg(text.mid(at, n).toHtmlEscaped());
		last = at + n;
		at = last - 1;
	}
	out += text.mid(last).toHtmlEscaped();
	return out;
}

QString Html(const QString &text, const QStringList &names)
{
	static const QRegularExpression link(QStringLiteral("https?://[^\\s<>\"']+"), QRegularExpression::CaseInsensitiveOption);
	QStringList who;
	for(const QString &name : names)
	{
		if(!name.trimmed().isEmpty())
			who.append(name.trimmed());
	}
	QString out;
	qsizetype last = 0;
	auto it = link.globalMatch(text);
	while(it.hasNext())
	{
		QRegularExpressionMatch m = it.next();
		QString url = m.captured(0);
		while(!url.isEmpty() && QStringLiteral(".,;:!?)]}").contains(url.back()))
			url.chop(1);
		out += PlainHtml(text.mid(last, m.capturedStart(0) - last), who);
		out += QStringLiteral("<a href=\"%1\">%2</a>").arg(url.toHtmlEscaped(), url.toHtmlEscaped());
		last = m.capturedStart(0) + url.size();
	}
	out += PlainHtml(text.mid(last), who);
	out.replace(QLatin1Char('\n'), QStringLiteral("<br>"));
	return out;
}

/** :shortcodes: and a few faces typed as text become emoji. */
QString WithEmoji(QString text)
{
	static const QList<QPair<QString, QString>> codes = {
		{ QStringLiteral(":smile:"), QStringLiteral("😄") }, { QStringLiteral(":joy:"), QStringLiteral("😂") },
		{ QStringLiteral(":wink:"), QStringLiteral("😉") }, { QStringLiteral(":heart:"), QStringLiteral("❤️") },
		{ QStringLiteral(":fire:"), QStringLiteral("🔥") }, { QStringLiteral(":thumbsup:"), QStringLiteral("👍") },
		{ QStringLiteral(":+1:"), QStringLiteral("👍") }, { QStringLiteral(":thumbsdown:"), QStringLiteral("👎") },
		{ QStringLiteral(":clap:"), QStringLiteral("👏") }, { QStringLiteral(":gg:"), QStringLiteral("🎮") },
		{ QStringLiteral(":skull:"), QStringLiteral("💀") }, { QStringLiteral(":eyes:"), QStringLiteral("👀") },
		{ QStringLiteral(":cry:"), QStringLiteral("😢") }, { QStringLiteral(":rage:"), QStringLiteral("😡") },
		{ QStringLiteral(":party:"), QStringLiteral("🥳") }, { QStringLiteral(":trophy:"), QStringLiteral("🏆") },
		{ QStringLiteral(":100:"), QStringLiteral("💯") }, { QStringLiteral(":rocket:"), QStringLiteral("🚀") },
		{ QStringLiteral(":ok:"), QStringLiteral("👌") }, { QStringLiteral(":wave:"), QStringLiteral("👋") },
		{ QStringLiteral(":thinking:"), QStringLiteral("🤔") }, { QStringLiteral(":sweat:"), QStringLiteral("😅") },
		{ QStringLiteral(":cool:"), QStringLiteral("😎") }, { QStringLiteral(":pray:"), QStringLiteral("🙏") },
	};
	for(const auto &c : codes)
		text.replace(c.first, c.second, Qt::CaseInsensitive);
	// Faces only where they stand on their own, so that links and times stay as they are
	static const QList<QPair<QRegularExpression, QString>> faces = {
		{ QRegularExpression(QStringLiteral("(^|\\s):-?\\)(?=\\s|$)")), QStringLiteral("\\1🙂") },
		{ QRegularExpression(QStringLiteral("(^|\\s):-?D(?=\\s|$)")), QStringLiteral("\\1😄") },
		{ QRegularExpression(QStringLiteral("(^|\\s);-?\\)(?=\\s|$)")), QStringLiteral("\\1😉") },
		{ QRegularExpression(QStringLiteral("(^|\\s):-?\\((?=\\s|$)")), QStringLiteral("\\1🙁") },
		{ QRegularExpression(QStringLiteral("(^|\\s):-?[pP](?=\\s|$)")), QStringLiteral("\\1😛") },
		{ QRegularExpression(QStringLiteral("(^|\\s)<3(?=\\s|$)")), QStringLiteral("\\1❤️") },
		{ QRegularExpression(QStringLiteral("(^|\\s)[xX]D(?=\\s|$)")), QStringLiteral("\\1😆") },
	};
	for(const auto &f : faces)
		text.replace(f.first, f.second);
	return text;
}

} // namespace chattext
