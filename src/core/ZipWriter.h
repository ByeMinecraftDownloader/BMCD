#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

// Minimal streaming ZIP writer (deflate or stored, no ZIP64). Written by hand
// instead of pulling a dependency: mrpack output needs deterministic entry
// order and the archive is small enough that the 4 GiB limit never triggers.
class ZipWriter
{
public:
    explicit ZipWriter(const QString &path);
    ~ZipWriter();

    bool addData(const QString &entryName, const QByteArray &data, bool compress = true);
    bool addFileFromPath(const QString &entryName, const QString &sourcePath, bool compress = true);
    bool close();

    QString errorString() const { return m_error; }
    qint64 bytesWritten() const { return m_offset; }

private:
    struct Entry
    {
        QByteArray name;
        quint32 crc = 0;
        quint32 compressedSize = 0;
        quint32 uncompressedSize = 0;
        quint32 offset = 0;
        quint16 method = 0;
        quint16 dosTime = 0;
        quint16 dosDate = 0;
    };

    bool writeEntry(const QByteArray &name, const QByteArray &data, bool compress);
    void setError(const QString &error);
    static QByteArray deflateRaw(const QByteArray &data, QString *error);

    QString m_path;
    QString m_error;
    QVector<Entry> m_entries;
    qint64 m_offset = 0;
    bool m_failed = false;
    bool m_closed = false;
    class QFile *m_file = nullptr;
};
