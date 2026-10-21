#pragma once

#include "core/Pack.h"

#include <QString>
#include <QStringList>
#include <QVector>

// A game directory the user already owns: vanilla .minecraft, a MultiMC /
// Prism instance, or a folder BMCD deployed into earlier. The import flow lets
// the user pick one as the deploy target instead of typing a path.
struct LocalInstance
{
    QString name;
    QString path;
    QString minecraftVersion;
    QString loaderType;
    QString loaderVersion;
    int modCount = 0;
    QString source;   // bmcd | multimc | directory
};

// Looks one level below `root` (and at `root` itself) for directories that look
// like a game/instance. Unreadable folders are skipped, never fatal.
QVector<LocalInstance> scanInstances(const QString &root);

// Default root to scan when the user has not configured one: the official
// launcher's directory on this platform, plus the standard MultiMC location.
QStringList defaultInstanceRoots();

// "My Pack 1.0!" -> "My-Pack-1.0" (safe as a single directory name).
QString sanitizedInstanceName(const QString &packName);
