#pragma once

#include "core/Pack.h"

#include <QString>
#include <QStringList>

// The three archive formats the launcher ecosystem actually ships:
//   * Modrinth  .mrpack / modrinth.index.json
//   * CurseForge .zip   / manifest.json (v1)
//   * FTB       .zip   / manifest.json (v1, targets[])
//
// Importing extracts the manifest + overrides into a staging directory and
// returns a Pack whose target is that staging root, so the rest of the app
// (serve, deploy, export, hashing) keeps working on a single model.
struct ImportResult
{
    bool ok = false;
    QString error;
    QString format;           // modrinth | curseforge | ftb
    Pack pack;
    QString stagingDir;
    QStringList unresolved;   // files with no download url (e.g. CF needs an API key)
    QStringList unsupported;  // entries skipped on purpose (server-only files, unsafe paths)
    int overrideCount = 0;
};

// `archivePath` is a .mrpack or .zip. Throws no exceptions; a failed import
// leaves the archive untouched and reports why.
ImportResult importPackArchive(const QString &archivePath);

// Stable staging directory for an archive (same archive -> same dir, so a
// failed import can be retried without piling up temp trees).
QString importStagingDir(const QString &archivePath);
