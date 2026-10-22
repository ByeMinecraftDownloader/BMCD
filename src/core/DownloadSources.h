#pragma once

#include <QString>
#include <QStringList>

// Resolves the configured CDN / Mojang mirror into concrete download URLs.
// Empty settings mean "use the official source"; with failover enabled the
// original URL is kept as a fallback after the configured primary.
namespace DownloadSources {

QString normaliseBase(const QString &base);
QString preferred(const QString &url, const QString &kind = {}, const QString &versionId = {});
QStringList candidates(const QString &url, const QString &kind = {}, const QString &versionId = {});

bool isMojangHost(const QString &host);
bool isModrinthHost(const QString &host);

} // namespace DownloadSources
