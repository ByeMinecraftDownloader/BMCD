#include "InstalledVersions.h"

#include "platform/Paths.h"

#include <QDir>
#include <QFileInfo>

namespace {

QString resolveRoot()
{
    const QString override = qEnvironmentVariable("MC_TARGET");
    if (!override.isEmpty())
        return override;
    return platform::defaultGameDir();
}

} // namespace

InstalledVersions::InstalledVersions(QObject *parent)
    : QObject(parent)
{
    rescan();
}

QString InstalledVersions::root() const
{
    return resolveRoot();
}

void InstalledVersions::rescan()
{
    QSet<QString> ids;

    const QDir versionsDir(root() + QStringLiteral("/versions"));
    if (versionsDir.exists()) {
        const QStringList entries = versionsDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &name : entries) {
            // A version counts as installed once its json exists: that is the
            // file every launcher writes first.
            if (QFileInfo::exists(versionsDir.filePath(name + QLatin1Char('/') + name + QStringLiteral(".json"))))
                ids.insert(name);
        }
    }

    // Bumped on every scan, so QML bindings that call contains() re-evaluate
    // even when the set itself did not change (a fresh install rewrites files).
    ++m_revision;
    if (ids == m_ids) {
        emit changed();
        return;
    }
    m_ids = ids;
    emit changed();
}
