#include "core/Pack.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>

namespace {

QJsonObject fileToJson(const PackFile &file)
{
    QJsonObject object{
        {QStringLiteral("path"), file.path},
        {QStringLiteral("url"), file.url},
        {QStringLiteral("sha1"), file.sha1},
        {QStringLiteral("sha512"), file.sha512},
        {QStringLiteral("size"), double(file.size)},
        {QStringLiteral("kind"), file.kind},
        {QStringLiteral("optional"), file.optional},
        {QStringLiteral("clientSide"), file.clientSide},
        {QStringLiteral("serverSide"), file.serverSide},
    };
    if (!file.licenseId.isEmpty())
        object.insert(QStringLiteral("licenseId"), file.licenseId);
    if (!file.licenseName.isEmpty())
        object.insert(QStringLiteral("licenseName"), file.licenseName);
    if (!file.licenseUrl.isEmpty())
        object.insert(QStringLiteral("licenseUrl"), file.licenseUrl);
    if (!file.sourceUrl.isEmpty())
        object.insert(QStringLiteral("sourceUrl"), file.sourceUrl);
    if (!file.author.isEmpty())
        object.insert(QStringLiteral("author"), file.author);
    if (!file.projectId.isEmpty())
        object.insert(QStringLiteral("projectId"), file.projectId);
    if (!file.fileId.isEmpty())
        object.insert(QStringLiteral("fileId"), file.fileId);
    return object;
}

PackFile fileFromJson(const QJsonObject &object)
{
    PackFile file;
    file.path = object.value(QStringLiteral("path")).toString();
    file.url = object.value(QStringLiteral("url")).toString();
    file.sha1 = object.value(QStringLiteral("sha1")).toString().toLower();
    file.sha512 = object.value(QStringLiteral("sha512")).toString().toLower();
    file.size = qint64(object.value(QStringLiteral("size")).toDouble());
    file.kind = object.value(QStringLiteral("kind")).toString(QStringLiteral("mod"));
    file.optional = object.value(QStringLiteral("optional")).toBool(false);
    file.clientSide = object.value(QStringLiteral("clientSide")).toBool(true);
    file.serverSide = object.value(QStringLiteral("serverSide")).toBool(true);
    file.licenseId = object.value(QStringLiteral("licenseId")).toString();
    file.licenseName = object.value(QStringLiteral("licenseName")).toString();
    file.licenseUrl = object.value(QStringLiteral("licenseUrl")).toString();
    file.sourceUrl = object.value(QStringLiteral("sourceUrl")).toString();
    file.author = object.value(QStringLiteral("author")).toString();
    file.projectId = object.value(QStringLiteral("projectId")).toString();
    file.fileId = object.value(QStringLiteral("fileId")).toString();
    return file;
}

} // namespace

QJsonObject Pack::toJson() const
{
    QJsonArray array;
    for (const PackFile &file : files)
        array.append(fileToJson(file));

    QJsonObject object{
        {QStringLiteral("format"), format},
        {QStringLiteral("name"), name},
        {QStringLiteral("versionId"), versionId},
        {QStringLiteral("minecraftVersion"), minecraftVersion},
        {QStringLiteral("loaderType"), loaderType},
        {QStringLiteral("loaderVersion"), loaderVersion},
        {QStringLiteral("target"), target},
        {QStringLiteral("description"), description},
        {QStringLiteral("iconUrl"), iconUrl},
        {QStringLiteral("generator"), generator},
        {QStringLiteral("overridesDir"), overridesDir},
        {QStringLiteral("files"), array},
    };
    if (!overrides.isEmpty()) {
        QJsonArray overrideArray;
        for (const QString &path : overrides)
            overrideArray.append(path);
        object.insert(QStringLiteral("overrides"), overrideArray);
    }
    return object;
}

Pack Pack::fromJson(const QJsonObject &object)
{
    Pack pack;
    pack.format = object.value(QStringLiteral("format")).toInt(1);
    pack.name = object.value(QStringLiteral("name")).toString();
    pack.versionId = object.value(QStringLiteral("versionId")).toString();
    pack.minecraftVersion = object.value(QStringLiteral("minecraftVersion")).toString();
    pack.loaderType = object.value(QStringLiteral("loaderType")).toString();
    pack.loaderVersion = object.value(QStringLiteral("loaderVersion")).toString();
    pack.target = object.value(QStringLiteral("target")).toString();
    pack.description = object.value(QStringLiteral("description")).toString();
    pack.iconUrl = object.value(QStringLiteral("iconUrl")).toString();
    pack.generator = object.value(QStringLiteral("generator")).toString();
    for (const QJsonValue &value : object.value(QStringLiteral("files")).toArray())
        pack.files.append(fileFromJson(value.toObject()));
    pack.overridesDir = object.value(QStringLiteral("overridesDir")).toString();
    for (const QJsonValue &value : object.value(QStringLiteral("overrides")).toArray())
        pack.overrides.append(value.toString());
    return pack;
}

bool Pack::isValid(QString *error) const
{
    const auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };
    if (name.isEmpty())
        return fail(QStringLiteral("缺少名称"));
    if (minecraftVersion.isEmpty())
        return fail(QStringLiteral("缺少 Minecraft 版本"));
    if (files.isEmpty() && overrides.isEmpty())
        return fail(QStringLiteral("包内没有文件"));

    QSet<QString> seen;
    for (const PackFile &file : files) {
        if (!isSafeRelativePath(file.path))
            return fail(QStringLiteral("不安全的路径：%1").arg(file.path));
        if (seen.contains(file.path))
            return fail(QStringLiteral("重复路径：%1").arg(file.path));
        seen.insert(file.path);
        if (file.sha1.isEmpty() && file.sha512.isEmpty() && !file.url.isEmpty())
            return fail(QStringLiteral("%1 缺少校验值").arg(file.path));
    }
    for (const QString &path : overrides) {
        if (!isSafeRelativePath(path))
            return fail(QStringLiteral("不安全的路径：%1").arg(path));
        if (seen.contains(path))
            return fail(QStringLiteral("重复路径：%1").arg(path));
        seen.insert(path);
    }
    return true;
}

qint64 Pack::totalBytes() const
{
    qint64 total = 0;
    for (const PackFile &file : files)
        total += file.size;
    if (!overridesDir.isEmpty()) {
        for (const QString &path : overrides)
            total += QFileInfo(QDir(overridesDir).filePath(path)).size();
    }
    return total;
}

int Pack::fileCount() const
{
    return int(files.size() + overrides.size());
}

QVector<PackFile> Pack::contentFiles() const
{
    QVector<PackFile> content;
    for (const PackFile &file : files) {
        if (file.kind == QLatin1String("mod") || file.kind == QLatin1String("config")
            || file.kind == QLatin1String("resource") || file.kind == QLatin1String("shader")
            || file.kind == QLatin1String("script"))
            content.append(file);
    }
    return content;
}

bool isSafeRelativePath(const QString &path)
{
    if (path.isEmpty() || path.size() > 512)
        return false;
    if (path.startsWith(QLatin1Char('/')) || path.startsWith(QLatin1String("\\\\"))
        || path.startsWith(QLatin1String("~")))
        return false;
    if (QRegularExpression(QStringLiteral("^[A-Za-z]:")).match(path).hasMatch())
        return false;

    static const QRegularExpression control(QStringLiteral("[\\x00-\\x1f]"));
    if (path.contains(control))
        return false;

    const QStringList segments = path.split(QLatin1Char('/'));
    for (const QString &segment : segments) {
        if (segment.isEmpty() || segment == QLatin1String(".") || segment == QLatin1String(".."))
            return false;
        if (segment.size() > 128)
            return false;
    }
    return true;
}
