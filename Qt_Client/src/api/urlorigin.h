#pragma once

#include <QUrl>

namespace UrlOrigin {

inline int effectivePort(const QUrl &url)
{
    if (url.port() >= 0) {
        return url.port();
    }
    return url.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) == 0
        ? 443 : 80;
}

inline QUrl normalizedHttpOrigin(const QUrl &url)
{
    if (!url.isValid() || url.host().isEmpty()) {
        return {};
    }

    const QString scheme = url.scheme().toLower();
    if (scheme != QStringLiteral("https")
        && scheme != QStringLiteral("http")) {
        return {};
    }

    QUrl origin;
    origin.setScheme(scheme);
    origin.setHost(url.host().toLower());
    origin.setPort(effectivePort(url));
    return origin;
}

inline bool sameHttpOrigin(const QUrl &left, const QUrl &right)
{
    const QUrl normalizedLeft = normalizedHttpOrigin(left);
    const QUrl normalizedRight = normalizedHttpOrigin(right);
    return !normalizedLeft.isEmpty()
        && !normalizedRight.isEmpty()
        && normalizedLeft.scheme() == normalizedRight.scheme()
        && normalizedLeft.host() == normalizedRight.host()
        && normalizedLeft.port() == normalizedRight.port();
}

} // namespace UrlOrigin
