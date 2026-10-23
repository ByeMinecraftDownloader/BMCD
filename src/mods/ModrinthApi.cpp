#include "mods/ModrinthApi.h"

#include <QEventLoop>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrlQuery>

#include <chrono>

namespace {

constexpr auto kBase = "https://api.modrinth.com/v2";

QStringList stringList(const QJsonValue &value)
{
    QStringList out;
    for (const QJsonValue &entry : value.toArray())
        out.append(entry.toString());
    return out;
}

mods::Version versionFromJson(const QJsonObject &object)
{
    mods::Version version;
    version.id = object.value(QStringLiteral("id")).toString();
    version.projectId = object.value(QStringLiteral("project_id")).toString();
    version.name = object.value(QStringLiteral("name")).toString();
    version.versionNumber = object.value(QStringLiteral("version_number")).toString();
    version.datePublished = object.value(QStringLiteral("date_published")).toString();
    version.loaders = stringList(object.value(QStringLiteral("loaders")));
    version.gameVersions = stringList(object.value(QStringLiteral("game_versions")));

    for (const QJsonValue &entry : object.value(QStringLiteral("files")).toArray()) {
        const QJsonObject fileObject = entry.toObject();
        mods::File file;
        file.filename = fileObject.value(QStringLiteral("filename")).toString();
        file.url = fileObject.value(QStringLiteral("url")).toString();
        file.size = qint64(fileObject.value(QStringLiteral("size")).toDouble());
        file.primary = fileObject.value(QStringLiteral("primary")).toBool(false);
        const QJsonObject hashes = fileObject.value(QStringLiteral("hashes")).toObject();
        file.sha1 = hashes.value(QStringLiteral("sha1")).toString().toLower();
        file.sha512 = hashes.value(QStringLiteral("sha512")).toString().toLower();
        if (!file.filename.isEmpty() && !file.url.isEmpty())
            version.files.append(file);
    }

    for (const QJsonValue &entry : object.value(QStringLiteral("dependencies")).toArray()) {
        const QJsonObject dep = entry.toObject();
        mods::Dependency dependency;
        dependency.projectId = dep.value(QStringLiteral("project_id")).toString();
        dependency.versionId = dep.value(QStringLiteral("version_id")).toString();
        dependency.type = dep.value(QStringLiteral("dependency_type")).toString();
        if (!dependency.projectId.isEmpty())
            version.dependencies.append(dependency);
    }
    return version;
}

mods::Project projectFromJson(const QJsonObject &object)
{
    mods::Project project;
    project.id = object.value(QStringLiteral("id")).toString();
    project.slug = object.value(QStringLiteral("slug")).toString();
    project.title = object.value(QStringLiteral("title")).toString();
    project.description = object.value(QStringLiteral("description")).toString();
    project.iconUrl = object.value(QStringLiteral("icon_url")).toString();
    const QJsonObject license = object.value(QStringLiteral("license")).toObject();
    project.licenseId = license.value(QStringLiteral("id")).toString();
    project.licenseName = license.value(QStringLiteral("name")).toString();
    project.licenseUrl = license.value(QStringLiteral("url")).toString();
    return project;
}

} // namespace

ModrinthApi::ModrinthApi(QString loader, QString gameVersion)
    : m_loader(std::move(loader))
    , m_gameVersion(std::move(gameVersion))
{
}

QJsonDocument ModrinthApi::get(const QUrl &url, QString *error)
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("BMCD/1.0 (mod picker; https://github.com/ByeMinecraftDownloader/BMCD)"));
    request.setRawHeader("Accept", "application/json");
    request.setTransferTimeout(std::chrono::milliseconds(20000));

    QNetworkReply *reply = m_nam.get(request);
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start(20000);
    if (!reply->isFinished())
        loop.exec();

    QJsonDocument document;
    if (!reply->isFinished()) {
        reply->abort();
        if (error)
            *error = QStringLiteral("请求超时：%1").arg(url.toString());
    } else if (reply->error() != QNetworkReply::NoError) {
        if (error)
            *error = QStringLiteral("请求失败：%1").arg(reply->errorString());
    } else {
        QJsonParseError parseError;
        document = QJsonDocument::fromJson(reply->readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            if (error)
                *error = QStringLiteral("响应格式错误：%1").arg(parseError.errorString());
        }
    }
    reply->deleteLater();
    return document;
}

QVector<mods::Version> ModrinthApi::versions(const QString &project)
{
    if (m_versions.contains(project))
        return m_versions.value(project);

    QUrl url(QStringLiteral("%1/project/%2/version")
                     .arg(QLatin1String(kBase), QString::fromLatin1(QUrl::toPercentEncoding(project))));
    QUrlQuery query;
    QJsonArray loaders{m_loader};
    QJsonArray gameVersions{m_gameVersion};
    query.addQueryItem(QStringLiteral("loaders"),
                       QString::fromUtf8(QJsonDocument(loaders).toJson(QJsonDocument::Compact)));
    query.addQueryItem(QStringLiteral("game_versions"),
                       QString::fromUtf8(QJsonDocument(gameVersions).toJson(QJsonDocument::Compact)));
    url.setQuery(query);

    QString error;
    const QJsonDocument document = get(url, &error);
    QVector<mods::Version> out;
    if (!document.isArray())
        return out;
    for (const QJsonValue &entry : document.array())
        out.append(versionFromJson(entry.toObject()));
    m_versions.insert(project, out);
    return out;
}

mods::Project ModrinthApi::project(const QString &project)
{
    if (m_projects.contains(project))
        return m_projects.value(project);

    QUrl url(QStringLiteral("%1/project/%2")
                     .arg(QLatin1String(kBase), QString::fromLatin1(QUrl::toPercentEncoding(project))));
    QString error;
    const QJsonDocument document = get(url, &error);
    mods::Project out;
    if (document.isObject())
        out = projectFromJson(document.object());
    m_projects.insert(project, out);
    return out;
}

mods::ResolveResult ModrinthApi::resolve(const QVector<mods::Root> &roots)
{
    mods::ResolveRequest request;
    request.loader = m_loader;
    request.gameVersion = m_gameVersion;
    request.roots = roots;
    return mods::resolve(request,
                         [this](const QString &project) { return versions(project); },
                         [this](const QString &project) { return this->project(project); });
}
