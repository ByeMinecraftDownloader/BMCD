#include "core/LicenseGate.h"

#include <QCryptographicHash>
#include <QFileInfo>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <utility>

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

QString groupIdFor(const PackFile &file)
{
    if (!file.projectId.isEmpty())
        return QStringLiteral("@project:") + file.projectId;
    if (file.kind == QLatin1String("client") || file.kind == QLatin1String("lib")
        || file.kind == QLatin1String("library") || file.kind == QLatin1String("asset"))
        return QStringLiteral("@minecraft");
    if (file.kind == QLatin1String("mod") || file.kind.isEmpty())
        return QStringLiteral("@mod:") + QFileInfo(file.path).completeBaseName();
    const QString top = file.path.section(QLatin1Char('/'), 0, 0);
    if (!top.isEmpty() && top != file.path)
        return QStringLiteral("@dir:") + top;
    return QStringLiteral("@file:") + file.path;
}

QString groupKindFor(const PackFile &file, const QString &groupId)
{
    if (groupId == QLatin1String("@minecraft"))
        return QStringLiteral("minecraft");
    if (groupId.startsWith(QLatin1String("@project:")))
        return QStringLiteral("project");
    if (groupId.startsWith(QLatin1String("@mod:")))
        return QStringLiteral("mod");
    if (groupId.startsWith(QLatin1String("@dir:")))
        return QStringLiteral("directory");
    Q_UNUSED(file)
    return QStringLiteral("file");
}

QString groupNameFor(const PackFile &file, const QString &kind)
{
    if (kind == QLatin1String("minecraft"))
        return QStringLiteral("Minecraft");
    if (kind == QLatin1String("project")) {
        if (!file.projectName.isEmpty())
            return file.projectName;
        if (file.kind == QLatin1String("mod") || file.kind.isEmpty())
            return QFileInfo(file.path).completeBaseName();
        return file.projectId;
    }
    if (kind == QLatin1String("mod"))
        return QFileInfo(file.path).completeBaseName();
    if (kind == QLatin1String("directory"))
        return file.path.section(QLatin1Char('/'), 0, 0);
    return file.path;
}

int verdictRank(Verdict verdict)
{
    switch (verdict) {
    case Verdict::Allowed:
        return 0;
    case Verdict::NeedsConfirm:
        return 1;
    case Verdict::Denied:
        return 2;
    }
    return 2;
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

    // Only files that the self-contained export actually redistributes are
    // gated. Client jars, libraries and assets are installed by the launcher,
    // not shipped inside the pack, so counting them as "denied" produced
    // thousands of meaningless entries for a plain version.
    const QVector<PackFile> content = pack.contentFiles();

    QHash<QString, int> groupIndex;
    QStringList noticeRows;
    QJsonArray records;
    for (const PackFile &file : content) {
        const QString groupId = groupIdFor(file);
        const QString groupKind = groupKindFor(file, groupId);
        int index = groupIndex.value(groupId, -1);
        if (index < 0) {
            index = result.groups.size();
            groupIndex.insert(groupId, index);
            GateGroup group;
            group.id = groupId;
            group.kind = groupKind;
            group.name = groupNameFor(file, groupKind);
            group.licenseId = file.licenseId;
            group.licenseName = file.licenseName;
            group.licenseUrl = file.licenseUrl;
            group.sourceUrl = file.sourceUrl;
            group.author = file.author;
            result.groups.append(group);
        }
        GateGroup &group = result.groups[index];
        if (group.name.isEmpty())
            group.name = groupNameFor(file, groupKind);

        // A group confirmation (one click for the parent mod) covers every
        // file of that group. A path confirmation remains supported for the
        // CLI and for older callers.
        const bool confirmedByGroup = confirmedSet.contains(groupId);
        const bool confirmedByPath = confirmedSet.contains(file.path);
        Verdict verdict = classify(file);
        if (verdict != Verdict::Allowed && (confirmedByGroup || confirmedByPath))
            verdict = Verdict::Allowed;

        result.verdicts.insert(file.path, verdict);
        result.groupOf.insert(file.path, groupId);
        group.paths.append(file.path);
        ++group.fileCount;
        if (verdict != Verdict::Allowed)
            ++group.pendingCount;
        // Show the licence of the most restrictive file in the group, not the
        // first one: a mixed MIT/unknown mod must read "unknown" before the
        // user confirms it.
        if (verdictRank(verdict) > verdictRank(group.verdict)) {
            group.verdict = verdict;
            group.licenseId = file.licenseId;
            group.licenseName = file.licenseName;
            group.licenseUrl = file.licenseUrl;
            group.sourceUrl = file.sourceUrl;
            group.author = file.author;
        }

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
            {QStringLiteral("group"), groupId},
            {QStringLiteral("groupName"), group.name},
            {QStringLiteral("licenseId"), file.licenseId},
            {QStringLiteral("licenseName"), file.licenseName},
            {QStringLiteral("licenseUrl"), file.licenseUrl},
            {QStringLiteral("sourceUrl"), file.sourceUrl},
            {QStringLiteral("author"), file.author},
            {QStringLiteral("verdict"), verdictName(verdict)},
            {QStringLiteral("confirmed"), confirmedByGroup || confirmedByPath},
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

    for (const GateGroup &group : std::as_const(result.groups)) {
        if (group.verdict == Verdict::Denied)
            result.deniedGroups.append(group.id);
        else if (group.verdict == Verdict::NeedsConfirm)
            result.needsConfirmGroups.append(group.id);
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
