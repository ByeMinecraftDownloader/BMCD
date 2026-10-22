#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

// One downloadable artifact inside a pack. Mirrors the union of the fields the
// three pack formats need: Modrinth wants hashes + env, CurseForge wants
// project/file ids, FTB wants size + side flags.
struct PackFile
{
    QString path;          // relative to the game directory, e.g. "mods/sodium.jar"
    QString url;
    QString sha1;
    QString sha256;
    QString sha512;
    qint64 size = 0;
    QString kind;          // mod | config | resource | shader | script | lib
    bool optional = false;
    bool clientSide = true;
    bool serverSide = true;
    bool selected = true;   // user picked this file for the plan
    bool enabled = true;    // effective after dependency/side resolution

    // Licence metadata (from the project API) — drives the self-contained gate.
    QString licenseId;
    QString licenseName;
    QString licenseUrl;
    QString sourceUrl;
    QString author;
    QString projectId;
    QString projectName;   // display title; used to group licence confirmations
    QString fileId;
};

// A resolved pack: the plan the UI shows on the serve screen and the single
// input every renderer/deployer consumes. JSON is the interchange format with
// qml (and with scripts / CI fixtures).
struct Pack
{
    int format = 1;
    QString name;
    QString versionId;       // pack version, e.g. "1.0.0"
    QString minecraftVersion;
    QString loaderType;      // fabric | quilt | neoforge | forge | vanilla
    QString loaderVersion;
    QString target;          // source root for deploy/self-contained export
    QString description;
    QString iconUrl;
    QString generator;
    QVector<PackFile> files;
    // Imported packs carry a second tree: files that shipped inside the archive
    // (config/, resourcepacks/, ...) instead of being downloaded from a CDN.
    // They are not part of files[] because they have no URL or licence.
    QString overridesDir;
    QStringList overrides;

    QJsonObject toJson() const;
    static Pack fromJson(const QJsonObject &object);

    bool isValid(QString *error = nullptr) const;

    // Derived numbers for the serve screen.
    qint64 totalBytes() const;
    int fileCount() const;

    // Files that make sense in a mrpack/FTB export (pack content only, not the
    // Minecraft client, libraries or assets).
    QVector<PackFile> contentFiles() const;
};

// Relative path safety shared by importers and the deployer: rejects absolute
// paths, drive letters, "..", empty segments and control characters.
bool isSafeRelativePath(const QString &path);
