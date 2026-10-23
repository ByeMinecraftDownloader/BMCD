#include "mods/ModsController.h"

#include "mods/ModAliases.h"

#include "core/Pack.h"
#include "mods/ModrinthApi.h"

#include <QDateTime>
#include <QFileInfo>
#include <QPointer>
#include <QThread>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>

namespace {

constexpr auto kBase = "https://api.modrinth.com/v2";

QJsonArray facets(const QString &loader, const QString &gameVersion)
{
    QJsonArray out;
    QJsonArray type;
    type.append(QStringLiteral("project_type:mod"));
    out.append(type);
    if (!loader.isEmpty()) {
        QJsonArray loaderFacet;
        loaderFacet.append(QStringLiteral("categories:") + loader);
        out.append(loaderFacet);
    }
    if (!gameVersion.isEmpty()) {
        QJsonArray versionFacet;
        versionFacet.append(QStringLiteral("versions:") + gameVersion);
        out.append(versionFacet);
    }
    return out;
}

} // namespace

ModsController::ModsController(QObject *parent)
    : QObject(parent)
    , m_aliases(mods::loadAliasTable())
{
}

void ModsController::setContext(const QString &loader, const QString &gameVersion)
{
    if (m_loader == loader && m_gameVersion == gameVersion)
        return;
    m_loader = loader;
    m_gameVersion = gameVersion;
    m_results.clear();
    m_selected.clear();
    m_error.clear();
    m_planError.clear();
    m_query.clear();
    m_offset = 0;
    emit contextChanged();
    emit resultsChanged();
    emit selectedChanged();
    emit errorChanged();
    emit planningChanged();
}

void ModsController::search(const QString &query)
{
    m_query = query.trimmed();
    m_offset = 0;
    requestPage(m_query, 0);
}

void ModsController::loadMore()
{
    if (!m_hasMore || m_searching)
        return;
    requestPage(m_query, m_offset);
}

void ModsController::requestPage(const QString &query, int offset)
{
    const int generation = ++m_generation;
    if (m_searchReply) {
        QNetworkReply *previous = m_searchReply;
        m_searchReply = nullptr;
        previous->abort();
        previous->deleteLater();
    }
    m_searching = true;
    emit searchingChanged();

    QUrl url(QStringLiteral("%1/search").arg(QLatin1String(kBase)));
    QUrlQuery params;
    params.addQueryItem(QStringLiteral("query"), mods::expandAlias(query, m_aliases));
    const QJsonArray facetArray = facets(m_loader, m_gameVersion);
    params.addQueryItem(QStringLiteral("facets"),
                        QString::fromUtf8(QJsonDocument(facetArray).toJson(QJsonDocument::Compact)));
    params.addQueryItem(QStringLiteral("limit"), QStringLiteral("20"));
    params.addQueryItem(QStringLiteral("offset"), QString::number(offset));
    params.addQueryItem(QStringLiteral("index"), QStringLiteral("relevance"));
    url.setQuery(params);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("BMCD/1.0 (mod picker; https://github.com/ByeMinecraftDownloader/BMCD)"));
    request.setRawHeader("Accept", "application/json");
    request.setTransferTimeout(std::chrono::milliseconds(20000));
    QNetworkReply *reply = m_nam.get(request);
    m_searchReply = reply;
    m_offset = offset;

    connect(reply, &QNetworkReply::finished, this, [this, reply, generation] {
        finishSearch(reply, generation);
    });
}

void ModsController::finishSearch(QNetworkReply *reply, int generation)
{
    if (generation != m_generation) {
        reply->deleteLater();
        return;
    }
    if (reply == m_searchReply)
        m_searchReply = nullptr;
    m_searching = false;

    if (reply->error() != QNetworkReply::NoError) {
        m_error = QStringLiteral("搜索失败：%1").arg(reply->errorString());
    } else {
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(reply->readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            m_error = QStringLiteral("搜索响应格式错误");
        } else {
            m_error.clear();
            const QJsonObject root = document.object();
            if (m_offset == 0)
                m_results.clear();
            const QJsonArray hits = root.value(QStringLiteral("hits")).toArray();
            for (const QJsonValue &hit : hits)
                m_results.append(parseHit(hit.toObject()));
            m_hasMore = hits.size() >= 20;
            m_offset += hits.size();
        }
    }
    reply->deleteLater();
    emit searchingChanged();
    emit resultsChanged();
    emit errorChanged();
}

QVariantMap ModsController::parseHit(const QJsonObject &object) const
{
    QVariantMap hit;
    hit.insert(QStringLiteral("slug"), object.value(QStringLiteral("slug")).toString());
    hit.insert(QStringLiteral("projectId"), object.value(QStringLiteral("project_id")).toString());
    hit.insert(QStringLiteral("title"), object.value(QStringLiteral("title")).toString());
    hit.insert(QStringLiteral("description"), object.value(QStringLiteral("description")).toString());
    hit.insert(QStringLiteral("iconUrl"), object.value(QStringLiteral("icon_url")).toString());
    hit.insert(QStringLiteral("downloads"), double(object.value(QStringLiteral("downloads")).toDouble()));
    hit.insert(QStringLiteral("author"), object.value(QStringLiteral("author")).toString());
    return hit;
}

void ModsController::addProject(const QString &slug, const QString &title, const QString &iconUrl)
{
    if (slug.isEmpty() || isSelected(slug))
        return;
    QVariantMap entry;
    entry.insert(QStringLiteral("slug"), slug);
    entry.insert(QStringLiteral("title"), title.isEmpty() ? slug : title);
    entry.insert(QStringLiteral("iconUrl"), iconUrl);
    m_selected.append(entry);
    emit selectedChanged();
}

void ModsController::removeProject(const QString &slug)
{
    for (int i = 0; i < m_selected.size(); ++i) {
        if (m_selected.at(i).toMap().value(QStringLiteral("slug")).toString() == slug) {
            m_selected.removeAt(i);
            emit selectedChanged();
            return;
        }
    }
}

bool ModsController::isSelected(const QString &slug) const
{
    for (const QVariant &entry : m_selected) {
        if (entry.toMap().value(QStringLiteral("slug")).toString() == slug)
            return true;
    }
    return false;
}

void ModsController::clearSelection()
{
    if (m_selected.isEmpty())
        return;
    m_selected.clear();
    emit selectedChanged();
}

void ModsController::buildPlan()
{
    if (m_planning || m_selected.isEmpty())
        return;
    m_planError.clear();
    m_note.clear();
    m_pendingPlan.clear();
    m_awaitingDependencies.clear();
    m_awaitingConfirmation = false;
    m_planning = true;
    emit pendingChanged();
    emit planningChanged();

    QVector<mods::Root> roots;
    for (const QVariant &entry : m_selected) {
        mods::Root root;
        root.project = entry.toMap().value(QStringLiteral("slug")).toString();
        roots.append(root);
    }
    const QString loader = m_loader;
    const QString gameVersion = m_gameVersion;

    QPointer<ModsController> self(this);
    QThread *thread = new QThread;
    QObject *context = new QObject;
    context->moveToThread(thread);
    connect(thread, &QThread::started, context, [self, roots, loader, gameVersion, thread] {
        ModrinthApi api(loader, gameVersion);
        const mods::ResolveResult result = api.resolve(roots);
        if (self)
            QMetaObject::invokeMethod(self, [self, result] { self->applyPlan(result); }, Qt::QueuedConnection);
        thread->quit();
    });
    connect(thread, &QThread::finished, context, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void ModsController::applyPlan(const mods::ResolveResult &result)
{
    m_planning = false;
    emit planningChanged();
    if (!result.ok) {
        if (!result.conflicts.isEmpty()) {
            QStringList lines;
            for (const mods::Conflict &conflict : result.conflicts)
                lines.append(conflict.message);
            m_planError = QStringLiteral("计划未生成，请调整菜篮后重试：\n")
                    + lines.join(QStringLiteral("\n"));
        } else {
            m_planError = result.error;
        }
        emit planningChanged();
        return;
    }

    Pack pack;
    pack.format = 1;
    pack.name = QStringLiteral("自定义模组 · %1 %2").arg(m_loader, m_gameVersion);
    pack.versionId = QStringLiteral("mods-%1")
                             .arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss")));
    pack.minecraftVersion = m_gameVersion;
    pack.loaderType = m_loader;
    pack.generator = QStringLiteral("0.1.0");
    pack.description = QStringLiteral("由 BMCD 模组选择生成");

    QHash<QString, mods::Project> projects;
    for (const mods::Project &project : result.projects)
        projects.insert(project.id, project);

    for (const mods::Version &version : result.versions) {
        const mods::File *file = mods::primaryFile(version);
        if (!file)
            continue;
        PackFile entry;
        entry.path = QStringLiteral("mods/") + QFileInfo(file->filename).fileName();
        entry.url = file->url;
        entry.sha1 = file->sha1;
        entry.sha512 = file->sha512;
        entry.size = file->size;
        entry.kind = QStringLiteral("mod");
        entry.projectId = version.projectId;
        entry.projectName = projects.value(version.projectId).title;
        entry.fileId = version.id;
        const mods::Project project = projects.value(version.projectId);
        entry.sourceUrl = project.slug.isEmpty()
                ? QStringLiteral("https://modrinth.com/project/%1").arg(version.projectId)
                : QStringLiteral("https://modrinth.com/mod/%1").arg(project.slug);
        entry.licenseId = project.licenseId;
        entry.licenseName = project.licenseName;
        entry.licenseUrl = project.licenseUrl;
        entry.author = project.author;
        pack.files.append(entry);
    }

    QString error;
    if (!pack.isValid(&error)) {
        m_planError = QStringLiteral("生成的计划无效：%1").arg(error);
        emit planningChanged();
        return;
    }
    if (!result.warnings.isEmpty())
        m_note = result.warnings.join(QStringLiteral("；"));
    emit noteChanged();

    const QString json = QString::fromUtf8(QJsonDocument(pack.toJson()).toJson(QJsonDocument::Compact));
    if (result.addedDependencies.isEmpty()) {
        emit planReady(json);
        return;
    }

    QStringList dependencyTitles;
    for (const QString &projectId : result.addedDependencies) {
        const mods::Project project = projects.value(projectId);
        dependencyTitles.append(project.title.isEmpty() ? projectId : project.title);
    }
    m_pendingPlan = json;
    m_awaitingDependencies = dependencyTitles;
    m_awaitingConfirmation = true;
    emit pendingChanged();
    emit planConfirmationRequested();
}

void ModsController::confirmPlan()
{
    if (!m_awaitingConfirmation)
        return;
    const QString json = m_pendingPlan;
    m_pendingPlan.clear();
    m_awaitingDependencies.clear();
    m_awaitingConfirmation = false;
    emit pendingChanged();
    if (!json.isEmpty())
        emit planReady(json);
}

void ModsController::cancelPlan()
{
    if (!m_awaitingConfirmation)
        return;
    m_pendingPlan.clear();
    m_awaitingDependencies.clear();
    m_awaitingConfirmation = false;
    emit pendingChanged();
    m_planError = QStringLiteral("已取消自动加入依赖");
    emit planningChanged();
}
