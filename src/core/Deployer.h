#pragma once

#include "core/Pack.h"

#include <QString>
#include <QStringList>

#include <functional>

// Deploys a pack into a game directory (the "serve to folder" exit).
//
// Safety rules that the tests pin down:
//  * every path is validated with isSafeRelativePath (no zip-slip, no absolute)
//  * nothing is written outside the target directory
//  * files are staged first, then moved into place; an existing file is backed
//    up to .bmcd-backup-<id>/ and restored if a later step fails
//  * a missing or corrupt cache file aborts before the target is touched
struct DeployOptions
{
    QString targetDir;
    QString sourceRoot;              // where files are read from (defaults to Pack::target)
    bool overwrite = true;
    bool backup = true;
    bool dryRun = false;
    bool skipOptional = false;
    std::function<void(int done, int total, const QString &path)> progress;
};

struct DeployReport
{
    bool ok = false;
    int deployed = 0;
    int skipped = 0;
    int failed = 0;
    qint64 bytes = 0;
    QString error;
    QStringList conflicts;           // existing files that would be replaced
    QStringList failures;            // per-file failure messages
    QString stagingDir;
    QString backupDir;
    QStringList rolledBack;
};

// `confirmed` marks optional files the user chose to include.
DeployReport deploy(const Pack &pack, const DeployOptions &options,
                    const QStringList &confirmed = {});

// Verifies every byte of an existing deployment against the pack hashes.
struct VerifyReport
{
    bool ok = false;
    int present = 0;
    int missing = 0;
    int corrupt = 0;
    QStringList problems;
};
VerifyReport verifyDeployment(const Pack &pack, const QString &targetDir);

QString sha1OfFile(const QString &path);
