// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <coplaylinkpreview.h>

#include <QBuffer>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QRegularExpression>
#include <QTextDocumentFragment>
#include <QTimer>
#include <QUrlQuery>

#include <memory>

/** Everything together, the page and its picture, may take this long. */
static const int kPreviewTimeoutMs = 5000;
/** Of a web page only the beginning is read, where the meta tags are. */
static const qint64 kMaxPageBytes = 512 * 1024;
static const qint64 kMaxImageBytes = 4 * 1024 * 1024;
static const int kThumbWidth = 400;
static const int kThumbHeight = 225;

static QNetworkAccessManager *Network()
{
	static QNetworkAccessManager *nam = nullptr;
	if(!nam)
	{
		nam = new QNetworkAccessManager();
		// Never from https to http, and never anywhere but http(s)
		nam->setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
	}
	return nam;
}

static bool IsWebUrl(const QUrl &url)
{
	return url.isValid() && (url.scheme() == QLatin1String("https") || url.scheme() == QLatin1String("http"))
		&& !url.host().isEmpty();
}

QUrl CoPlayLinkPreview::FirstLink(const QString &text)
{
	static const QRegularExpression link(QStringLiteral("https?://[^\\s<>\"']+"),
		QRegularExpression::CaseInsensitiveOption);
	QRegularExpressionMatch m = link.match(text);
	if(!m.hasMatch())
		return QUrl();
	QString s = m.captured(0);
	// A full stop or bracket right after a link belongs to the sentence
	while(!s.isEmpty() && QStringLiteral(".,;:!?)]}").contains(s.back()))
		s.chop(1);
	QUrl url(s, QUrl::StrictMode);
	return IsWebUrl(url) ? url : QUrl();
}

QByteArray CoPlayLinkPreview::Thumbnail(const QByteArray &image_file)
{
	QImage image = QImage::fromData(image_file);
	if(image.isNull())
		return QByteArray();
	if(image.width() > kThumbWidth || image.height() > kThumbHeight)
		image = image.scaled(kThumbWidth, kThumbHeight, Qt::KeepAspectRatio, Qt::SmoothTransformation);
	image = image.convertToFormat(QImage::Format_RGB888);
	for(int attempt = 0; attempt < 8; attempt++)
	{
		for(int quality = 80; quality >= 40; quality -= 10)
		{
			QByteArray jpeg;
			QBuffer buffer(&jpeg);
			buffer.open(QIODevice::WriteOnly);
			if(!image.save(&buffer, "JPEG", quality))
				return QByteArray();
			if(static_cast<size_t>(jpeg.size()) <= coplay::kMaxChatImageSize)
				return jpeg;
		}
		image = image.scaled(image.width() * 3 / 4, image.height() * 3 / 4, Qt::KeepAspectRatio, Qt::SmoothTransformation);
	}
	return QByteArray();
}

static QString PlainText(const QString &html)
{
	return QTextDocumentFragment::fromHtml(html).toPlainText().simplified();
}

/** The value of an attribute of one HTML tag. */
static QString Attribute(const QString &tag, const QString &name)
{
	QRegularExpression re(QStringLiteral("\\b%1\\s*=\\s*(\"([^\"]*)\"|'([^']*)'|([^\\s>]+))").arg(name),
		QRegularExpression::CaseInsensitiveOption);
	QRegularExpressionMatch m = re.match(tag);
	if(!m.hasMatch())
		return QString();
	for(int i = 2; i <= 4; i++)
	{
		if(m.capturedLength(i) > 0)
			return PlainText(m.captured(i));
	}
	return QString();
}

namespace {

/** One preview being made. Lives until done, or until the time is up. */
struct Job
{
	QPointer<QObject> context;
	CoPlayLinkPreview::Done done;
	QUrl url;
	coplay::ChatEmbed embed;
	QPointer<QNetworkReply> reply;
	QTimer *timer = nullptr;
	bool finished = false;

	void Finish(const QByteArray &image)
	{
		if(finished)
			return;
		finished = true;
		if(timer)
			timer->deleteLater();
		if(reply)
			reply->abort();
		if(context && done)
			done(embed, image.isEmpty() ? QByteArray() : CoPlayLinkPreview::Thumbnail(image));
	}
};

}

/** Reads at most limit bytes and calls back with what arrived, or with nothing on an error. */
static void Get(const std::shared_ptr<Job> &job, const QUrl &url, qint64 limit,
		std::function<void(const QByteArray &body, const QString &content_type)> then)
{
	if(job->finished)
		return;
	QNetworkRequest request(url);
	request.setHeader(QNetworkRequest::UserAgentHeader,
		QStringLiteral("Mozilla/5.0 (compatible; PSRemotePlayTogether link preview)"));
	request.setRawHeader("Accept-Language", "en, *;q=0.5");
	request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
	request.setMaximumRedirectsAllowed(5);
	QNetworkReply *reply = Network()->get(request);
	job->reply = reply;
	auto body = std::make_shared<QByteArray>();
	QObject::connect(reply, &QNetworkReply::readyRead, reply, [reply, body, limit]() {
		body->append(reply->read(limit - body->size()));
		if(body->size() >= limit)
			reply->abort(); // enough of it; finished() follows with what we have
	});
	QObject::connect(reply, &QNetworkReply::finished, reply, [job, reply, body, limit, then]() {
		reply->deleteLater();
		if(job->finished)
			return;
		if(body->size() < limit)
			body->append(reply->read(limit - body->size()));
		bool cut_short = body->size() >= limit;
		if(reply->error() != QNetworkReply::NoError && !(cut_short && reply->error() == QNetworkReply::OperationCanceledError))
		{
			then(QByteArray(), QString());
			return;
		}
		then(*body, reply->header(QNetworkRequest::ContentTypeHeader).toString().toLower());
	});
}

static void FetchImage(const std::shared_ptr<Job> &job, const QUrl &image_url)
{
	if(!IsWebUrl(image_url))
	{
		job->Finish(QByteArray());
		return;
	}
	Get(job, image_url, kMaxImageBytes, [job](const QByteArray &body, const QString &) {
		job->Finish(body);
	});
}

static bool IsYouTube(const QUrl &url)
{
	QString host = url.host().toLower();
	return host == QLatin1String("youtu.be") || host == QLatin1String("youtube.com") || host.endsWith(QLatin1String(".youtube.com"));
}

void CoPlayLinkPreview::Fetch(QObject *context, const QUrl &url, Done done)
{
	auto job = std::make_shared<Job>();
	job->context = context;
	job->done = std::move(done);
	job->url = url;
	job->embed.url = url.toString(QUrl::FullyEncoded).toStdString();
	job->embed.site = url.host().toStdString();
	if(job->embed.url.size() > coplay::kMaxChatUrl)
	{
		// Too long to pass on as it is: no preview, the link stays in the text
		job->embed.url.clear();
		job->Finish(QByteArray());
		return;
	}

	job->timer = new QTimer();
	job->timer->setSingleShot(true);
	QObject::connect(job->timer, &QTimer::timeout, job->timer, [job]() {
		// Whatever was found so far is shown, without a picture
		job->Finish(QByteArray());
	});
	job->timer->start(kPreviewTimeoutMs);

	if(IsYouTube(url))
	{
		// YouTube's own pages ask for consent first in many countries; its oEmbed answer does not
		QUrl oembed(QStringLiteral("https://www.youtube.com/oembed"));
		QUrlQuery query;
		query.addQueryItem(QStringLiteral("url"), url.toString(QUrl::FullyEncoded));
		query.addQueryItem(QStringLiteral("format"), QStringLiteral("json"));
		oembed.setQuery(query);
		Get(job, oembed, 64 * 1024, [job](const QByteArray &body, const QString &) {
			QJsonObject o = QJsonDocument::fromJson(body).object();
			job->embed.site = "YouTube";
			job->embed.title = coplay::TruncateUtf8(o.value(QStringLiteral("title")).toString().toStdString(), coplay::kMaxChatTitle);
			job->embed.description = coplay::TruncateUtf8(o.value(QStringLiteral("author_name")).toString().toStdString(), coplay::kMaxChatDescription);
			FetchImage(job, QUrl(o.value(QStringLiteral("thumbnail_url")).toString()));
		});
		return;
	}

	Get(job, url, kMaxPageBytes, [job](const QByteArray &body, const QString &content_type) {
		if(body.isEmpty())
		{
			job->Finish(QByteArray());
			return;
		}
		if(content_type.startsWith(QLatin1String("image/")))
		{
			// A picture itself: it is the preview. Read the rest of it.
			QString name = job->url.fileName();
			job->embed.title = coplay::TruncateUtf8(name.toStdString(), coplay::kMaxChatTitle);
			FetchImage(job, job->url);
			return;
		}
		if(!content_type.isEmpty() && !content_type.contains(QLatin1String("html")))
		{
			job->Finish(QByteArray());
			return;
		}

		QString html = QString::fromUtf8(body);
		QString title, description, site, image;
		static const QRegularExpression meta(QStringLiteral("<meta\\b[^>]*>"), QRegularExpression::CaseInsensitiveOption);
		auto it = meta.globalMatch(html);
		while(it.hasNext())
		{
			QString tag = it.next().captured(0);
			QString key = Attribute(tag, QStringLiteral("property"));
			if(key.isEmpty())
				key = Attribute(tag, QStringLiteral("name"));
			key = key.toLower();
			QString content = Attribute(tag, QStringLiteral("content"));
			if(content.isEmpty())
				continue;
			if((key == QLatin1String("og:title") || (key == QLatin1String("twitter:title") && title.isEmpty())))
				title = content;
			else if(key == QLatin1String("og:description") || ((key == QLatin1String("twitter:description") || key == QLatin1String("description")) && description.isEmpty()))
				description = content;
			else if(key == QLatin1String("og:site_name"))
				site = content;
			else if(key == QLatin1String("og:image") || key == QLatin1String("og:image:url") || (key == QLatin1String("twitter:image") && image.isEmpty()))
				image = content;
		}
		if(title.isEmpty())
		{
			static const QRegularExpression title_tag(QStringLiteral("<title[^>]*>([^<]*)</title>"), QRegularExpression::CaseInsensitiveOption);
			QRegularExpressionMatch m = title_tag.match(html);
			if(m.hasMatch())
				title = PlainText(m.captured(1));
		}
		if(title.isEmpty() && description.isEmpty() && image.isEmpty())
		{
			// Nothing worth a preview; the link in the text is enough
			job->embed.url.clear();
			job->Finish(QByteArray());
			return;
		}
		if(!site.isEmpty())
			job->embed.site = coplay::TruncateUtf8(site.toStdString(), coplay::kMaxChatSite);
		job->embed.title = coplay::TruncateUtf8(title.toStdString(), coplay::kMaxChatTitle);
		job->embed.description = coplay::TruncateUtf8(description.toStdString(), coplay::kMaxChatDescription);
		if(image.isEmpty())
			job->Finish(QByteArray());
		else
			FetchImage(job, job->url.resolved(QUrl(image)));
	});
}
