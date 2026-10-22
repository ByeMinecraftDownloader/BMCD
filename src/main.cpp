#include <QCommandLineParser>
#include <QLocale>
#include <QTranslator>
#include <qqml.h>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QStorageInfo>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QTextStream>
#include <QTimer>

#include "AppSettings.h"
#include "DownloadTask.h"
#include "Pack.h"
#include "Deployer.h"
#include "LicenseGate.h"
#include "Renderers.h"
#include "rag/Corpus.h"
#include "rag/Eval.h"
#include "rag/Embedder.h"
#include "rag/RagController.h"
#include "rag/Embedder.h"
#include "rag/Index.h"
#include "rag/Retriever.h"
#include "platform/Console.h"
#include "platform/FileOps.h"
#include "platform/Paths.h"
#include "platform/PlatformInfo.h"
#include "theme/ThemeSource.h"
#include "theme/IconImageProvider.h"
#include "theme/TokenBuilder.h"
#include "InstalledVersions.h"
#include "Instances.h"
#include "PackImporter.h"
#include "PlanParser.h"

#include <chrono>

namespace {
bool loadPack(const QString &path, Pack *pack, QString *error);
int runExportPlan(const QString &versionId, const QString &target, const QString &outPath);
int runLicenses(const QString &packPath, const QStringList &confirmed);
int runPackRender(const QString &packPath, const QString &format, const QString &outPath,
                  const QStringList &confirmed);
int runServe(const QString &packPath, const QString &targetDir, bool dryRun, bool backup,
             bool skipOptional, const QStringList &confirmed);
int runVerify(const QString &packPath, const QString &dir);


constexpr auto kManifestUrl = "https://piston-meta.mojang.com/mc/game/version_manifest_v2.json";

QTextStream &out()
{
    static QTextStream stream(stdout);
    return stream;
}

// Blocking GET. Only used by the command line modes, which have no event loop
// of their own to interleave requests with.
QByteArray fetch(const QUrl &url, int timeoutMs = 30000)
{
    QNetworkAccessManager nam;
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(std::chrono::milliseconds(timeoutMs));
    request.setHeader(QNetworkRequest::UserAgentHeader, platform::userAgent());

    QNetworkReply *reply = nam.get(request);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::errorOccurred, &loop, &QEventLoop::quit);
    loop.exec();

    QByteArray data = reply->readAll();
    if (reply->error() != QNetworkReply::NoError)
        data.clear();
    reply->deleteLater();
    return data;
}

QString formatBytes(qint64 bytes)
{
    const double mb = double(bytes) / (1024.0 * 1024.0);
    if (mb >= 1024.0)
        return QString::number(mb / 1024.0, 'f', 2) + QStringLiteral(" GB");
    return QString::number(mb, 'f', 1) + QStringLiteral(" MB");
}

bool fetchPlan(const QString &versionId, TaskPlan *plan, QString *error)
{
    out() << "manifest ..." << Qt::endl;
    const QJsonObject manifest = QJsonDocument::fromJson(fetch(QUrl(QString::fromLatin1(kManifestUrl)))).object();
    if (manifest.isEmpty()) {
        *error = QStringLiteral("无法获取版本清单");
        return false;
    }

    QString versionUrl;
    const QJsonArray versions = manifest.value(QLatin1String("versions")).toArray();
    for (const QJsonValue &value : versions) {
        const QJsonObject entry = value.toObject();
        if (entry.value(QLatin1String("id")).toString() == versionId) {
            versionUrl = entry.value(QLatin1String("url")).toString();
            break;
        }
    }
    if (versionUrl.isEmpty()) {
        *error = QStringLiteral("清单里没有版本 %1").arg(versionId);
        return false;
    }

    out() << "version json ..." << Qt::endl;
    const QByteArray rawVersionJson = fetch(QUrl(versionUrl));
    const QJsonObject versionJson = QJsonDocument::fromJson(rawVersionJson).object();
    if (versionJson.isEmpty()) {
        *error = QStringLiteral("无法获取版本 json");
        return false;
    }

    QJsonObject assetIndexJson;
    const QString assetIndexUrl = versionJson.value(QLatin1String("assetIndex")).toObject().value(QLatin1String("url")).toString();
    if (!assetIndexUrl.isEmpty()) {
        out() << "asset index ..." << Qt::endl;
        assetIndexJson = QJsonDocument::fromJson(fetch(QUrl(assetIndexUrl), 60000)).object();
    }

    return PlanParser::build(versionJson, rawVersionJson, versionUrl, assetIndexJson, plan, error);
}

int runPlan(const QString &versionId)
{
    TaskPlan plan;
    QString error;
    if (!fetchPlan(versionId, &plan, &error)) {
        out() << "error: " << error << Qt::endl;
        return 1;
    }

    int assets = 0, libraries = 0, client = 0;
    for (const PlanFile &file : plan.files) {
        if (file.kind == QLatin1String("asset"))
            ++assets;
        else if (file.kind == QLatin1String("library"))
            ++libraries;
        else if (file.kind == QLatin1String("client"))
            ++client;
    }

    out() << Qt::endl;
    out() << "version      " << plan.id << " (" << plan.type << ")" << Qt::endl;
    out() << "java         " << plan.javaMajor << Qt::endl;
    out() << "asset index  " << plan.assetIndexId << Qt::endl;
    out() << "files        " << plan.files.size() << Qt::endl;
    out() << "  client     " << client << Qt::endl;
    out() << "  libraries  " << libraries << Qt::endl;
    out() << "  assets     " << assets << Qt::endl;
    out() << "total        " << formatBytes(plan.totalBytes) << " (" << plan.totalBytes << " bytes)" << Qt::endl;
    return 0;
}

int runDownload(const QString &versionId, const QString &target, int limit, int concurrency)
{
    TaskPlan plan;
    QString error;
    if (!fetchPlan(versionId, &plan, &error)) {
        out() << "error: " << error << Qt::endl;
        return 1;
    }

    out() << Qt::endl
          << "downloading " << plan.files.size() << " files, " << formatBytes(plan.totalBytes) << " -> " << target
          << Qt::endl;

    QDir().mkpath(target);
    DownloadTask task(plan, target, concurrency);
    task.setLimit(limit);

    int exitCode = 0;
    QEventLoop loop;
    int lastPercent = -1;

    QObject::connect(&task, &DownloadTask::progress, &loop,
                     [&, planTotal = plan.totalBytes](qint64 doneBytes, qint64 total, int doneFiles, int totalFiles, double speed) {
                         const qint64 denominator = total > 0 ? total : planTotal;
                         const int percent = int(100.0 * double(doneBytes) / double(denominator <= 0 ? 1 : denominator));
                         if (percent != lastPercent) {
                             lastPercent = percent;
                             out() << "\r" << percent << "%  " << doneFiles << "/" << totalFiles << " files  "
                                   << formatBytes(doneBytes) << "  " << QString::number(speed / (1024.0 * 1024.0), 'f', 1)
                                   << " MB/s   " << Qt::flush;
                         }
                     });
    QObject::connect(&task, &DownloadTask::log, &loop, [](const QString &message) {
        out() << Qt::endl << "  " << message << Qt::endl;
    });
    QObject::connect(&task, &DownloadTask::finished, &loop, [&](int skipped, int downloaded) {
        out() << Qt::endl
              << "done: " << downloaded << " downloaded, " << skipped << " already present" << Qt::endl;
        loop.quit();
    });
    QObject::connect(&task, &DownloadTask::failed, &loop, [&](const QString &reason) {
        out() << Qt::endl << "failed: " << reason << Qt::endl;
        exitCode = 2;
        loop.quit();
    });

    QTimer::singleShot(0, &task, &DownloadTask::start);
    loop.exec();
    return exitCode;
}

void setCoreMetadata()
{
    QCoreApplication::setApplicationName(QStringLiteral("bmcd"));
    QCoreApplication::setOrganizationName(QStringLiteral("BMCD"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0.0"));
}

// Some environments swallow Qt's console output entirely; MC_LOG answers
// "why is the list empty" without a debugger.
void installFileLog()
{
    const QString logPath = qEnvironmentVariable("MC_LOG");
    if (logPath.isEmpty())
        return;

    static QFile logFile(logPath);
    if (logFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        qInstallMessageHandler([](QtMsgType type, const QMessageLogContext &, const QString &msg) {
            const char *level = type == QtDebugMsg ? "DEBUG"
                                : type == QtInfoMsg ? "INFO"
                                : type == QtWarningMsg ? "WARN"
                                                       : "ERROR";
            logFile.write(QStringLiteral("[%1] %2\n").arg(QLatin1String(level), msg).toUtf8());
            logFile.flush();
        });
    }
}


int runRagStats()
{
    rag::Index index;
    QString error;
    if (!index.open(rag::Index::defaultPath(), &error)) {
        qCritical("%s", qPrintable(error));
        return 1;
    }
    QString note;
    rag::makeEmbedder(rag::defaultModelDir(), &note);
    QTextStream out(stdout);
    out << "index: " << index.path() << "\n"
        << "chunks: " << index.count() << "\n"
        << "dim: " << index.dim() << "\n"
        << "embedder: " << index.meta(QStringLiteral("embedder")) << "\n"
        << "built: " << index.meta(QStringLiteral("built_at")) << "\n"
        << "available: " << note << "\n";
    return 0;
}

int runRagBuild(const QString &docsDir, const QString &query, int limit, bool fromDocs)
{
    QString note;
    auto embedder = rag::makeEmbedder(rag::defaultModelDir(), &note);
    qInfo("%s", qPrintable(note));

    rag::Index index;
    QString error;
    if (!index.open(rag::Index::defaultPath(), &error)) {
        qCritical("%s", qPrintable(error));
        return 1;
    }
    if (!index.reset(&error)) {
        qCritical("%s", qPrintable(error));
        return 1;
    }
    rag::CorpusBuilder builder(&index, embedder.get());
    rag::BuildReport report;
    const bool ok = fromDocs
            ? builder.buildFromDocs(docsDir, &report, &error)
            : builder.buildFromModrinth(query, limit, &report, &error);
    if (!ok) {
        qCritical("%s", qPrintable(error));
        return 1;
    }
    index.setMeta(QStringLiteral("source"), fromDocs ? docsDir : QStringLiteral("modrinth"));
    QTextStream out(stdout);
    out << "indexed " << report.projects << " documents, " << report.chunks << " chunks\n"
        << "embedder: " << report.embedder << "\n"
        << "index: " << index.path() << "\n";
    return 0;
}

int runAsk(const QString &question, int top, bool json)
{
    QString note;
    auto embedder = rag::makeEmbedder(rag::defaultModelDir(), &note);
    rag::Index index;
    QString error;
    if (!index.open(rag::Index::defaultPath(), &error)) {
        qCritical("%s", qPrintable(error));
        return 1;
    }
    if (index.count() == 0) {
        qCritical("索引为空：先运行 bmcd --rag-build");
        return 1;
    }
    rag::Retriever retriever(&index, embedder.get());
    const QVector<rag::SearchHit> hits = retriever.search(question, top, {}, &error);
    if (!error.isEmpty())
        qWarning("%s", qPrintable(error));

    if (json) {
        QJsonArray array;
        for (const rag::SearchHit &hit : hits) {
            QJsonObject item;
            item.insert(QStringLiteral("project"), hit.chunk.project);
            item.insert(QStringLiteral("title"), hit.chunk.title);
            item.insert(QStringLiteral("kind"), hit.chunk.kind);
            item.insert(QStringLiteral("text"), hit.chunk.text);
            item.insert(QStringLiteral("url"), hit.chunk.sourceUrl);
            item.insert(QStringLiteral("score"), hit.score);
            array.append(item);
        }
        QJsonObject root;
        root.insert(QStringLiteral("query"), question);
        root.insert(QStringLiteral("embedder"), embedder->name());
        root.insert(QStringLiteral("hits"), array);
        QTextStream out(stdout);
        out << QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented));
        return 0;
    }

    QTextStream out(stdout);
    out << "query: " << question << "\n"
        << "embedder: " << embedder->name() << " (" << note << ")\n\n";
    int rank = 1;
    for (const rag::SearchHit &hit : hits) {
        QString snippet = hit.chunk.text.simplified();
        if (snippet.size() > 240)
            snippet = snippet.left(240) + QStringLiteral("…");
        out << rank++ << ". " << hit.chunk.title << "  [" << hit.chunk.kind << " / "
            << hit.chunk.project << "]  score " << QString::number(hit.score, 'f', 4) << "\n"
            << "   " << snippet << "\n"
            << "   " << hit.chunk.sourceUrl << "\n";
    }
    out << "\n" << retriever.lastNote() << "\n";
    return hits.isEmpty() ? 1 : 0;
}

int runRagEval(const QString &qrelsPath, int top, bool json)
{
    QString error;
    const QVector<rag::JudgedQuery> qrels = rag::loadQrels(qrelsPath, &error);
    if (qrels.isEmpty()) {
        qCritical("%s", qPrintable(error));
        return 1;
    }

    QString note;
    auto embedder = rag::makeEmbedder(rag::defaultModelDir(), &note);
    rag::Index index;
    if (!index.open(rag::Index::defaultPath(), &error)) {
        qCritical("%s", qPrintable(error));
        return 1;
    }
    if (index.count() == 0) {
        qCritical("索引为空：先运行 bmcd --rag-build");
        return 1;
    }

    rag::Evaluator evaluator(&index, embedder.get());
    const rag::EvalReport report = evaluator.run(qrels, top, &error);
    if (!error.isEmpty())
        qWarning("%s", qPrintable(error));

    QTextStream out(stdout);
    if (json) {
        QJsonObject root;
        root.insert(QStringLiteral("queries"), report.queries);
        root.insert(QStringLiteral("judged"), report.judged);
        root.insert(QStringLiteral("top"), report.top);
        root.insert(QStringLiteral("fusedNdcg"), report.fusedNdcg);
        root.insert(QStringLiteral("bm25Ndcg"), report.bm25Ndcg);
        root.insert(QStringLiteral("embedder"), embedder->name());
        out << QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented));
        return 0;
    }
    out << "queries: " << report.queries << " (" << report.judged << " 有结果)\n"
        << "embedder: " << embedder->name() << "\n"
        << "nDCG@" << report.top << " fused: " << QString::number(report.fusedNdcg, 'f', 4) << "\n"
        << "nDCG@" << report.top << " bm25:  " << QString::number(report.bm25Ndcg, 'f', 4) << "\n";
    return 0;
}

int runImport(const QString &archivePath, const QString &target)
{
    ImportResult result = importPackArchive(archivePath);
    if (!result.ok) {
        out() << "error: " << result.error << Qt::endl;
        return 1;
    }

    out() << "format: " << result.format << "\n"
          << "name: " << result.pack.name << "\n"
          << "version: " << result.pack.versionId << "\n"
          << "minecraft: " << result.pack.minecraftVersion
          << " / " << result.pack.loaderType << " " << result.pack.loaderVersion << "\n"
          << "files: " << result.pack.files.size() << " (内置 " << result.overrideCount << ")\n"
          << "staging: " << result.stagingDir << "\n";
    for (const QString &path : result.unresolved)
        out() << "warning: 无下载地址 " << path << "\n";
    for (const QString &path : result.unsupported)
        out() << "skip: " << path << "\n";
    out() << Qt::endl;

    if (target.isEmpty()) {
        out() << "提示：加 --to <游戏目录> 下载并部署；或 --pack <staging>/import.json 继续操作。"
              << Qt::endl;
        return 0;
    }

    if (!result.pack.files.isEmpty()) {
        TaskPlan plan;
        plan.id = result.pack.versionId;
        plan.type = QStringLiteral("pack");
        for (const PackFile &file : result.pack.files) {
            PlanFile entry;
            entry.path = file.path;
            entry.url = file.url;
            entry.sha1 = file.sha1;
            entry.size = file.size;
            entry.kind = file.kind;
            plan.files.append(entry);
            plan.totalBytes += file.size;
        }

        DownloadTask task(plan, result.pack.target, 6);
        QEventLoop loop;
        bool failed = false;
        bool completed = false;
        QObject::connect(&task, &DownloadTask::finished, &loop, [&](int, int) {
            completed = true;
            loop.quit();
        });
        QObject::connect(&task, &DownloadTask::failed, &loop, [&](const QString &message) {
            out() << "download failed: " << message << Qt::endl;
            failed = true;
            completed = true;
            loop.quit();
        });
        int lastPercent = -1;
        QObject::connect(&task, &DownloadTask::progress, &loop,
                         [&lastPercent](qint64 done, qint64 total, int files, int totalFiles, double) {
                             const int percent = total > 0 ? int(100 * done / total) : 0;
                             if (percent != lastPercent) {
                                 lastPercent = percent;
                                 out() << "\r" << files << "/" << totalFiles << "  "
                                       << percent << "%" << Qt::flush;
                             }
                         });
        task.start();
        if (!completed)
            loop.exec();
        out() << Qt::endl;
        if (failed) {
            out() << "提示：部分文件下载失败，直接重跑同一条命令即可续传。" << Qt::endl;
            return 1;
        }
    }

    DeployOptions options;
    options.targetDir = target;
    options.sourceRoot = result.pack.target;
    const DeployReport report = deploy(result.pack, options);
    if (!report.ok) {
        out() << "error: " << report.error << Qt::endl;
        return 1;
    }
    out() << "deployed " << report.deployed << " files to " << target << Qt::endl;
    return 0;
}

int runFetchModel()
{
    const QString dir = rag::defaultModelDir();
    QDir().mkpath(dir);
    struct File {
        const char *name;
        const char *url;
    };
    static const File files[] = {
        {"model_quantized.onnx",
         "https://huggingface.co/Xenova/multilingual-e5-small/resolve/main/onnx/model_quantized.onnx"},
        {"tokenizer.json",
         "https://huggingface.co/Xenova/multilingual-e5-small/resolve/main/tokenizer.json"},
    };
    for (const File &file : files) {
        const QString target = QDir(dir).absoluteFilePath(QString::fromLatin1(file.name));
        if (QFileInfo(target).size() > 1024) {
            out() << "present: " << target << Qt::endl;
            continue;
        }
        out() << "downloading " << file.name << " ..." << Qt::flush;
        const QByteArray data = fetch(QUrl(QString::fromLatin1(file.url)), 600000);
        if (data.isEmpty()) {
            out() << Qt::endl << "failed: " << file.url << Qt::endl;
            return 1;
        }
        const QString part = target + QStringLiteral(".part");
        QFile output(part);
        if (!output.open(QIODevice::WriteOnly) || output.write(data) != data.size()) {
            out() << Qt::endl << "failed to write " << part << Qt::endl;
            return 1;
        }
        output.close();
        if (!platform::replaceFile(part, target)) {
            out() << Qt::endl << "failed to move " << part << " into place" << Qt::endl;
            return 1;
        }
        out() << Qt::endl << "wrote " << target << " (" << formatBytes(data.size()) << ")" << Qt::endl;
    }
    out() << "model ready in " << dir << "; rebuild the index with --rag-build" << Qt::endl;
    return 0;
}

void dumpItemTree(QQuickItem *item, int depth, QTextStream &out)
{
    if (!item || !item->isVisible())
        return;
    const QString cls = QString::fromLatin1(item->metaObject()->className());
    const QString text = item->property("text").toString();
    const QPointF origin = item->mapToScene(QPointF(0, 0));
    out << QString(depth * 2, ' ') << cls;
    if (!text.isEmpty())
        out << " \"" << text.left(40).replace('\n', ' ') << "\"";
    out << " @(" << qRound(origin.x()) << "," << qRound(origin.y()) << ") "
        << qRound(item->width()) << "x" << qRound(item->height());
    const QVariant font = item->property("font");
    if (!text.isEmpty() && font.isValid()) {
        const QFont f = font.value<QFont>();
        out << " font=" << f.pixelSize() << "/" << f.weight();
    }
    out << "\n";
    const auto children = item->childItems();
    for (QQuickItem *child : children)
        dumpItemTree(child, depth + 1, out);
}

int runI18nCheck()
{
    QTranslator translator;
    if (!translator.load(QStringLiteral(":/i18n/bmcd_en.qm"))) {
        out() << "error: bmcd_en.qm is not embedded\n";
        return 1;
    }
    QCoreApplication::installTranslator(&translator);

    struct Probe {
        const char *context;
        const char *source;
        const char *expected;
    };
    static const Probe probes[] = {
        {"NavRail", "点菜", "Pick"},
        {"OrderView", "点菜", "Pick"},
        {"OrderView", "先选锅底，再按需加配菜；菜篮满了就开火。",
         "Pick the base version, then add mods; start cooking when the basket is ready."},
        {"ServeView", "出口", "Exits"},
        {"ServeView", "这桌菜", "This dish"},
        {"ServeView", "已烹饪", "Cooked"},
        {"WindowChrome", "关闭", "Close"},
        {"CookView", "当前任务", "Current task"},
    };
    int failures = 0;
    for (const Probe &probe : probes) {
        const QString got = QCoreApplication::translate(probe.context, probe.source);
        if (got != QLatin1String(probe.expected)) {
            out() << "FAIL " << probe.context << " / " << probe.source << " -> " << got << "\n";
            ++failures;
        }
    }
    out() << (failures == 0 ? "i18n ok" : "i18n FAILED") << " (" << failures << " failures)\n";
    return failures == 0 ? 0 : 1;
}

int runInstances(const QString &root)
{
    const QStringList roots = root.isEmpty() ? defaultInstanceRoots() : QStringList{root};
    bool any = false;
    for (const QString &path : roots) {
        const QVector<LocalInstance> instances = scanInstances(path);
        for (const LocalInstance &instance : instances) {
            any = true;
            out() << instance.name << "\t" << instance.path << "\t"
                  << instance.minecraftVersion << "\t"
                  << instance.loaderType << " " << instance.loaderVersion << "\t"
                  << instance.modCount << " mods\t" << instance.source << "\n";
        }
    }
    if (!any)
        out() << "没有找到游戏目录（可用 --instances-root <目录> 指定）" << Qt::endl;
    return any ? 0 : 1;
}

void addOptions(QCommandLineParser &parser)
{
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({QStringLiteral("plan"), QStringLiteral("Print the download plan for a version and exit."),
                      QStringLiteral("version")});
    parser.addOption({QStringLiteral("download"), QStringLiteral("Download a version without opening the window."),
                      QStringLiteral("version")});
    parser.addOption({QStringLiteral("target"), QStringLiteral("Target directory (default ~/.minecraft)."),
                      QStringLiteral("dir")});
    parser.addOption({QStringLiteral("limit"), QStringLiteral("Only transfer the first N files (testing)."),
                      QStringLiteral("n")});
    parser.addOption({QStringLiteral("concurrency"), QStringLiteral("Parallel transfers (default 6)."),
                      QStringLiteral("n"), QStringLiteral("6")});
    parser.addOption({QStringLiteral("select"), QStringLiteral("Open the UI with this version selected."),
                      QStringLiteral("version")});
    parser.addOption({QStringLiteral("tokens"), QStringLiteral("Print the derived colour tokens as JSON and exit.")});
    parser.addOption({QStringLiteral("light"), QStringLiteral("Force the light token ladder (with --tokens).")});
    parser.addOption({QStringLiteral("export-plan"), QStringLiteral("Write a pack JSON for a Minecraft version."),
                      QStringLiteral("version")});
    parser.addOption({QStringLiteral("pack"), QStringLiteral("Pack JSON to render, deploy or inspect."),
                      QStringLiteral("file")});
    parser.addOption({QStringLiteral("format"), QStringLiteral("Output format: mrpack | ftb | self."),
                      QStringLiteral("name")});
    parser.addOption({QStringLiteral("out"), QStringLiteral("Output archive path."), QStringLiteral("file")});
    parser.addOption({QStringLiteral("to"), QStringLiteral("Deploy the pack into this game directory."),
                      QStringLiteral("dir")});
    parser.addOption({QStringLiteral("verify"), QStringLiteral("Verify a deployment against the pack hashes."),
                      QStringLiteral("dir")});
    parser.addOption({QStringLiteral("licenses"), QStringLiteral("Print the licence gate result as JSON.")});
    parser.addOption({QStringLiteral("import"), QStringLiteral("Import a .mrpack / CurseForge / FTB archive."),
                      QStringLiteral("file")});
    parser.addOption({QStringLiteral("instances"), QStringLiteral("List discovered game/instance directories.")});
    parser.addOption({QStringLiteral("instances-root"), QStringLiteral("Root to scan with --instances."),
                      QStringLiteral("dir")});
    parser.addOption({QStringLiteral("confirm"), QStringLiteral("Confirm redistribution of a file path or licence group id (repeatable)."),
                      QStringLiteral("path")});
    parser.addOption({QStringLiteral("dry-run"), QStringLiteral("Preflight only; write nothing.")});
    parser.addOption({QStringLiteral("no-backup"), QStringLiteral("Do not back up files being replaced.")});
    parser.addOption({QStringLiteral("skip-optional"), QStringLiteral("Skip optional files.")});
    parser.addOption({QStringLiteral("rag-build"), QStringLiteral("Rebuild the knowledge index.")});
    parser.addOption({QStringLiteral("fetch-model"), QStringLiteral("Download the optional RAG embedding model.")});
    parser.addOption({QStringLiteral("rag-stats"), QStringLiteral("Print index statistics.")});
    parser.addOption({QStringLiteral("rag-eval"), QStringLiteral("Evaluate retrieval quality against a qrels file."),
                      QStringLiteral("file")});
    parser.addOption({QStringLiteral("ask"), QStringLiteral("Ask the local knowledge index."),
                      QStringLiteral("query")});
    parser.addOption({QStringLiteral("query"), QStringLiteral("Search term for --rag-build."),
                      QStringLiteral("term")});
    parser.addOption({QStringLiteral("from-docs"), QStringLiteral("Index Markdown documents from a directory."),
                      QStringLiteral("dir")});
    parser.addOption({QStringLiteral("top"), QStringLiteral("Number of results."), QStringLiteral("n"),
                      QStringLiteral("10")});
    parser.addOption({QStringLiteral("json"), QStringLiteral("Machine readable output.")});
    parser.addOption({QStringLiteral("i18n-check"), QStringLiteral("Verify the embedded English catalog.")});
    parser.addOption({QStringLiteral("benchmark"), QStringLiteral("Print startup/first-frame/memory numbers and exit.")});
    parser.addOption({QStringLiteral("view"), QStringLiteral("Open on a view: order|cook|serve|settings."),
                      QStringLiteral("name"), QStringLiteral("order")});
    parser.addOption({QStringLiteral("size"), QStringLiteral("Window size, e.g. 880x600."),
                      QStringLiteral("WxH")});
    parser.addOption({QStringLiteral("dump-layout"), QStringLiteral("Print the QML item tree and exit.")});
    parser.addOption({QStringLiteral("lang"), QStringLiteral("UI language code, e.g. en or zh_CN."),
                      QStringLiteral("code")});
}

QString resolveTarget(const QCommandLineParser &parser)
{
    return parser.isSet(QStringLiteral("target")) ? parser.value(QStringLiteral("target"))
                                                  : platform::defaultGameDir();
}

int runTokens(bool forceLight)
{
    const theme::SystemTheme systemTheme = theme::query();
    const bool dark = forceLight ? false : systemTheme.dark;
    theme::Tokens tokens = systemTheme.hasAccent ? theme::buildTokens(systemTheme.accent, dark)
                                                 : theme::fallbackTokens(dark);
    if (!theme::enforceContrast(tokens)) {
        tokens = theme::fallbackTokens(dark);
        theme::enforceContrast(tokens);
    }
    out() << theme::toJson(tokens) << Qt::endl;
    return 0;
}

// A QGuiApplication aborts on a machine without a display before the command
// line is even parsed, which made every headless mode unusable there. Scan the
// raw arguments first so those modes only construct a QCoreApplication.
bool isHeadlessInvocation(int argc, char *argv[])
{
    for (int i = 1; i < argc; ++i) {
        const QLatin1String arg(argv[i]);
        if (arg == QLatin1String("--plan") || arg.startsWith(QLatin1String("--plan="))
            || arg == QLatin1String("--download") || arg.startsWith(QLatin1String("--download="))
            || arg == QLatin1String("--version") || arg == QLatin1String("-v")
            || arg == QLatin1String("--help") || arg == QLatin1String("-h")
            || arg == QLatin1String("--tokens")
            || arg == QLatin1String("--pack") || arg.startsWith(QLatin1String("--pack="))
            || arg == QLatin1String("--export-plan") || arg.startsWith(QLatin1String("--export-plan="))
            || arg == QLatin1String("--serve") || arg == QLatin1String("--verify")
            || arg == QLatin1String("--licenses")
            || arg == QLatin1String("--i18n-check")
            || arg == QLatin1String("--import") || arg.startsWith(QLatin1String("--import="))
            || arg == QLatin1String("--instances") || arg.startsWith(QLatin1String("--instances-root"))
            || arg == QLatin1String("--rag-build") || arg == QLatin1String("--rag-stats")
            || arg == QLatin1String("--fetch-model")
            || arg == QLatin1String("--rag-eval") || arg == QLatin1String("--rag-eval=")
            || arg == QLatin1String("--ask") || arg.startsWith(QLatin1String("--ask=")))
            return true;
    }
    return false;
}

int runHeadless(int argc, char *argv[])
{
    // A WIN32_EXECUTABLE build has no console of its own; attach to the one
    // that launched us so `bmcd --plan X` still prints like a CLI tool.
    platform::attachConsole();
    platform::setUtf8Console();

    QCoreApplication app(argc, argv);
    setCoreMetadata();
    installFileLog();
    platform::installSystemProxy();

    QCommandLineParser parser;
    addOptions(parser);
    parser.process(app);

    const QString target = resolveTarget(parser);
    qputenv("MC_TARGET", target.toUtf8());

    if (parser.isSet(QStringLiteral("tokens")))
        return runTokens(parser.isSet(QStringLiteral("light")));

    if (parser.isSet(QStringLiteral("fetch-model")))
        return runFetchModel();

    if (parser.isSet(QStringLiteral("rag-stats")))
        return runRagStats();

    if (parser.isSet(QStringLiteral("rag-eval")))
        return runRagEval(parser.value(QStringLiteral("rag-eval")),
                          parser.value(QStringLiteral("top")).toInt() > 0
                                  ? parser.value(QStringLiteral("top")).toInt()
                                  : 10,
                          parser.isSet(QStringLiteral("json")));
    if (parser.isSet(QStringLiteral("rag-build"))) {
        const QString fromDocs = parser.value(QStringLiteral("from-docs"));
        return runRagBuild(fromDocs,
                           parser.value(QStringLiteral("query")),
                           parser.value(QStringLiteral("limit")).toInt() > 0
                                   ? parser.value(QStringLiteral("limit")).toInt()
                                   : 200,
                           !fromDocs.isEmpty());
    }

    if (parser.isSet(QStringLiteral("ask")))
        return runAsk(parser.value(QStringLiteral("ask")),
                      parser.value(QStringLiteral("top")).toInt() > 0
                              ? parser.value(QStringLiteral("top")).toInt()
                              : 10,
                      parser.isSet(QStringLiteral("json")));

    const QStringList confirmed = parser.values(QStringLiteral("confirm"));

    // --licenses alone prints the gate report; with --format it is the
    // "show the report, then export" flag from the README workflow.
    if (parser.isSet(QStringLiteral("licenses")) && !parser.isSet(QStringLiteral("format")))
        return runLicenses(parser.value(QStringLiteral("pack")), confirmed);

    if (parser.isSet(QStringLiteral("i18n-check")))
        return runI18nCheck();

    if (parser.isSet(QStringLiteral("instances")) || parser.isSet(QStringLiteral("instances-root")))
        return runInstances(parser.value(QStringLiteral("instances-root")));

    if (parser.isSet(QStringLiteral("import")))
        return runImport(parser.value(QStringLiteral("import")),
                         parser.isSet(QStringLiteral("to")) ? parser.value(QStringLiteral("to"))
                                                            : QString());

    if (parser.isSet(QStringLiteral("export-plan")))
        return runExportPlan(parser.value(QStringLiteral("export-plan")), target,
                             parser.value(QStringLiteral("out")));

    if (parser.isSet(QStringLiteral("pack"))) {
        const QString packPath = parser.value(QStringLiteral("pack"));
        if (parser.isSet(QStringLiteral("format")))
            return runPackRender(packPath, parser.value(QStringLiteral("format")),
                                 parser.value(QStringLiteral("out")), confirmed);
        if (parser.isSet(QStringLiteral("verify")))
            return runVerify(packPath, parser.value(QStringLiteral("verify")));
        return runServe(packPath, parser.value(QStringLiteral("to")),
                        parser.isSet(QStringLiteral("dry-run")),
                        !parser.isSet(QStringLiteral("no-backup")),
                        parser.isSet(QStringLiteral("skip-optional")), confirmed);
    }

    if (parser.isSet(QStringLiteral("plan")))
        return runPlan(parser.value(QStringLiteral("plan")));

    return runDownload(parser.value(QStringLiteral("download")), target,
                       parser.value(QStringLiteral("limit")).toInt() > 0
                               ? parser.value(QStringLiteral("limit")).toInt()
                               : -1,
                       parser.value(QStringLiteral("concurrency")).toInt());
}

bool loadPack(const QString &path, Pack *pack, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("无法读取 %1").arg(path);
        return false;
    }
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (document.isNull() || !document.isObject()) {
        *error = QStringLiteral("JSON 解析失败：%1").arg(parseError.errorString());
        return false;
    }
    *pack = Pack::fromJson(document.object());
    return pack->isValid(error);
}

int runExportPlan(const QString &versionId, const QString &target, const QString &outPath)
{
    TaskPlan plan;
    QString error;
    if (!fetchPlan(versionId, &plan, &error)) {
        out() << "error: " << error << Qt::endl;
        return 1;
    }

    Pack pack;
    pack.name = QStringLiteral("Minecraft %1").arg(versionId);
    pack.versionId = versionId;
    pack.minecraftVersion = versionId;
    pack.loaderType = QStringLiteral("vanilla");
    pack.target = target;
    pack.generator = QCoreApplication::applicationVersion();
    pack.description = QStringLiteral("由 BMCD 导出的原版文件计划");
    for (const PlanFile &file : plan.files) {
        PackFile entry;
        entry.path = file.path;
        entry.url = file.url;
        entry.sha1 = file.sha1;
        entry.size = file.size;
        entry.kind = file.kind == QLatin1String("client") ? QStringLiteral("client")
                    : file.kind == QLatin1String("library") ? QStringLiteral("lib")
                                                           : QStringLiteral("asset");
        entry.clientSide = entry.serverSide = true;
        pack.files.append(entry);
    }

    if (outPath.isEmpty()) {
        out() << QJsonDocument(pack.toJson()).toJson(QJsonDocument::Indented) << Qt::endl;
        return 0;
    }
    if (!platform::writeFileAtomic(outPath, QJsonDocument(pack.toJson()).toJson(QJsonDocument::Indented))) {
        out() << "error: 无法写入 " << outPath << Qt::endl;
        return 1;
    }
    out() << "pack: " << pack.files.size() << " files -> " << outPath << Qt::endl;
    return 0;
}

int runLicenses(const QString &packPath, const QStringList &confirmed)
{
    Pack pack;
    QString error;
    if (!loadPack(packPath, &pack, &error)) {
        out() << "error: " << error << Qt::endl;
        return 1;
    }
    const license::GateResult gate = license::evaluate(pack, confirmed);
    out() << gate.redistributionJson << Qt::endl;
    out() << "allowed " << gate.allowed.size() << ", needs-confirm " << gate.needsConfirm.size()
          << ", denied " << gate.denied.size() << " (denied groups "
          << gate.deniedGroups.size() << ", needs-confirm groups "
          << gate.needsConfirmGroups.size() << ")" << Qt::endl;
    return gate.exportable ? 0 : 4;
}

int runPackRender(const QString &packPath, const QString &format, const QString &outPath,
                  const QStringList &confirmed)
{
    Pack pack;
    QString error;
    if (!loadPack(packPath, &pack, &error)) {
        out() << "error: " << error << Qt::endl;
        return 1;
    }

    RenderOptions options;
    options.outputPath = outPath;
    options.confirmedPaths = confirmed;
    options.progress = [](int done, int total, const QString &step) {
        out() << "\r" << done << "/" << total << "  " << step << "              " << Qt::flush;
    };

    RenderResult result;
    if (format == QLatin1String("mrpack")) {
        result = renderMrpack(pack, options);
    } else if (format == QLatin1String("ftb")) {
        result = renderFtb(pack, options);
    } else if (format == QLatin1String("self")) {
        const license::GateResult gate = license::evaluate(pack, confirmed);
        if (!gate.denied.isEmpty()) {
            out() << Qt::endl << "error: 以下模组未确认再分发：" << Qt::endl;
            for (const license::GateGroup &group : gate.groups) {
                if (group.verdict != license::Verdict::Denied)
                    continue;
                out() << "  " << group.name << "  [" << group.id << "]  "
                      << group.pendingCount << " 个文件" << Qt::endl;
            }
            out() << "可用 --confirm <组 id> 一次确认整个模组；复查：bmcd --pack "
                  << packPath << " --licenses" << Qt::endl;
            return 4;
        }
        if (!gate.needsConfirm.isEmpty()) {
            out() << Qt::endl << "warning: " << gate.needsConfirm.size()
                  << " 个文件需要 --confirm 确认（copyleft）" << Qt::endl;
        }
        result = renderSelfContained(pack, options, gate);
    } else {
        out() << "error: 未知格式 " << format << "（mrpack | ftb | self）" << Qt::endl;
        return 1;
    }

    out() << Qt::endl;
    if (!result.ok) {
        out() << "error: " << result.error << Qt::endl;
        return 1;
    }
    out() << "written " << outPath << " (" << formatBytes(result.bytes) << ", " << result.fileCount
          << " entries)" << Qt::endl;
    return 0;
}

int runServe(const QString &packPath, const QString &targetDir, bool dryRun, bool backup,
             bool skipOptional, const QStringList &confirmed)
{
    Pack pack;
    QString error;
    if (!loadPack(packPath, &pack, &error)) {
        out() << "error: " << error << Qt::endl;
        return 1;
    }

    DeployOptions options;
    options.targetDir = targetDir.isEmpty() ? pack.target : targetDir;
    options.sourceRoot = pack.target;
    options.dryRun = dryRun;
    options.backup = backup;
    options.skipOptional = skipOptional;
    options.progress = [](int done, int total, const QString &path) {
        out() << "\r" << done << "/" << total << "  " << path << "                 " << Qt::flush;
    };

    const DeployReport report = deploy(pack, options, confirmed);
    out() << Qt::endl;
    for (const QString &conflict : report.conflicts)
        out() << "  conflict " << conflict << Qt::endl;
    for (const QString &failure : report.failures)
        out() << "  " << failure << Qt::endl;

    if (!report.ok) {
        out() << "error: " << report.error << Qt::endl;
        return 2;
    }
    if (dryRun) {
        out() << "dry run ok: " << pack.files.size() << " files, "
              << report.conflicts.size() << " conflicts, " << formatBytes(pack.totalBytes()) << Qt::endl;
        return 0;
    }
    out() << "served " << report.deployed << " files (" << formatBytes(report.bytes) << ") -> "
          << options.targetDir << ", skipped " << report.skipped;
    if (!report.backupDir.isEmpty() && QFileInfo::exists(report.backupDir))
        out() << ", backup " << report.backupDir;
    out() << Qt::endl;
    return 0;
}

int runVerify(const QString &packPath, const QString &dir)
{
    Pack pack;
    QString error;
    if (!loadPack(packPath, &pack, &error)) {
        out() << "error: " << error << Qt::endl;
        return 1;
    }
    const VerifyReport report = verifyDeployment(pack, dir);
    for (const QString &problem : report.problems)
        out() << "  " << problem << Qt::endl;
    out() << "present " << report.present << ", missing " << report.missing << ", corrupt "
          << report.corrupt << Qt::endl;
    return report.ok ? 0 : 5;
}

} // namespace

int main(int argc, char *argv[])
{
    QElapsedTimer startupTimer;
    startupTimer.start();
    if (isHeadlessInvocation(argc, argv))
        return runHeadless(argc, argv);

    QGuiApplication app(argc, argv);
    setCoreMetadata();
    QGuiApplication::setApplicationDisplayName(QStringLiteral("Bye!MinecraftDownloader"));
    // Ties the window to the desktop entry and AppStream metadata shipped by
    // the AppImage and the Flatpak.
    QGuiApplication::setDesktopFileName(QStringLiteral("org.bmcd.BMCD"));
    installFileLog();
    platform::installSystemProxy();

    QCommandLineParser parser;
    addOptions(parser);
    parser.process(app);

    const QString target = parser.isSet(QStringLiteral("target"))
            ? parser.value(QStringLiteral("target"))
            : AppSettings::resolvedTargetDir();
    if (parser.isSet(QStringLiteral("target")))
        qputenv("MC_TARGET", target.toUtf8());

    // Language: explicit --lang, then BMCD_LANG, then the persisted setting,
    // then the system locale. Chinese is the source and needs no catalog.
    AppSettings appSettings;
    QString language = parser.value(QStringLiteral("lang"));
    if (language.isEmpty())
        language = qEnvironmentVariable("BMCD_LANG");
    if (language.isEmpty())
        language = appSettings.language();
    QTranslator translator;
    const auto applyLanguage = [&](const QString &code) {
        const bool english = code.startsWith(QLatin1String("en"), Qt::CaseInsensitive);
        if (english) {
            if (!translator.load(QStringLiteral(":/i18n/bmcd_en.qm")))
                qWarning("BMCD: could not load the English translation");
            app.installTranslator(&translator);
        } else {
            app.removeTranslator(&translator);
        }
    };
    applyLanguage(language);

    // ── UI ──────────────────────────────────────────────────────────────
    QQmlApplicationEngine engine;
    qmlRegisterSingletonInstance("BMCD.Settings", 1, 0, "AppSettings", &appSettings);
    QObject::connect(&appSettings, &AppSettings::languageChanged, &engine, [&] {
        const QString code = appSettings.language();
        applyLanguage(code);
        engine.retranslate();
        engine.rootContext()->setContextProperty(
                QStringLiteral("uiLanguage"),
                code.startsWith(QLatin1String("en"), Qt::CaseInsensitive)
                        ? QStringLiteral("English") : QStringLiteral("中文"));
    });
    engine.addImageProvider(QStringLiteral("bmcdicon"), new IconImageProvider);

    // Deep link / screenshot support: preselect a version when the window opens.
    engine.rootContext()->setContextProperty(QStringLiteral("initialVersion"),
                                             parser.isSet(QStringLiteral("select"))
                                                     ? parser.value(QStringLiteral("select"))
                                                     : QString());
    engine.rootContext()->setContextProperty(QStringLiteral("autoStartDownload"),
                                             qEnvironmentVariableIsSet("MC_AUTOSTART"));
    QSize initialSize(1120, 760);
    const QStringList sizeParts = parser.value(QStringLiteral("size")).split(QLatin1Char('x'));
    if (sizeParts.size() == 2 && sizeParts.at(0).toInt() > 0 && sizeParts.at(1).toInt() > 0)
        initialSize = QSize(sizeParts.at(0).toInt(), sizeParts.at(1).toInt());
    engine.rootContext()->setContextProperty(QStringLiteral("initialWidth"), initialSize.width());
    engine.rootContext()->setContextProperty(QStringLiteral("initialHeight"), initialSize.height());
    engine.rootContext()->setContextProperty(QStringLiteral("homeDir"), QDir::homePath());

    const QString view = parser.value(QStringLiteral("view"));
    int initialSection = view == QLatin1String("cook") ? 1
                       : view == QLatin1String("serve") ? 2
                       : view == QLatin1String("settings") ? 3
                                                           : 0;
    engine.rootContext()->setContextProperty(QStringLiteral("initialSection"), initialSection);
    engine.rootContext()->setContextProperty(QStringLiteral("uiLanguage"),
                                             language.startsWith(QLatin1String("en"), Qt::CaseInsensitive)
                                                     ? QStringLiteral("English")
                                                     : QStringLiteral("中文"));

    // --benchmark: one timing line for the 1.0 performance budget. Runs with the
    // offscreen platform on CI (`QT_QPA_PLATFORM=offscreen bmcd --benchmark`).
    if (parser.isSet(QStringLiteral("benchmark"))) {
        QObject::connect(
                &engine,
                &QQmlApplicationEngine::objectCreated,
                &app,
                [&app, &startupTimer](QObject *object, const QUrl &) {
                    auto *window = qobject_cast<QQuickWindow *>(object);
                    if (!window)
                        return;
                    const qint64 createdMs = startupTimer.elapsed();
                    QObject::connect(window, &QQuickWindow::frameSwapped, &app, [&app, &startupTimer, createdMs] {
                        qint64 rssKb = 0;
                        QFile status(QStringLiteral("/proc/self/status"));
                        if (status.open(QIODevice::ReadOnly)) {
                            for (const QByteArray &line : status.readAll().split('\n')) {
                                if (line.startsWith("VmRSS:")) {
                                    rssKb = line.mid(6).trimmed().split(' ').value(0).toLongLong();
                                    break;
                                }
                            }
                        }
                        out() << "benchmark: first-frame " << startupTimer.elapsed()
                              << " ms, window-created " << createdMs << " ms, rss "
                              << (rssKb / 1024) << " MB" << Qt::endl;
                        app.quit();
                    });
                },
                Qt::QueuedConnection);
    }

    if (parser.isSet(QStringLiteral("dump-layout"))) {
        QObject::connect(
                &engine,
                &QQmlApplicationEngine::objectCreated,
                &app,
                [&app](QObject *object, const QUrl &) {
                    auto *window = qobject_cast<QQuickWindow *>(object);
                    if (!window)
                        return;
                    const int delay = qEnvironmentVariableIntValue("MC_DUMP_DELAY") > 0
                            ? qEnvironmentVariableIntValue("MC_DUMP_DELAY") : 800;
                    QTimer::singleShot(delay, window, [window, &app] {
                        QTextStream out(stdout);
                        dumpItemTree(window->contentItem(), 0, out);
                        app.quit();
                    });
                },
                Qt::QueuedConnection);
    }

    // MC_SCREENSHOT=<path> renders one frame and exits. Handy for visual checks
    // on headless machines (pair with QT_QPA_PLATFORM=offscreen).
    const QString screenshotPath = qEnvironmentVariable("MC_SCREENSHOT");
    if (!screenshotPath.isEmpty()) {
        QObject::connect(
                &engine,
                &QQmlApplicationEngine::objectCreated,
                &app,
                [&app, screenshotPath](QObject *object, const QUrl &) {
                    auto *window = qobject_cast<QQuickWindow *>(object);
                    if (!window)
                        return;
                    QTimer::singleShot(qEnvironmentVariableIntValue("MC_SCREENSHOT_DELAY") > 0
                                               ? qEnvironmentVariableIntValue("MC_SCREENSHOT_DELAY")
                                               : 1200,
                                       window, [window, &app, screenshotPath] {
                        const QImage frame = window->grabWindow();
                        if (frame.isNull() || !frame.save(screenshotPath))
                            qWarning("screenshot failed: %s", qPrintable(screenshotPath));
                        else
                            qInfo("screenshot saved: %s (%dx%d)", qPrintable(screenshotPath),
                                  frame.width(), frame.height());
                        app.quit();
                    });
                },
                Qt::QueuedConnection);
    }

    QObject::connect(
            &engine,
            &QQmlApplicationEngine::objectCreationFailed,
            &app,
            [] {
                qWarning("BMCD: failed to create the main window (loadFromModule failed)");
                QCoreApplication::exit(-1);
            },
            Qt::QueuedConnection);

    engine.addImportPath(QStringLiteral(":/qt/qml"));
    engine.loadFromModule("BMCD.UI", "Main");

    return app.exec();
}
