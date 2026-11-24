// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef COPLAYLINKPREVIEW_H
#define COPLAYLINKPREVIEW_H

#include <coplay/protocol.h>

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QUrl>

#include <functional>

/**
 * Makes the preview of a link for a chat message: title, site, a short
 * description and a small picture. Only the writer's app looks at the link,
 * once, and sends what it found along with the message, so that the others
 * do not each contact a site they never chose to visit.
 */
class CoPlayLinkPreview : public QObject
{
	Q_OBJECT

	public:
		using Done = std::function<void(const coplay::ChatEmbed &embed, const QByteArray &image)>;

		/**
		 * Looks at the link, for a few seconds at most, and answers on this thread.
		 * done gets an embed whose url is empty if there was nothing to show; it is
		 * not called if context is gone by then.
		 */
		static void Fetch(QObject *context, const QUrl &url, Done done);

		/** The first http(s) link in a text, or an empty URL. */
		static QUrl FirstLink(const QString &text);

		/** A JPEG of at most max_bytes (a session's chat: coplay::kMaxChatImageSize) for the preview, or empty. */
		static QByteArray Thumbnail(const QByteArray &image_file, size_t max_bytes = coplay::kMaxChatImageSize);
};

#endif // COPLAYLINKPREVIEW_H
