#pragma once

#include "mods/ModResolver.h"

#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QString>

// Blocking Modrinth v2 client. Construct it inside the worker thread that needs
// it (QtConcurrent); it uses a nested event loop per request, like PackImporter
// does for CurseForge.
class ModrinthApi
{
public:
    ModrinthApi(QString loader, QString gameVersion);

    mods::ResolveResult resolve(const QVector<mods::Root> &roots);
    QVector<mods::Version> versions(const QString &project);
    mods::Project project(const QString &project);

private:
    QJsonDocument get(const QUrl &url, QString *error);

    QString m_loader;
    QString m_gameVersion;
    QNetworkAccessManager m_nam;
    mutable QHash<QString, QVector<mods::Version>> m_versions;
    mutable QHash<QString, mods::Project> m_projects;
};
