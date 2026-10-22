#pragma once

#include "core/Pack.h"

#include <QHash>
#include <QStringList>

// Distribution gate for self-contained exports. Only content files (mods,
// configs, resource packs, shaders, scripts) are shipped inside the pack, so
// only those need a decision; it is recorded in redistribution.json and
// rendered into NOTICE.md. Client jars, libraries and assets stay launcher-side.
namespace license {

enum class Verdict
{
    Allowed,      // permissive, auto-approved
    NeedsConfirm, // copyleft: allowed only with an explicit user click
    Denied,       // unknown / no licence / LicenseRef-*: blocked
};

QString verdictName(Verdict verdict);

// Allowlist of permissive SPDX ids (case-insensitive, aliases normalised).
bool isPermissive(const QString &licenseId);
bool isCopyleft(const QString &licenseId);

Verdict classify(const PackFile &file);

// Authorisation is per parent project, not per file: one click covers every
// artefact that belongs to the same mod.
struct GateGroup
{
    QString id;        // stable key: @project:<id>, @minecraft, @mod:<name>, ...
    QString kind;      // project | minecraft | mod | directory | file
    QString name;      // display name (project title, jar stem, directory)
    QString licenseId;
    QString licenseName;
    QString licenseUrl;
    QString sourceUrl;
    QString author;
    Verdict verdict = Verdict::Allowed; // worst verdict of the files in the group
    int fileCount = 0;
    int pendingCount = 0;               // files still denied or needs-confirm
    QStringList paths;
};

struct GateResult
{
    QHash<QString, Verdict> verdicts; // keyed by PackFile::path
    QHash<QString, QString> groupOf;  // PackFile::path -> GateGroup::id
    QVector<GateGroup> groups;
    QStringList allowed;
    QStringList needsConfirm;
    QStringList denied;
    QStringList deniedGroups;         // ids of groups with a denied file
    QStringList needsConfirmGroups;   // ids of groups needing confirmation
    QString noticeMarkdown;
    QByteArray redistributionJson;
    bool exportable = false;          // true when nothing is denied
};

// `confirmed` holds paths or group ids the user explicitly approved on the
// serve screen. Group ids cover every file of that parent mod at once.
GateResult evaluate(const Pack &pack, const QStringList &confirmed = {});

} // namespace license
