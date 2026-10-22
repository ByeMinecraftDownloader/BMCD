#pragma once

#include <QString>

// The minimum a launcher needs to recognise a BMCD-managed loader install.
struct ProfileSpec
{
    QString gameDir;            // .minecraft root, absolute
    QString minecraftVersion;   // e.g. "1.20.1"
    QString loaderType;         // fabric | quilt | forge | neoforge | vanilla
    QString loaderVersion;      // e.g. "0.15.11"
    QString lastVersionId;      // versions/<id>/<id>.json, required
    QString name;               // display name; generated when empty
    QString javaArgs;           // optional, used on first creation only
};

struct ProfileMergeResult
{
    bool ok = false;
    bool created = false;
    bool changed = false;
    QString id;
    QString name;
    QString path;
    QString backupPath;
    QString error;
};

// Reads and writes launcher_profiles.json without interpreting launcher-owned
// data. Unknown top-level keys, settings and other profiles are kept verbatim.
// A write is atomic (QSaveFile) and the previous file is copied to
// "launcher_profiles.json.bmcd-backup" first.
class ProfileMerger
{
public:
    static QString profileId(const QString &minecraftVersion,
                             const QString &loaderType,
                             const QString &loaderVersion);
    static QString defaultName(const QString &minecraftVersion,
                               const QString &loaderType);
    static ProfileMergeResult merge(const ProfileSpec &spec);
};
