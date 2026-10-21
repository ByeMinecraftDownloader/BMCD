#pragma once

#include <QByteArray>
#include <QString>

namespace platform {

// Replace `target` with `source` in one step. On POSIX this is rename(2); on
// Windows MoveFileEx(MOVEFILE_REPLACE_EXISTING) which fails with sharing
// violations while an antivirus scanner still holds the destination open, so
// the Windows path retries with backoff instead of giving up on the first try.
bool replaceFile(const QString &source, const QString &target);

// Write `data` to `path` atomically (QSaveFile), creating parent directories.
bool writeFileAtomic(const QString &path, const QByteArray &data);

bool removeIfExists(const QString &path);

// Copy a file with the same retry behaviour as replaceFile (used by the
// deployer when the target lives on another filesystem).
bool copyFile(const QString &source, const QString &target);

// Windows: prefix absolute paths longer than MAX_PATH with \\?\ so Qt and the
// Win32 APIs stop truncating them. Other platforms: identity.
QString nativePath(const QString &path);

} // namespace platform
