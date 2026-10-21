#include "core/LicenseGate.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace license {
namespace {

const QStringList kPermissive{
    QStringLiteral("mit"),           QStringLiteral("apache-2.0"),
    QStringLiteral("bsd-2-clause"),  QStringLiteral("bsd-3-clause"),
    QStringLiteral("zlib"),          QStringLiteral("cc0-1.0"),
    QStringLiteral("unlicense"),     QStringLiteral("isc"),
    QStringLiteral("mpl-2.0"),
};

// Copyleft licences are redistributable as part of a modpack, but the user must
// acknowledge it (and the NOTICE carries the source link).
const QStringList kCopyleft{
    QStringLiteral("gpl-2.0"),       QStringLiteral("gpl-2.0-only"),
    QStringLiteral("gpl-2.0-or-later"), QStringLiteral("gpl-3.0"),
    QStringLiteral("gpl-3.0-only"),  QStringLiteral("gpl-3.0-or-later"),
    QStringLiteral("lgpl-2.1"),      QStringLiteral("lgpl-3.0"),
    QStringLiteral("lgpl-3.0-only"), QStringLiteral("agpl-3.0"),
    QStringLiteral("agpl-3.0-only"), QStringLiteral("cc-by-sa-4.0"),
};

QString normalize(const QString &id)
{
    return id.trimmed().toLower();
}

QString mdEscape(QString text)
{
    text.replace(QLatin1Char('|'), QLatin1String("\\|"));
    text.replace(QLatin1Char('\n'), QLatin1Char(' '));
    return text;
}

} // namespace

QString verdictName(Verdict verdict)
{
    switch (verdict) {
    case Verdict::Allowed:
        return QStringLiteral("allowed");
    case Verdict::NeedsConfirm:
        return QStringLiteral("needs-confirm");
    case Verdict::Denied:
        return QStringLiteral("denied");
    }
    return QStringLiteral("denied");
}

bool isPermissive(const QString &licenseId)
{
    return kPermissive.contains(normalize(licenseId));
}

bool isCopyleft(const QString &licenseId)
{
    return kCopyleft.contains(normalize(licenseId));
}

Verdict classify(const PackFile &file)
{
    const QString id = normalize(file.licenseId);
    if (id.isEmpty() || id.startsWith(QLatin1String("licenseref-")))
        return Verdict::Denied;
    // A file whose licence is not recorded in the project metadata cannot be
    // redistributed blindly, even if the id looks familiar.
    if (isPermissive(id))
        return Verdict::Allowed;
    if (isCopyleft(id))
        return Verdict::NeedsConfirm;
    return Verdict::Denied;
}

GateResult evaluate(const Pack &pack, const QStringList &confirmed)
{
    GateResult result;
    const QSet<QString> confirmedSet(confirmed.begin(), confirmed.end());

    QStringList noticeRows;
    QJsonArray records;
    for (const PackFile &file : pack.files) {
        Verdict verdict = classify(file);
        if (verdict != Verdict::Allowed && confirmedSet.contains(file.path))
            verdict = Verdict::Allowed;

        result.verdicts.insert(file.path, verdict);
        switch (verdict) {
        case Verdict::Allowed:
            result.allowed.append(file.path);
            break;
        case Verdict::NeedsConfirm:
            result.needsConfirm.append(file.path);
            break;
        case Verdict::Denied:
            result.denied.append(file.path);
            break;
        }

        records.append(QJsonObject{
            {QStringLiteral("path"), file.path},
            {QStringLiteral("licenseId"), file.licenseId},
            {QStringLiteral("licenseName"), file.licenseName},
            {QStringLiteral("licenseUrl"), file.licenseUrl},
            {QStringLiteral("sourceUrl"), file.sourceUrl},
            {QStringLiteral("author"), file.author},
            {QStringLiteral("verdict"), verdictName(verdict)},
            {QStringLiteral("confirmed"), confirmedSet.contains(file.path)},
            {QStringLiteral("sha1"), file.sha1},
        });

        noticeRows.append(QStringLiteral("| `%1` | %2 | %3 | %4 |")
                               .arg(mdEscape(file.path), mdEscape(file.licenseId.isEmpty()
                                                                      ? QStringLiteral("未标注")
                                                                      : file.licenseId),
                                    mdEscape(file.author.isEmpty() ? QStringLiteral("未知")
                                                                   : file.author),
                                    file.sourceUrl.isEmpty()
                                            ? QString()
                                            : QStringLiteral("[来源](%1)").arg(file.sourceUrl)));
    }

    result.exportable = result.denied.isEmpty();

    QString notice = QStringLiteral("# NOTICE\n\n本整合包重新分发了以下文件。各文件版权归原作者所有，"
                                    "许可证与来源见下表。\n\n");
    notice += QStringLiteral("| 文件 | 许可证 | 作者 | 来源 |\n| --- | --- | --- | --- |\n");
    notice += noticeRows.join(QLatin1Char('\n'));
    notice += QStringLiteral("\n\n本文件由 BMCD 自动生成。若你是权利人不希望自己的作品被重新分发，"
                             "请联系整合包作者移除对应条目。\n");
    result.noticeMarkdown = notice;

    const QJsonObject root{
        {QStringLiteral("generatedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
        {QStringLiteral("generator"), QStringLiteral("BMCD ") + pack.generator},
        {QStringLiteral("pack"), pack.name},
        {QStringLiteral("packVersion"), pack.versionId},
        {QStringLiteral("minecraft"), pack.minecraftVersion},
        {QStringLiteral("files"), records},
    };
    result.redistributionJson = QJsonDocument(root).toJson(QJsonDocument::Indented);
    return result;
}

} // namespace license
