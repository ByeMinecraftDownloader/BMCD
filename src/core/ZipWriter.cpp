#include "core/ZipWriter.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>

#include <zlib.h>

namespace {

void writeU16(QByteArray &buffer, quint16 value)
{
    buffer.append(char(value & 0xFF));
    buffer.append(char((value >> 8) & 0xFF));
}

void writeU32(QByteArray &buffer, quint32 value)
{
    writeU16(buffer, quint16(value & 0xFFFF));
    writeU16(buffer, quint16((value >> 16) & 0xFFFF));
}

void dosTimestamp(QDateTime when, quint16 *dosTime, quint16 *dosDate)
{
    const QDate date = when.date();
    const QTime time = when.time();
    *dosTime = quint16((time.hour() << 11) | (time.minute() << 5) | (time.second() / 2));
    *dosDate = quint16(((std::max(1980, date.year()) - 1980) << 9) | (date.month() << 5) | date.day());
}

constexpr quint32 kMaxEntry = 0xFFFFFFFFu;

} // namespace

ZipWriter::ZipWriter(const QString &path)
    : m_path(path)
{
    auto *file = new QFile(path);
    if (!file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        setError(QStringLiteral("无法创建 %1").arg(path));
        delete file;
        return;
    }
    m_file = file;
}

ZipWriter::~ZipWriter()
{
    if (m_file && !m_closed)
        close();
    delete m_file;
}

void ZipWriter::setError(const QString &error)
{
    m_error = error;
    m_failed = true;
}

QByteArray ZipWriter::deflateRaw(const QByteArray &data, QString *error)
{
    z_stream stream{};
    if (deflateInit2(&stream, Z_BEST_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
        *error = QStringLiteral("deflateInit2 失败");
        return {};
    }
    QByteArray output;
    output.resize(int(deflateBound(&stream, uLong(data.size()))));
    stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(data.constData()));
    stream.avail_in = uInt(data.size());
    stream.next_out = reinterpret_cast<Bytef *>(output.data());
    stream.avail_out = uInt(output.size());

    const int result = deflate(&stream, Z_FINISH);
    if (result != Z_STREAM_END) {
        deflateEnd(&stream);
        *error = QStringLiteral("deflate 失败");
        return {};
    }
    output.resize(int(stream.total_out));
    deflateEnd(&stream);
    return output;
}

bool ZipWriter::addData(const QString &entryName, const QByteArray &data, bool compress)
{
    return writeEntry(entryName.toUtf8(), data, compress);
}

bool ZipWriter::addFileFromPath(const QString &entryName, const QString &sourcePath, bool compress)
{
    QFile file(sourcePath);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(QStringLiteral("无法读取 %1").arg(sourcePath));
        return false;
    }
    const QByteArray data = file.readAll();
    if (data.isEmpty() && file.size() > 0) {
        setError(QStringLiteral("读取 %1 失败").arg(sourcePath));
        return false;
    }
    return addData(entryName, data, compress);
}

bool ZipWriter::writeEntry(const QByteArray &name, const QByteArray &data, bool compress)
{
    if (m_failed)
        return false;
    if (!m_file) {
        setError(QStringLiteral("文件未打开"));
        return false;
    }
    if (m_entries.size() >= 0xFFFF) {
        setError(QStringLiteral("条目过多"));
        return false;
    }
    if (qint64(data.size()) > qint64(kMaxEntry)) {
        setError(QStringLiteral("条目超过 4 GiB"));
        return false;
    }

    Entry entry;
    entry.name = name;
    entry.uncompressedSize = quint32(data.size());
    entry.crc = quint32(crc32(0, reinterpret_cast<const Bytef *>(data.constData()), uInt(data.size())));
    entry.offset = quint32(m_offset);

    QByteArray payload = data;
    entry.method = 0; // stored
    if (compress) {
        QString error;
        const QByteArray compressed = deflateRaw(data, &error);
        if (!compressed.isEmpty() && compressed.size() < data.size()) {
            payload = compressed;
            entry.method = 8;
        }
    }
    entry.compressedSize = quint32(payload.size());
    if (qint64(payload.size()) > qint64(kMaxEntry)) {
        setError(QStringLiteral("压缩条目超过 4 GiB"));
        return false;
    }

    dosTimestamp(QDateTime::currentDateTime(), &entry.dosTime, &entry.dosDate);

    QByteArray header;
    writeU32(header, 0x04034b50);   // local file header
    writeU16(header, 20);           // version needed
    writeU16(header, 0x0800);       // UTF-8 names
    writeU16(header, entry.method);
    writeU16(header, entry.dosTime);
    writeU16(header, entry.dosDate);
    writeU32(header, entry.crc);
    writeU32(header, entry.compressedSize);
    writeU32(header, entry.uncompressedSize);
    writeU16(header, quint16(name.size()));
    writeU16(header, 0);            // extra field length
    header.append(name);

    if (m_file->write(header) != header.size() || m_file->write(payload) != payload.size()) {
        setError(QStringLiteral("写入失败"));
        return false;
    }
    m_offset += header.size() + payload.size();
    m_entries.append(entry);
    return true;
}

bool ZipWriter::close()
{
    if (m_closed)
        return !m_failed;
    m_closed = true;
    if (m_failed || !m_file)
        return false;

    const qint64 centralStart = m_offset;
    for (const Entry &entry : std::as_const(m_entries)) {
        QByteArray record;
        writeU32(record, 0x02014b50);   // central directory header
        writeU16(record, 20);           // version made by
        writeU16(record, 20);           // version needed
        writeU16(record, 0x0800);       // UTF-8
        writeU16(record, entry.method);
        writeU16(record, entry.dosTime);
        writeU16(record, entry.dosDate);
        writeU32(record, entry.crc);
        writeU32(record, entry.compressedSize);
        writeU32(record, entry.uncompressedSize);
        writeU16(record, quint16(entry.name.size()));
        writeU16(record, 0);            // extra length
        writeU16(record, 0);            // comment length
        writeU16(record, 0);            // disk number
        writeU16(record, 0);            // internal attributes
        writeU32(record, 0);            // external attributes
        writeU32(record, entry.offset);
        record.append(entry.name);
        if (m_file->write(record) != record.size()) {
            setError(QStringLiteral("写入目录失败"));
            return false;
        }
        m_offset += record.size();
    }

    QByteArray end;
    writeU32(end, 0x06054b50);          // end of central directory
    writeU16(end, 0);                   // disk
    writeU16(end, 0);                   // central directory disk
    writeU16(end, quint16(m_entries.size()));
    writeU16(end, quint16(m_entries.size()));
    writeU32(end, quint32(m_offset - centralStart));
    writeU32(end, quint32(centralStart));
    writeU16(end, 0);                   // comment length
    if (m_file->write(end) != end.size()) {
        setError(QStringLiteral("写入结尾失败"));
        return false;
    }
    m_offset += end.size();
    m_file->close();
    return true;
}
