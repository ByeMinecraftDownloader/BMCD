#include "core/Renderers.h"

#include "core/ZipWriter.h"
#include "platform/FileOps.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace {

QString loaderDependencyKey(const QString &loaderType)
{
    if (loaderType == QLatin1String("fabric"))
        return QStringLiteral("fabric-loader");
    if (loaderType == QLatin1String("quilt"))
        return QStringLiteral("quilt-loader");
    if (loaderType == QLatin1String("forge"))
        return QStringLiteral("forge");
    if (loaderType == QLatin1String("neoforge"))
        return QStringLiteral("neoforge");
    return {};
}

QByteArray jsonBytes(const QJsonObject &object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Indented);
}

QString sha1OfFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha1);
    if (!hash.addData(&file))
        return {};
    return QString::fromLatin1(hash.result().toHex());
}

void report(RenderResult *result, const RenderOptions &options, int done, int total, const QString &step)
{
    if (options.progress)
        options.progress(done, total, step);
}

} // namespace

RenderResult renderMrpack(const Pack &pack, const RenderOptions &options)
{
    RenderResult result;
    if (options.outputPath.isEmpty()) {
        result.error = QStringLiteral("未指定输出文件");
        return result;
    }
    QString invalid;
    if (!pack.isValid(&invalid)) {
        result.error = invalid;
        return result;
    }

    const QVector<PackFile> content = pack.contentFiles();
    QJsonArray files;
    for (const PackFile &file : content) {
        QJsonArray downloads{file.url};
        QJsonObject env{{QStringLiteral("client"), file.clientSide ? QStringLiteral("required")
                                                                : QStringLiteral("unsupported")},
                        {QStringLiteral("server"), file.serverSide ? QStringLiteral("required")
                                                                  : QStringLiteral("unsupported")}};
        QJsonObject hashes{{QStringLiteral("sha1"), file.sha1}};
        if (!file.sha256.isEmpty())
            hashes.insert(QStringLiteral("sha256"), file.sha256);
        if (!file.sha512.isEmpty())
            hashes.insert(QStringLiteral("sha512"), file.sha512);
        files.append(QJsonObject{
            {QStringLiteral("path"), file.path},
            {QStringLiteral("hashes"), hashes},
            {QStringLiteral("env"), env},
            {QStringLiteral("downloads"), downloads},
            {QStringLiteral("fileSize"), double(file.size)},
        });
    }

    QJsonObject dependencies{{QStringLiteral("minecraft"), pack.minecraftVersion}};
    const QString loaderKey = loaderDependencyKey(pack.loaderType);
    if (!loaderKey.isEmpty() && !pack.loaderVersion.isEmpty())
        dependencies.insert(loaderKey, pack.loaderVersion);

    const QJsonObject index{
        {QStringLiteral("formatVersion"), 1},
        {QStringLiteral("game"), QStringLiteral("minecraft")},
        {QStringLiteral("versionId"), pack.versionId.isEmpty() ? QStringLiteral("1.0.0") : pack.versionId},
        {QStringLiteral("name"), pack.name},
        {QStringLiteral("summary"), pack.description},
        {QStringLiteral("files"), files},
        {QStringLiteral("dependencies"), dependencies},
    };

    ZipWriter writer(options.outputPath);
    if (!writer.addData(QStringLiteral("modrinth.index.json"), jsonBytes(index), true)) {
        result.error = writer.errorString();
        return result;
    }
    // Bundled files from an imported pack travel under overrides/, exactly like
    // the Modrinth format expects.
    for (const QString &relative : pack.overrides) {
        const QString source = QDir(pack.overridesDir).filePath(relative);
        if (!writer.addFileFromPath(QStringLiteral("overrides/") + relative, source, true)) {
            result.error = writer.errorString();
            return result;
        }
    }
    report(&result, options, 1, 1, QStringLiteral("写入 modrinth.index.json"));
    if (!writer.close()) {
        result.error = writer.errorString();
        return result;
    }
    result.ok = true;
    result.bytes = QFileInfo(options.outputPath).size();
    result.fileCount = 1 + int(pack.overrides.size()); // jars stay on their CDN urls
    return result;
}

RenderResult renderFtb(const Pack &pack, const RenderOptions &options)
{
    RenderResult result;
    if (options.outputPath.isEmpty()) {
        result.error = QStringLiteral("未指定输出文件");
        return result;
    }
    QString invalid;
    if (!pack.isValid(&invalid)) {
        result.error = invalid;
        return result;
    }

    const QVector<PackFile> content = pack.contentFiles();
    QJsonArray files;
    for (const PackFile &file : content) {
        files.append(QJsonObject{
            {QStringLiteral("path"), file.path},
            {QStringLiteral("url"), file.url},
            {QStringLiteral("sha1"), file.sha1},
            {QStringLiteral("size"), double(file.size)},
            {QStringLiteral("clientonly"), !file.serverSide},
            {QStringLiteral("serveronly"), !file.clientSide},
            {QStringLiteral("optional"), file.optional},
        });
    }
    const QJsonObject manifest{
        {QStringLiteral("name"), pack.name},
        {QStringLiteral("version"), pack.versionId.isEmpty() ? QStringLiteral("1.0.0") : pack.versionId},
        {QStringLiteral("targets"),
         QJsonArray{QJsonObject{
             {QStringLiteral("type"), QStringLiteral("game")},
             {QStringLiteral("version"), pack.minecraftVersion},
             {QStringLiteral("name"), pack.minecraftVersion},
             {QStringLiteral("id"), pack.minecraftVersion},
         }}},
        {QStringLiteral("files"), files},
    };

    ZipWriter writer(options.outputPath);
    bool overridesOk = true;
    for (const QString &relative : pack.overrides) {
        const QString source = QDir(pack.overridesDir).filePath(relative);
        if (!writer.addFileFromPath(QStringLiteral("overrides/") + relative, source, true)) {
            overridesOk = false;
            break;
        }
    }
    if (!overridesOk) {
        result.error = writer.errorString();
        return result;
    }
    if (!writer.addData(QStringLiteral("manifest.json"), jsonBytes(manifest), true)
        || !writer.addData(QStringLiteral("modlist.html"),
                           QStringLiteral("<html><body><h1>%1</h1><ul></ul></body></html>")
                                   .arg(pack.name.toHtmlEscaped())
                                   .toUtf8(),
                           true)
        || !writer.close()) {
        result.error = writer.errorString();
        return result;
    }
    result.ok = true;
    result.bytes = QFileInfo(options.outputPath).size();
    result.fileCount = int(content.size()) + int(pack.overrides.size()) + 2;
    return result;
}

RenderResult renderSelfContained(const Pack &pack, const RenderOptions &options,
                                 const license::GateResult &gate)
{
    RenderResult result;
    if (options.outputPath.isEmpty()) {
        result.error = QStringLiteral("未指定输出文件");
        return result;
    }
    if (!gate.denied.isEmpty()) {
        result.error = QStringLiteral("有 %1 个文件未获得再分发许可").arg(gate.denied.size());
        return result;
    }
    QString invalid;
    if (!pack.isValid(&invalid)) {
        result.error = invalid;
        return result;
    }

    const QString sourceRoot = options.sourceRoot.isEmpty() ? pack.target : options.sourceRoot;
    const QVector<PackFile> content = pack.contentFiles();
    const int total = int(content.size()) + int(pack.overrides.size()) + 3;

    ZipWriter writer(options.outputPath);
    if (!writer.addData(QStringLiteral("NOTICE.md"), gate.noticeMarkdown.toUtf8(), true)
        || !writer.addData(QStringLiteral("redistribution.json"), gate.redistributionJson, true)) {
        result.error = writer.errorString();
        return result;
    }
    int done = 2;
    for (const PackFile &file : content) {
        const QString source = QDir(sourceRoot).filePath(file.path);
        const QFileInfo info(source);
        if (!info.exists() || (file.size > 0 && info.size() != file.size)
            || (!file.sha1.isEmpty() && sha1OfFile(source) != file.sha1)) {
            result.error = QStringLiteral("缓存缺失或校验失败：%1").arg(file.path);
            return result;
        }
        const QString entry = QStringLiteral("overrides/") + file.path;
        // Jars are already compressed: store to keep export fast.
        const bool compress = !entry.endsWith(QLatin1String(".jar")) && !entry.endsWith(QLatin1String(".zip"));
        if (!writer.addFileFromPath(entry, source, compress)) {
            result.error = writer.errorString();
            return result;
        }
        report(&result, options, ++done, total, file.path);
    }

    for (const QString &relative : pack.overrides) {
        const QString source = QDir(pack.overridesDir).filePath(relative);
        if (!writer.addFileFromPath(QStringLiteral("overrides/") + relative, source, true)) {
            result.error = writer.errorString();
            return result;
        }
        report(&result, options, ++done, total, relative);
    }

    // A self-contained package is a valid Modrinth pack with every file moved
    // into overrides/ and an empty remote file list: launchers and BMCD itself
    // install it without any network access.
    QJsonObject dependencies{{QStringLiteral("minecraft"), pack.minecraftVersion}};
    if (!pack.loaderType.isEmpty() && !pack.loaderVersion.isEmpty())
        dependencies.insert(pack.loaderType + QLatin1String("-loader"), pack.loaderVersion);
    const QJsonObject index{
        {QStringLiteral("formatVersion"), 1},
        {QStringLiteral("game"), QStringLiteral("minecraft")},
        {QStringLiteral("versionId"), pack.versionId.isEmpty() ? QStringLiteral("1.0.0") : pack.versionId},
        {QStringLiteral("name"), pack.name},
        {QStringLiteral("summary"), QStringLiteral("Self-contained BMCD export")},
        {QStringLiteral("files"), QJsonArray{}},
        {QStringLiteral("dependencies"), dependencies},
    };
    if (!writer.addData(QStringLiteral("modrinth.index.json"), jsonBytes(index), true) || !writer.close()) {
        result.error = writer.errorString();
        return result;
    }
    result.ok = true;
    result.bytes = QFileInfo(options.outputPath).size();
    result.fileCount = total;
    return result;
}
