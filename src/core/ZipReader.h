#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

class QJsonObject;

// Read-only half of the ZIP story (ZipWriter is the write half). Importing a
// .mrpack / CurseForge / FTB archive needs to read a handful of JSON manifests
// plus the overrides tree, so this stays deliberately small: central directory
// parsing, stored + raw-deflate entries, CRC verification, and a safe extractor
// that never lets an entry escape the destination directory.
class ZipReader
{
public:
    struct Entry
    {
        QString name;
        quint32 crc = 0;
        quint64 compressedSize = 0;
        quint64 size = 0;
        quint16 method = 0;
        quint32 localOffset = 0;
    };

    explicit ZipReader(const QString &path);

    bool open(QString *error = nullptr);
    bool isOpen() const { return m_open; }

    bool contains(const QString &name) const;
    QStringList names() const;
    const Entry *entry(const QString &name) const;

    // Reads one entry, verifying its size and CRC-32. `maxBytes` guards against
    // decompression bombs (the manifests we read are tiny).
    bool read(const QString &name, QByteArray *data, QString *error = nullptr,
              qint64 maxBytes = 64LL * 1024 * 1024);
    bool readJson(const QString &name, QJsonObject *object, QString *error = nullptr);

    // Extracts every entry whose name starts with `prefix` into `destRoot`,
    // stripping the prefix. Entry names are validated with isSafeRelativePath;
    // a hostile archive aborts the extraction instead of writing outside.
    bool extractPrefix(const QString &prefix, const QString &destRoot, QStringList *extracted,
                       QString *error, bool stripPrefix = true);

    static bool looksLikeZip(const QString &path);

private:
    bool readCompressed(const Entry &entry, QByteArray *data, QString *error, qint64 maxBytes);
    static bool writeEntry(const QString &destRoot, const QString &relative, const QByteArray &data,
                           QString *error);

    QString m_path;
    QVector<Entry> m_entries;
    bool m_open = false;
};
