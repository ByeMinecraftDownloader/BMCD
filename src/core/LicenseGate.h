#pragma once

#include "core/Pack.h"

#include <QHash>
#include <QStringList>

// Distribution gate for self-contained exports. The pack ships the mod jars
// themselves, so every file needs a decision that is recorded in
// redistribution.json and rendered into NOTICE.md.
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

struct GateResult
{
    QHash<QString, Verdict> verdicts; // keyed by PackFile::path
    QStringList allowed;
    QStringList needsConfirm;
    QStringList denied;
    QString noticeMarkdown;
    QByteArray redistributionJson;
    bool exportable = false;          // true when nothing is denied
};

// `confirmed` holds paths the user explicitly approved on the serve screen.
GateResult evaluate(const Pack &pack, const QStringList &confirmed = {});

} // namespace license
