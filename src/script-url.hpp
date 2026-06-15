#pragma once

#include <QNetworkRequest>
#include <QStringList>
#include <QUrl>

inline QString TelePTextUserAgent()
{
	return QStringLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
			      "(KHTML, like Gecko) Chrome/125.0.0.0 Safari/537.36 OBS-TeleP/0.1");
}

inline QUrl TelePTextUrlFromInput(const QString &input)
{
	QUrl url = QUrl::fromUserInput(input.trimmed());
	if (!url.isValid())
		return url;

	const QString path = url.path();
	if (path.contains(QStringLiteral("/p/")) && !path.endsWith(QStringLiteral("/export/txt"))) {
		QString normalized = path;
		while (normalized.endsWith('/'))
			normalized.chop(1);
		normalized += QStringLiteral("/export/txt");
		url.setPath(normalized);
		url.setQuery(QString());
	}

	return url;
}

inline QNetworkRequest TelePTextRequest(const QUrl &url)
{
	QNetworkRequest request(url);
	request.setHeader(QNetworkRequest::UserAgentHeader, TelePTextUserAgent());
	request.setRawHeader("Accept", "text/plain,text/*,*/*");
	request.setRawHeader("Accept-Language", "en-US,en;q=0.9,el;q=0.8");
	request.setRawHeader("Cache-Control", "no-cache");
	request.setRawHeader("Pragma", "no-cache");
	request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
	request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
	return request;
}

inline QStringList TelePCurlTextArguments(const QUrl &url)
{
	return QStringList{
		QStringLiteral("-L"),
		QStringLiteral("-sS"),
		QStringLiteral("--fail"),
		QStringLiteral("-A"),
		TelePTextUserAgent(),
		QStringLiteral("-H"),
		QStringLiteral("Accept: text/plain,text/*,*/*"),
		QStringLiteral("-H"),
		QStringLiteral("Accept-Language: en-US,en;q=0.9,el;q=0.8"),
		url.toString()
	};
}

inline QString TelePTitleFromUrl(const QUrl &url)
{
	QString path = url.path();
	if (path.endsWith(QStringLiteral("/export/txt")))
		path.chop(QStringLiteral("/export/txt").size());
	const QString title = path.section('/', -1).trimmed();
	return title.isEmpty() ? url.host() : QUrl::fromPercentEncoding(title.toUtf8());
}
