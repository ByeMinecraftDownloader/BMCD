#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

// Pure, network-free dependency resolution for "pick your own mods".
// ModrinthApi feeds it real data; tests feed it fixtures. This separation is
// deliberate: dependency graphs and conflict rules are where launchers break,
// so they must be testable without a network or a UI.
namespace mods {

struct File
{
    QString filename;
    QString url;
    QString sha1;
    QString sha512;
    qint64 size = 0;
    bool primary = false;
};

struct Dependency
{
    QString projectId;
    QString versionId;
    QString type; // required | optional | incompatible | embedded
};

struct Version
{
    QString id;
    QString projectId;
    QString name;
    QString versionNumber;
    QString datePublished;
    QStringList loaders;
    QStringList gameVersions;
    QVector<File> files;
    QVector<Dependency> dependencies;
};

struct Project
{
    QString id;
    QString slug;
    QString title;
    QString description;
    QString author;
    QString iconUrl;
    QString licenseId;
    QString licenseName;
    QString licenseUrl;
};

struct Root
{
    QString project;   // slug or project id
    QString versionId; // empty = newest compatible
};

struct ResolveRequest
{
    QString loader;
    QString gameVersion;
    QVector<Root> roots;
};

struct Conflict
{
    QString kind; // missing | incompatible | version | file
    QString message;
    QString projectId;
};

struct ResolveResult
{
    bool ok = false;
    QString error;
    QStringList warnings;
    QVector<Version> versions;
    QVector<Project> projects;
    // Dependencies the resolver added on the user's behalf. The UI must ask
    // before those are written into a download plan.
    QStringList addedDependencies;
    QVector<Conflict> conflicts;
};

using VersionsFn = std::function<QVector<Version>(const QString &project)>;
using ProjectFn = std::function<Project(const QString &project)>;

const File *primaryFile(const Version &version);
QString fileNameFor(const Version &version);

ResolveResult resolve(const ResolveRequest &request, const VersionsFn &versionsFor,
                      const ProjectFn &projectFor);

} // namespace mods
