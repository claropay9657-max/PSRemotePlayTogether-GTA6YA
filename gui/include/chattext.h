// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_CHATTEXT_H
#define CHIAKI_CHATTEXT_H

#include <QString>
#include <QStringList>

/** Chat text as the app shows it, the same in a session's chat and in the friends' chats. */
namespace chattext {

/**
 * The text as rich text for a Text item: escaped, links clickable, line breaks kept, and "@name"
 * of one of names drawn as a mention (the accent colour, bold).
 */
QString Html(const QString &text, const QStringList &names = QStringList());
/**
 * Whether text mentions name: "@" and the name, any case, at the start or after a space or
 * punctuation, and not followed by a letter or digit.
 */
bool Mentions(const QString &text, const QString &name);
/** :shortcodes: and a few faces typed as text become emoji. */
QString WithEmoji(QString text);

} // namespace chattext

#endif // CHIAKI_CHATTEXT_H
