#include "mods/ModResolver.h"

#include <QFileInfo>
#include <QHash>
#include <QMap>
#include <QQueue>
#include <QSet>

#include <optional>

namespace mods {
namespace {

bool compatible(const Version &version, const QString &loader, const QString &gameVersion)
{
    if (!version.loaders.isEmpty() && !version.loaders.contains(loader))
        return false;
    if (!version.gameVersions.isEmpty() && !version.gameVersions.contains(gameVersion))
        return false;
    return true;
}

QString targetName(const Version &version)
{
    const File *file = primaryFile(version);
    if (!file)
        return {};
    return QStringLiteral("mods/") + QFileInfo(file->filename).fileName();
}

} // namespace

const File *primaryFile(const Version &version)
{
    for (const File &file : version.files) {
        if (file.primary)
            return &file;
    }
    return version.files.isEmpty() ? nullptr : &version.files.first();
}

QString fileNameFor(const Version &version)
{
    const File *file = primaryFile(version);
    return file ? QFileInfo(file->filename).fileName() : QString();
}

ResolveResult resolve(const ResolveRequest &request, const VersionsFn &versionsFor,
                      const ProjectFn &projectFor)
{
    ResolveResult result;
    if (request.loader.isEmpty() || request.gameVersion.isEmpty()) {
        result.error = QStringLiteral("缺少加载器或游戏版本");
        return result;
    }
    if (request.roots.isEmpty()) {
        result.error = QStringLiteral("没有选择任何模组");
        return result;
    }

    QHash<QString, QVector<Version>> cache;
    auto candidates = [&](const QString &project) -> const QVector<Version> & {
        if (!cache.contains(project))
            cache.insert(project, versionsFor(project));
        return cache[project];
    };

    auto choose = [&](const QString &project, const QString &pinned) -> std::optional<Version> {
        const QVector<Version> &all = candidates(project);
        std::optional<Version> best;
        if (!pinned.isEmpty()) {
            for (const Version &version : all) {
                if (version.id == pinned) {
                    if (!compatible(version, request.loader, request.gameVersion))
                        return std::nullopt;
                    return version;
                }
            }
            return std::nullopt;
        }
        for (const Version &version : all) {
            if (!compatible(version, request.loader, request.gameVersion))
                continue;
            if (version.files.isEmpty())
                continue;
            if (!best || version.datePublished > best->datePublished)
                best = version;
        }
        return best;
    };

    QMap<QString, Version> selected; // canonical project id -> version
    QSet<QString> forbidden;
    QSet<QString> roots;
    QQueue<QString> queue; // project ids whose dependencies still need walking

    auto fail = [&](const QString &kind, const QString &message,
                    const QString &projectId = {}) -> bool {
        Conflict conflict;
        conflict.kind = kind;
        conflict.message = message;
        conflict.projectId = projectId;
        result.conflicts.append(conflict);
        result.error = message;
        return false;
    };

    auto select = [&](const Version &version, const QString &requested) -> bool {
        if (forbidden.contains(version.projectId)) {
            return fail(QStringLiteral("incompatible"),
                        QStringLiteral("%1 与已选模组不兼容").arg(version.name),
                        version.projectId);
        }
        const auto existing = selected.constFind(version.projectId);
        if (existing != selected.constEnd()) {
            if (existing->id != version.id) {
                return fail(QStringLiteral("version"),
                            QStringLiteral("依赖要求 %1 的另一个版本").arg(version.projectId),
                            version.projectId);
            }
            return true;
        }
        if (version.files.isEmpty()) {
            return fail(QStringLiteral("missing"),
                        QStringLiteral("%1 没有可下载文件").arg(requested),
                        version.projectId);
        }
        selected.insert(version.projectId, version);
        queue.enqueue(version.projectId);
        return true;
    };

    for (const Root &root : request.roots) {
        const auto version = choose(root.project, root.versionId);
        if (!version) {
            fail(QStringLiteral("missing"),
                 QStringLiteral("没有适用于 %1 %2 的版本：%3")
                         .arg(request.loader, request.gameVersion, root.project),
                 root.project);
            return result;
        }
        if (!select(*version, root.project))
            return result;
        roots.insert(version->projectId);
    }

    while (!queue.isEmpty()) {
        const QString project = queue.dequeue();
        if (!selected.contains(project))
            continue;
        const Version version = selected.value(project);
        for (const Dependency &dependency : version.dependencies) {
            if (dependency.type == QLatin1String("embedded")) {
                result.warnings.append(QStringLiteral("%1 的内置依赖已包含在文件中").arg(version.name));
                continue;
            }
            if (dependency.type == QLatin1String("optional"))
                continue;
            if (dependency.type == QLatin1String("incompatible")) {
                forbidden.insert(dependency.projectId);
                if (selected.contains(dependency.projectId)) {
                    fail(QStringLiteral("incompatible"),
                         QStringLiteral("%1 与已选模组不兼容")
                                 .arg(selected.value(dependency.projectId).name),
                         dependency.projectId);
                    return result;
                }
                continue;
            }

            if (forbidden.contains(dependency.projectId)) {
                fail(QStringLiteral("incompatible"),
                     QStringLiteral("依赖 %1 与已选模组不兼容").arg(dependency.projectId),
                     dependency.projectId);
                return result;
            }
            const auto existing = selected.constFind(dependency.projectId);
            if (existing != selected.constEnd()) {
                if (!dependency.versionId.isEmpty() && existing->id != dependency.versionId) {
                    fail(QStringLiteral("version"),
                         QStringLiteral("依赖要求 %1 的另一个版本").arg(dependency.projectId),
                         dependency.projectId);
                    return result;
                }
                continue;
            }

            const auto dependencyVersion = choose(dependency.projectId, dependency.versionId);
            if (!dependencyVersion) {
                fail(QStringLiteral("missing"),
                     QStringLiteral("缺少依赖：%1（%2 %3）")
                             .arg(dependency.projectId, request.loader, request.gameVersion),
                     dependency.projectId);
                return result;
            }
            const bool wasSelected = selected.contains(dependency.projectId);
            if (!select(*dependencyVersion, dependency.projectId))
                return result;
            if (!wasSelected && !roots.contains(dependency.projectId))
                result.addedDependencies.append(dependencyVersion->projectId);
        }
    }

    QMap<QString, QString> paths;
    for (const Version &version : selected) {
        const QString path = targetName(version);
        if (path.isEmpty()) {
            result.error = QStringLiteral("%1 没有可下载文件").arg(version.name);
            return result;
        }
        if (paths.contains(path)) {
            fail(QStringLiteral("file"),
                 QStringLiteral("文件冲突：%1 同时由多个模组提供").arg(path));
            return result;
        }
        paths.insert(path, version.projectId);
    }

    for (auto it = selected.constBegin(); it != selected.constEnd(); ++it) {
        result.versions.append(it.value());
        const Project project = projectFor(it.value().projectId);
        if (project.id.isEmpty() && project.slug.isEmpty())
            result.warnings.append(QStringLiteral("无法读取 %1 的项目信息").arg(it.value().projectId));
        else
            result.projects.append(project);
    }

    result.ok = true;
    return result;
}

} // namespace mods
