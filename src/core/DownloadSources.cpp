#include "DownloadSources.h"

#include "AppSettings.h"

#include <QFileInfo>
#include <QUrl>

namespace {

QString joinPath(const QString &basePath, const QString &path)
{
    QString left = basePath;
    while (left.endsWith(QLatin1Char('/')))
        left.chop(1);
    QString right = path;
    if (!right.startsWith(QLatin1Char('/')))
        right.prepend(QLatin1Char('/'));
    return left + right;
}

QString mappedUrl(const QString &original, const QString &base, const QString &kind,
                  const QString &versionId)
{
    const QUrl source(original);
    const QUrl root = QUrl(DownloadSources::normaliseBase(base));
    if (!source.isValid() || source.host().isEmpty() || root.host().isEmpty())
        return {};

    const QString host = source.host().toLower();
    const bool bmcl = root.host().contains(QLatin1String("bmclapi"), Qt::CaseInsensitive);
    QString path = source.path();
    if (bmcl) {
        // BMCLAPI uses Maven and asset roots, and cannot serve the object-store
        // path of the client jar, so those three shapes need a real mapping.
        if (host == QLatin1String("resources.download.minecraft.net"))
            path = QStringLiteral("/assets") + path;
        else if (host == QLatin1String("libraries.minecraft.net"))
            path = QStringLiteral("/maven") + path;
        else if (kind == QLatin1String("client") && !versionId.isEmpty()
                 && host.startsWith(QLatin1String("piston-")))
            path = QStringLiteral("/version/") + versionId + QStringLiteral("/client");
        else if (kind == QLatin1String("server") && !versionId.isEmpty()
                 && host.startsWith(QLatin1String("piston-")))
            path = QStringLiteral("/version/") + versionId + QStringLiteral("/server");
        // Asset indexes and version JSONs both live under /v1/packages and the
        // BMCLAPI mirror serves those paths unchanged.
    }

    QUrl out = root;
    out.setPath(joinPath(root.path(), path));
    out.setQuery(source.query());
    out.setFragment(source.fragment());
    return out.toString();
}

void appendUnique(QStringList &out, const QString &url)
{
    if (!url.isEmpty() && !out.contains(url))
        out.append(url);
}

} // namespace

QString DownloadSources::normaliseBase(const QString &base)
{
    QString text = base.trimmed();
    if (text.isEmpty())
        return {};

    if (!text.contains(QLatin1String("://")))
        text.prepend(QLatin1String("https://"));

    QUrl url(text);
    if (!url.isValid() || url.host().isEmpty()
        || (url.scheme() != QLatin1String("http") && url.scheme() != QLatin1String("https")))
        return {};

    QString path = url.path();
    while (path.endsWith(QLatin1Char('/')))
        path.chop(1);
    url.setPath(path);
    url.setQuery(QString());
    url.setFragment(QString());
    return url.toString();
}

bool DownloadSources::isMojangHost(const QString &host)
{
    const QString lower = host.toLower();
    return lower == QLatin1String("piston-meta.mojang.com")
            || lower == QLatin1String("piston-data.mojang.com")
            || lower == QLatin1String("launchermeta.mojang.com")
            || lower == QLatin1String("launcher.mojang.com")
            || lower == QLatin1String("libraries.minecraft.net")
            || lower == QLatin1String("resources.download.minecraft.net");
}

bool DownloadSources::isModrinthHost(const QString &host)
{
    const QString lower = host.toLower();
    return lower == QLatin1String("cdn.modrinth.com")
            || lower == QLatin1String("cdn-raw.modrinth.com");
}

QStringList DownloadSources::candidates(const QString &url, const QString &kind,
                                        const QString &versionId)
{
    QStringList out;
    if (url.isEmpty())
        return out;

    const QUrl source(url);
    if (!source.isValid() || source.host().isEmpty()) {
        out.append(url);
        return out;
    }

    const QString cdn = AppSettings::configuredCdnBase();
    const QString mirror = AppSettings::configuredMirrorBase();
    if (!cdn.isEmpty() && isModrinthHost(source.host()))
        appendUnique(out, mappedUrl(url, cdn, kind, versionId));
    if (!mirror.isEmpty() && isMojangHost(source.host()))
        appendUnique(out, mappedUrl(url, mirror, kind, versionId));

    appendUnique(out, url);
    if (!AppSettings::failoverEnabled() && out.size() > 1)
        out = {out.first()};
    return out;
}

QString DownloadSources::preferred(const QString &url, const QString &kind,
                                   const QString &versionId)
{
    const QStringList urls = candidates(url, kind, versionId);
    return urls.isEmpty() ? url : urls.first();
}
