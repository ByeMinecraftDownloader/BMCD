#pragma once

#include <QDateTime>
#include <QList>
#include <QString>

// One downloadable file. "path" is relative to the target root and mirrors the
// layout real launchers use (versions/<id>/<id>.jar, libraries/..., assets/...),
// so that a downloaded tree can be dropped straight into .minecraft.
struct PlanFile
{
    QString path;
    QString url;
    QString sha1;
    QString sha256;
    QString sha512;
    qint64 size = 0;
    QString kind; // client / library / asset / asset-index
};

struct TaskPlan
{
    QString id;
    QString type;
    QString assetIndexId;
    QDateTime releaseTime;
    int javaMajor = 0;
    int assetCount = 0;
    qint64 totalBytes = 0;
    QList<PlanFile> files;

    bool isValid() const { return !files.isEmpty(); }
};
