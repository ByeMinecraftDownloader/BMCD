#include "rag/Corpus.h"

#include "platform/Console.h"

#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTextStream>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

namespace rag {
namespace {

QByteArray httpGet(const QUrl &url, QString *error)
{
    QNetworkAccessManager manager;
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, platform::userAgent());
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = manager.get(request);

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(30000);
    loop.exec();

    if (!timer.isActive()) {
        reply->abort();
        if (error)
            *error = QStringLiteral("请求超时：%1").arg(url.toString());
        reply->deleteLater();
        return {};
    }
    const QByteArray body = reply->readAll();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() != QNetworkReply::NoError || status >= 400) {
        if (error)
            *error = QStringLiteral("HTTP %1：%2").arg(status).arg(reply->errorString());
        reply->deleteLater();
        return {};
    }
    reply->deleteLater();
    return body;
}

} // namespace

QString CorpusBuilder::cleanMarkdown(const QString &markdown)
{
    QString text = markdown;
    text.replace(QRegularExpression(QStringLiteral("```[\\s\\S]*?```")), QStringLiteral(" "));
    text.replace(QRegularExpression(QStringLiteral("!\\[[^\\]]*\\]\\([^)]*\\)")), QStringLiteral(" "));
    text.replace(QRegularExpression(QStringLiteral("\\[([^\\]]+)\\]\\([^)]*\\)")),
                 QStringLiteral("\\1"));
    text.replace(QRegularExpression(QStringLiteral("<[^>]+>")), QStringLiteral(" "));
    text.replace(QRegularExpression(QStringLiteral("^\\s{0,3}#{1,6}\\s*"), QRegularExpression::MultilineOption),
                 QString());
    text.replace(QRegularExpression(QStringLiteral("[*_`>|]")), QStringLiteral(" "));
    text.replace(QRegularExpression(QStringLiteral("[ \\t]+")), QStringLiteral(" "));
    text.replace(QRegularExpression(QStringLiteral("\\n{3,}")), QStringLiteral("\n\n"));
    return text.trimmed();
}

QVector<QString> CorpusBuilder::chunkText(const QString &title, const QString &body, int maxChars,
                                          int overlap)
{
    QVector<QString> chunks;
    QStringList sections;
    QString current;
    const QStringList lines = body.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        if (line.startsWith(QLatin1String("## ")) && !current.trimmed().isEmpty()) {
            sections.append(current);
            current.clear();
        }
        current += line;
        current += QLatin1Char('\n');
    }
    if (!current.trimmed().isEmpty())
        sections.append(current);

    const QString prefix = QStringLiteral("# %1\n").arg(title);
    for (const QString &section : sections) {
        QString rest = section.trimmed();
        while (!rest.isEmpty()) {
            if (rest.size() <= maxChars) {
                chunks.append(prefix + rest);
                break;
            }
            int cut = rest.lastIndexOf(QLatin1Char('\n'), maxChars);
            if (cut < maxChars / 2)
                cut = maxChars;
            chunks.append(prefix + rest.left(cut).trimmed());
            rest = rest.mid(qMax(0, cut - overlap)).trimmed();
        }
    }
    return chunks;
}

bool CorpusBuilder::addChunk(const QString &project, const QString &kind, const QString &title,
                             const QString &loader, const QString &gameVersion, const QString &text,
                             const QString &sourceUrl, QString *error)
{
    QVector<float> values;
    if (!m_embedder->embed(QStringLiteral("passage: ") + text, &values) || values.isEmpty()) {
        if (error)
            *error = QStringLiteral("嵌入失败：%1").arg(title);
        return false;
    }
    Chunk chunk;
    chunk.project = project;
    chunk.kind = kind;
    chunk.title = title;
    chunk.loader = loader;
    chunk.gameVersion = gameVersion;
    chunk.text = text;
    chunk.sourceUrl = sourceUrl;
    chunk.vector = Index::quantise(values, &chunk.scale);
    return m_index->addChunk(&chunk, error);
}

bool CorpusBuilder::buildFromDocs(const QString &dir, BuildReport *report, QString *error)
{
    const QDir root(dir);
    if (!root.exists()) {
        if (error)
            *error = QStringLiteral("目录不存在：%1").arg(dir);
        return false;
    }
    if (!m_index->beginBulk(error))
        return false;

    const QFileInfoList files = root.entryInfoList({QStringLiteral("*.md")}, QDir::Files, QDir::Name);
    for (const QFileInfo &info : files) {
        QFile file(info.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            if (report)
                ++report->skipped;
            continue;
        }
        const QString raw = QString::fromUtf8(file.readAll());
        const QString title = info.completeBaseName().replace(QLatin1Char('-'), QLatin1Char(' '));
        const QString body = cleanMarkdown(raw);
        const QVector<QString> pieces = chunkText(title, body);
        for (const QString &piece : pieces) {
            QString addError;
            if (!addChunk(info.completeBaseName(), QStringLiteral("doc"), title, QString(),
                          QString(), piece, QStringLiteral("file://%1").arg(info.absoluteFilePath()),
                          &addError)) {
                if (error)
                    *error = addError;
                m_index->endBulk(false, nullptr);
                return false;
            }
            if (report)
                ++report->chunks;
        }
        if (report)
            ++report->projects;
    }
    m_index->setMeta(QStringLiteral("embedder"), m_embedder->name());
    m_index->setMeta(QStringLiteral("dim"), QString::number(m_embedder->dim()));
    m_index->setMeta(QStringLiteral("built_at"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    if (report)
        report->embedder = m_embedder->name();
    return m_index->endBulk(true, error);
}

bool CorpusBuilder::buildFromModrinth(const QString &query, int limit, BuildReport *report,
                                      QString *error, const std::function<void(int, int)> &progress)
{
    QUrl searchUrl(QStringLiteral("https://api.modrinth.com/v2/search"));
    QUrlQuery queryParams;
    queryParams.addQueryItem(QStringLiteral("limit"), QString::number(qMin(100, limit)));
    queryParams.addQueryItem(QStringLiteral("offset"), QStringLiteral("0"));
    queryParams.addQueryItem(QStringLiteral("index"), QStringLiteral("downloads"));
    if (!query.isEmpty())
        queryParams.addQueryItem(QStringLiteral("query"), query);
    queryParams.addQueryItem(QStringLiteral("facets"),
                             QStringLiteral("[[\"project_type:mod\",\"project_type:modpack\"]]"));
    searchUrl.setQuery(queryParams);

    const QByteArray searchBody = httpGet(searchUrl, error);
    if (searchBody.isEmpty())
        return false;
    const QJsonArray hits = QJsonDocument::fromJson(searchBody).object().value(QStringLiteral("hits")).toArray();
    if (hits.isEmpty()) {
        if (error)
            *error = QStringLiteral("Modrinth 搜索没有结果");
        return false;
    }

    QStringList ids;
    for (const QJsonValue &value : hits) {
        const QString id = value.toObject().value(QStringLiteral("project_id")).toString();
        if (!id.isEmpty() && ids.size() < limit)
            ids.append(id);
    }
    if (ids.isEmpty()) {
        if (error)
            *error = QStringLiteral("搜索结果缺少 project_id");
        return false;
    }

    QUrl projectsUrl(QStringLiteral("https://api.modrinth.com/v2/projects"));
    QUrlQuery projectParams;
    projectParams.addQueryItem(QStringLiteral("ids"),
                               QLatin1Char('[') + QStringLiteral("\"") + ids.join(QStringLiteral("\",\""))
                                       + QStringLiteral("\"]"));
    projectsUrl.setQuery(projectParams);
    const QByteArray projectsBody = httpGet(projectsUrl, error);
    if (projectsBody.isEmpty())
        return false;
    const QJsonArray projects = QJsonDocument::fromJson(projectsBody).array();

    if (!m_index->beginBulk(error))
        return false;
    int done = 0;
    for (const QJsonValue &value : projects) {
        const QJsonObject project = value.toObject();
        const QString title = project.value(QStringLiteral("title")).toString();
        const QString slug = project.value(QStringLiteral("slug")).toString();
        QString loader;
        for (const QJsonValue &item : project.value(QStringLiteral("loaders")).toArray())
            loader += (loader.isEmpty() ? QString() : QStringLiteral(",")) + item.toString();
        QString versions;
        const QJsonArray gameVersions = project.value(QStringLiteral("game_versions")).toArray();
        for (int i = 0; i < qMin(8, gameVersions.size()); ++i)
            versions += (versions.isEmpty() ? QString() : QStringLiteral(",")) + gameVersions.at(i).toString();

        QString body = project.value(QStringLiteral("body")).toString();
        if (body.isEmpty())
            body = project.value(QStringLiteral("description")).toString();
        const QString text = cleanMarkdown(body);
        if (text.size() < 40)
            continue;

        const QVector<QString> pieces = chunkText(title, text);
        for (const QString &piece : pieces) {
            QString addError;
            if (!addChunk(slug, project.value(QStringLiteral("project_type")).toString(), title, loader,
                          versions, piece,
                          QStringLiteral("https://modrinth.com/project/%1").arg(slug), &addError)) {
                if (error)
                    *error = addError;
                m_index->endBulk(false, nullptr);
                return false;
            }
            if (report)
                ++report->chunks;
        }
        if (report)
            ++report->projects;
        ++done;
        if (progress)
            progress(done, projects.size());
    }

    m_index->setMeta(QStringLiteral("embedder"), m_embedder->name());
    m_index->setMeta(QStringLiteral("dim"), QString::number(m_embedder->dim()));
    m_index->setMeta(QStringLiteral("built_at"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    if (report)
        report->embedder = m_embedder->name();
    return m_index->endBulk(true, error);
}

} // namespace rag
