#include "core/ZipReader.h"

#include "core/Pack.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <zlib.h>

#include <cstring>

namespace {

constexpr quint32 kEndOfCentralDirectory = 0x06054b50;
constexpr quint32 kCentralFileHeader = 0x02014b50;
constexpr quint32 kLocalFileHeader = 0x04034b50;
constexpr quint64 kMaxArchiveBytes = 4ULL * 1024 * 1024 * 1024;

quint16 read16(const QByteArray &data, int offset)
{
    return quint16(quint8(data.at(offset))) | (quint16(quint8(data.at(offset + 1))) << 8);
}

quint32 read32(const QByteArray &data, int offset)
{
    return quint32(quint8(data.at(offset))) | (quint32(quint8(data.at(offset + 1))) << 8)
            | (quint32(quint8(data.at(offset + 2))) << 16)
            | (quint32(quint8(data.at(offset + 3))) << 24);
}

QByteArray inflateRaw(const QByteArray &compressed, qint64 expectedSize, QString *error)
{
    z_stream stream{};
    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
        *error = QStringLiteral("zlib 初始化失败");
        return {};
    }

    QByteArray output;
    output.resize(int(expectedSize));
    stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(compressed.constData()));
    stream.avail_in = uInt(compressed.size());
    stream.next_out = reinterpret_cast<Bytef *>(output.data());
    stream.avail_out = uInt(output.size());

    const int result = inflate(&stream, Z_FINISH);
    const uLong produced = stream.total_out;
    inflateEnd(&stream);
    if (result != Z_STREAM_END) {
        *error = QStringLiteral("zlib 解压失败（错误码 %1）").arg(result);
        return {};
    }
    if (qint64(produced) != expectedSize) {
        *error = QStringLiteral("解压大小不符：期望 %1，实际 %2").arg(expectedSize).arg(produced);
        return {};
    }
    return output;
}

QString normalizedName(QString name)
{
    name.replace(QLatin1Char('\\'), QLatin1Char('/'));
    while (name.startsWith(QLatin1String("./")))
        name.remove(0, 2);
    return name;
}

} // namespace

ZipReader::ZipReader(const QString &path)
    : m_path(path)
{
}

bool ZipReader::looksLikeZip(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray magic = file.read(4);
    return magic == QByteArrayLiteral("PK\x03\x04") || magic == QByteArrayLiteral("PK\x05\x06");
}

bool ZipReader::open(QString *error)
{
    m_entries.clear();
    m_open = false;

    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("无法读取 %1").arg(m_path);
        return false;
    }
    const qint64 fileSize = file.size();
    if (fileSize < 22 || quint64(fileSize) > kMaxArchiveBytes) {
        if (error)
            *error = QStringLiteral("不是有效的 ZIP：%1").arg(m_path);
        return false;
    }

    const int tailSize = int(qMin<qint64>(fileSize, 65557));
    if (!file.seek(fileSize - tailSize)) {
        if (error)
            *error = QStringLiteral("读取 ZIP 结尾失败");
        return false;
    }
    const QByteArray tail = file.read(tailSize);

    int eocd = -1;
    for (int i = tail.size() - 22; i >= 0; --i) {
        if (read32(tail, i) == kEndOfCentralDirectory) {
            eocd = i;
            break;
        }
    }
    if (eocd < 0) {
        if (error)
            *error = QStringLiteral("找不到 ZIP 中央目录");
        return false;
    }

    const quint16 disk = read16(tail, eocd + 4);
    const quint16 cdDisk = read16(tail, eocd + 6);
    const quint16 diskEntries = read16(tail, eocd + 8);
    const quint16 totalEntries = read16(tail, eocd + 10);
    const quint32 cdSize = read32(tail, eocd + 12);
    const quint32 cdOffset = read32(tail, eocd + 16);
    if (disk != 0 || cdDisk != 0 || diskEntries != totalEntries) {
        if (error)
            *error = QStringLiteral("不支持分卷 ZIP");
        return false;
    }
    if (totalEntries == 0xffff || cdSize == 0xffffffff || cdOffset == 0xffffffff) {
        if (error)
            *error = QStringLiteral("不支持 ZIP64 压缩包");
        return false;
    }
    if (qint64(cdOffset) + qint64(cdSize) > fileSize) {
        if (error)
            *error = QStringLiteral("ZIP 中央目录越界");
        return false;
    }

    if (!file.seek(cdOffset)) {
        if (error)
            *error = QStringLiteral("定位 ZIP 中央目录失败");
        return false;
    }
    const QByteArray directory = file.read(cdSize);
    if (directory.size() != int(cdSize)) {
        if (error)
            *error = QStringLiteral("读取 ZIP 中央目录失败");
        return false;
    }

    int offset = 0;
    for (int index = 0; index < totalEntries; ++index) {
        if (offset + 46 > directory.size() || read32(directory, offset) != kCentralFileHeader) {
            if (error)
                *error = QStringLiteral("ZIP 中央目录损坏（条目 %1）").arg(index);
            return false;
        }
        const quint16 nameLength = read16(directory, offset + 28);
        const quint16 extraLength = read16(directory, offset + 30);
        const quint16 commentLength = read16(directory, offset + 32);
        if (offset + 46 + nameLength + extraLength + commentLength > directory.size()) {
            if (error)
                *error = QStringLiteral("ZIP 中央目录条目越界");
            return false;
        }

        Entry entry;
        entry.name = normalizedName(QString::fromUtf8(directory.mid(offset + 46, nameLength)));
        entry.method = read16(directory, offset + 10);
        entry.crc = read32(directory, offset + 16);
        entry.compressedSize = read32(directory, offset + 20);
        entry.size = read32(directory, offset + 24);
        entry.localOffset = read32(directory, offset + 42);

        const quint16 flags = read16(directory, offset + 8);
        if (flags & 0x1) {
            if (error)
                *error = QStringLiteral("不支持加密 ZIP 条目：%1").arg(entry.name);
            return false;
        }
        if (entry.localOffset >= cdOffset) {
            if (error)
                *error = QStringLiteral("ZIP 条目偏移越界：%1").arg(entry.name);
            return false;
        }
        if (entry.method != 0 && entry.method != 8) {
            if (error)
                *error = QStringLiteral("不支持的压缩方法 %1：%2").arg(entry.method).arg(entry.name);
            return false;
        }

        m_entries.append(entry);
        offset += 46 + nameLength + extraLength + commentLength;
    }

    m_open = true;
    return true;
}

bool ZipReader::contains(const QString &name) const
{
    return entry(name) != nullptr;
}

QStringList ZipReader::names() const
{
    QStringList result;
    result.reserve(m_entries.size());
    for (const Entry &entry : m_entries)
        result.append(entry.name);
    return result;
}

const ZipReader::Entry *ZipReader::entry(const QString &name) const
{
    for (const Entry &entry : m_entries) {
        if (entry.name == name)
            return &entry;
    }
    return nullptr;
}

bool ZipReader::readCompressed(const Entry &entry, QByteArray *data, QString *error, qint64 maxBytes)
{
    if (qint64(entry.size) > maxBytes) {
        if (error)
            *error = QStringLiteral("条目过大：%1").arg(entry.name);
        return false;
    }

    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("无法读取 %1").arg(m_path);
        return false;
    }
    if (!file.seek(entry.localOffset)) {
        if (error)
            *error = QStringLiteral("定位条目失败：%1").arg(entry.name);
        return false;
    }
    const QByteArray local = file.read(30);
    if (local.size() != 30 || read32(local, 0) != kLocalFileHeader) {
        if (error)
            *error = QStringLiteral("ZIP 本地头损坏：%1").arg(entry.name);
        return false;
    }
    const quint16 nameLength = read16(local, 26);
    const quint16 extraLength = read16(local, 28);
    const qint64 dataOffset = qint64(entry.localOffset) + 30 + nameLength + extraLength;
    if (!file.seek(dataOffset)) {
        if (error)
            *error = QStringLiteral("定位条目数据失败：%1").arg(entry.name);
        return false;
    }
    const QByteArray compressed = file.read(qint64(entry.compressedSize));
    if (compressed.size() != qint64(entry.compressedSize)) {
        if (error)
            *error = QStringLiteral("条目数据不完整：%1").arg(entry.name);
        return false;
    }

    QByteArray plain;
    if (entry.method == 0) {
        plain = compressed;
    } else {
        plain = inflateRaw(compressed, qint64(entry.size), error);
        if (plain.isNull())
            return false;
    }

    const uLong actualCrc = crc32(0L, reinterpret_cast<const Bytef *>(plain.constData()), uInt(plain.size()));
    if (quint32(actualCrc) != entry.crc) {
        if (error)
            *error = QStringLiteral("CRC 校验失败：%1").arg(entry.name);
        return false;
    }
    *data = plain;
    return true;
}

bool ZipReader::read(const QString &name, QByteArray *data, QString *error, qint64 maxBytes)
{
    if (!m_open) {
        if (error)
            *error = QStringLiteral("ZIP 尚未打开");
        return false;
    }
    const Entry *found = entry(name);
    if (!found) {
        if (error)
            *error = QStringLiteral("压缩包里没有 %1").arg(name);
        return false;
    }
    return readCompressed(*found, data, error, maxBytes);
}

bool ZipReader::readJson(const QString &name, QJsonObject *object, QString *error)
{
    QByteArray data;
    if (!read(name, &data, error))
        return false;
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    if (document.isNull() || !document.isObject()) {
        if (error)
            *error = QStringLiteral("%1 不是有效的 JSON：%2").arg(name, parseError.errorString());
        return false;
    }
    *object = document.object();
    return true;
}

bool ZipReader::writeEntry(const QString &destRoot, const QString &relative, const QByteArray &data,
                           QString *error)
{
    if (!isSafeRelativePath(relative)) {
        if (error)
            *error = QStringLiteral("不安全的 ZIP 路径：%1").arg(relative);
        return false;
    }
    const QString absolute = QDir(destRoot).filePath(relative);
    const QFileInfo info(absolute);
    if (!QDir().mkpath(info.absolutePath())) {
        if (error)
            *error = QStringLiteral("无法创建目录：%1").arg(info.absolutePath());
        return false;
    }
    QSaveFile file(absolute);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QStringLiteral("无法写入：%1").arg(absolute);
        return false;
    }
    if (file.write(data) != data.size() || !file.commit()) {
        if (error)
            *error = QStringLiteral("写入失败：%1").arg(absolute);
        return false;
    }
    return true;
}

bool ZipReader::extractPrefix(const QString &prefix, const QString &destRoot, QStringList *extracted,
                              QString *error, bool stripPrefix)
{
    if (!m_open && !open(error))
        return false;
    if (!QDir().mkpath(destRoot)) {
        if (error)
            *error = QStringLiteral("无法创建目录：%1").arg(destRoot);
        return false;
    }

    qint64 total = 0;
    for (const Entry &entry : m_entries) {
        if (!entry.name.startsWith(prefix))
            continue;
        if (entry.name.endsWith(QLatin1Char('/')))
            continue;
        total += qint64(entry.size);
        if (total > 2LL * 1024 * 1024 * 1024) {
            if (error)
                *error = QStringLiteral("解压体积超过 2 GB，已中止");
            return false;
        }

        QString relative = stripPrefix ? entry.name.mid(prefix.size()) : entry.name;
        if (relative.isEmpty())
            continue;
        QByteArray data;
        if (!readCompressed(entry, &data, error, 512LL * 1024 * 1024))
            return false;
        if (!writeEntry(destRoot, relative, data, error))
            return false;
        if (extracted)
            extracted->append(relative);
    }
    return true;
}
