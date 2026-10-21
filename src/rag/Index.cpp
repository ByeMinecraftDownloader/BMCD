#include "rag/Index.h"

#include "platform/Paths.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <cstring>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QtMath>

namespace rag {
namespace {

QString connectionName()
{
    return QStringLiteral("bmcd-rag");
}

bool applyFilters(const Filters &filters, QStringList *clauses)
{
    if (!filters.loader.isEmpty()) {
        clauses->append(QStringLiteral("loader LIKE '%%1%'").arg(filters.loader));
    }
    if (!filters.gameVersion.isEmpty()) {
        clauses->append(QStringLiteral("game_version LIKE '%%1%'").arg(filters.gameVersion));
    }
    if (!filters.kind.isEmpty())
        clauses->append(QStringLiteral("kind = '%1'").arg(filters.kind));
    return true;
}

} // namespace

QString Index::defaultPath()
{
    return platform::cacheDir() + QStringLiteral("/rag/index.sqlite");
}

bool Index::open(const QString &path, QString *error)
{
    close();
    const QString dir = QFileInfo(path).absolutePath();
    QDir().mkpath(dir);

    QSqlDatabase db = QSqlDatabase::contains(connectionName())
            ? QSqlDatabase::database(connectionName(), false)
            : QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName());
    db.setDatabaseName(path);
    if (!db.open()) {
        if (error)
            *error = QStringLiteral("无法打开索引 %1：%2").arg(path, db.lastError().text());
        return false;
    }

    QSqlQuery pragma(db);
    pragma.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    pragma.exec(QStringLiteral("PRAGMA synchronous=NORMAL"));

    QSqlQuery schema(db);
    const bool ok = schema.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS meta(key TEXT PRIMARY KEY, value TEXT)"))
            && schema.exec(QStringLiteral(
                    "CREATE TABLE IF NOT EXISTS chunks("
                    "id INTEGER PRIMARY KEY, project TEXT, kind TEXT, title TEXT, "
                    "loader TEXT, game_version TEXT, text TEXT, source_url TEXT, "
                    "dim INTEGER, scale REAL, vec BLOB)"))
            && schema.exec(QStringLiteral(
                    "CREATE VIRTUAL TABLE IF NOT EXISTS chunks_fts USING fts5("
                    "title, text, tokenize='unicode61 remove_diacritics 2')"));
    if (!ok) {
        if (error)
            *error = QStringLiteral("无法创建索引结构：%1").arg(schema.lastError().text());
        return false;
    }
    m_path = path;
    m_valid = true;
    return true;
}

void Index::close()
{
    if (QSqlDatabase::contains(connectionName())) {
        {
            QSqlDatabase db = QSqlDatabase::database(connectionName(), false);
            if (db.isOpen())
                db.close();
        }
        QSqlDatabase::removeDatabase(connectionName());
    }
    m_valid = false;
}

bool Index::reset(QString *error)
{
    if (!m_valid)
        return false;
    QSqlDatabase db = QSqlDatabase::database(connectionName(), false);
    QSqlQuery query(db);
    const bool ok = query.exec(QStringLiteral("DELETE FROM chunks"))
            && query.exec(QStringLiteral("DELETE FROM chunks_fts"))
            && query.exec(QStringLiteral("DELETE FROM meta"));
    if (!ok) {
        if (error)
            *error = query.lastError().text();
    }
    return ok;
}

bool Index::beginBulk(QString *error)
{
    QSqlDatabase db = QSqlDatabase::database(connectionName(), false);
    if (!db.transaction()) {
        if (error)
            *error = db.lastError().text();
        return false;
    }
    return true;
}

bool Index::addChunk(Chunk *chunk, QString *error)
{
    if (!m_valid || !chunk)
        return false;
    if (chunk->vector.isEmpty()) {
        if (error)
            *error = QStringLiteral("chunk 没有向量");
        return false;
    }

    QSqlDatabase db = QSqlDatabase::database(connectionName(), false);
    QSqlQuery insert(db);
    insert.prepare(QStringLiteral(
            "INSERT INTO chunks(project, kind, title, loader, game_version, text, source_url, "
            "dim, scale, vec) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    insert.addBindValue(chunk->project);
    insert.addBindValue(chunk->kind);
    insert.addBindValue(chunk->title);
    insert.addBindValue(chunk->loader);
    insert.addBindValue(chunk->gameVersion);
    insert.addBindValue(chunk->text);
    insert.addBindValue(chunk->sourceUrl);
    insert.addBindValue(chunk->dim());
    insert.addBindValue(double(chunk->scale));
    insert.addBindValue(QByteArray(reinterpret_cast<const char *>(chunk->vector.constData()),
                                   chunk->vector.size()));
    if (!insert.exec()) {
        if (error)
            *error = insert.lastError().text();
        return false;
    }
    chunk->id = insert.lastInsertId().toLongLong();

    QSqlQuery fts(db);
    fts.prepare(QStringLiteral("INSERT INTO chunks_fts(rowid, title, text) VALUES (?, ?, ?)"));
    fts.addBindValue(chunk->id);
    fts.addBindValue(chunk->title);
    fts.addBindValue(chunk->text);
    if (!fts.exec()) {
        if (error)
            *error = fts.lastError().text();
        return false;
    }
    return true;
}

bool Index::endBulk(bool commit, QString *error)
{
    QSqlDatabase db = QSqlDatabase::database(connectionName(), false);
    if (commit) {
        if (!db.commit()) {
            if (error)
                *error = db.lastError().text();
            return false;
        }
    } else {
        db.rollback();
    }
    return true;
}

qint64 Index::count() const
{
    QSqlDatabase db = QSqlDatabase::database(connectionName(), false);
    QSqlQuery query(db);
    if (query.exec(QStringLiteral("SELECT COUNT(*) FROM chunks")) && query.next())
        return query.value(0).toLongLong();
    return 0;
}

int Index::dim() const
{
    return meta(QStringLiteral("dim")).toInt();
}

bool Index::setMeta(const QString &key, const QString &value)
{
    QSqlDatabase db = QSqlDatabase::database(connectionName(), false);
    QSqlQuery query(db);
    query.prepare(QStringLiteral("INSERT INTO meta(key, value) VALUES(?, ?) "
                                 "ON CONFLICT(key) DO UPDATE SET value=excluded.value"));
    query.addBindValue(key);
    query.addBindValue(value);
    return query.exec();
}

QString Index::meta(const QString &key) const
{
    QSqlDatabase db = QSqlDatabase::database(connectionName(), false);
    QSqlQuery query(db);
    query.prepare(QStringLiteral("SELECT value FROM meta WHERE key = ?"));
    query.addBindValue(key);
    if (query.exec() && query.next())
        return query.value(0).toString();
    return QString();
}

QVector<Chunk> Index::chunks(QString *error) const
{
    QVector<Chunk> out;
    QSqlDatabase db = QSqlDatabase::database(connectionName(), false);
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral(
                "SELECT id, project, kind, title, loader, game_version, text, source_url, "
                "dim, scale, vec FROM chunks"))) {
        if (error)
            *error = query.lastError().text();
        return out;
    }
    while (query.next()) {
        Chunk chunk;
        chunk.id = query.value(0).toLongLong();
        chunk.project = query.value(1).toString();
        chunk.kind = query.value(2).toString();
        chunk.title = query.value(3).toString();
        chunk.loader = query.value(4).toString();
        chunk.gameVersion = query.value(5).toString();
        chunk.text = query.value(6).toString();
        chunk.sourceUrl = query.value(7).toString();
        chunk.scale = float(query.value(9).toDouble());
        const QByteArray blob = query.value(10).toByteArray();
        chunk.vector.resize(blob.size());
        std::memcpy(chunk.vector.data(), blob.constData(), blob.size());
        if (chunk.dim() == 0 && error)
            *error = QStringLiteral("chunk %1 缺向量").arg(chunk.id);
        out.append(chunk);
    }
    return out;
}

QVector<Chunk> Index::ftsSearch(const QString &query, int limit, const Filters &filters,
                                QString *error) const
{
    QVector<Chunk> out;
    QStringList terms;
    for (const QString &raw : query.split(QRegularExpression(QStringLiteral("\\s+")),
                                          Qt::SkipEmptyParts)) {
        QString term = raw;
        term.remove(QLatin1Char('"'));
        if (!term.isEmpty())
            terms.append(QStringLiteral("\"%1\"").arg(term));
    }
    if (terms.isEmpty())
        return out;

    QStringList clauses;
    applyFilters(filters, &clauses);
    const QString sql = QStringLiteral(
            "SELECT c.id, c.project, c.kind, c.title, c.loader, c.game_version, c.text, "
            "c.source_url, c.dim, c.scale, c.vec FROM chunks_fts f "
            "JOIN chunks c ON c.id = f.rowid "
            "WHERE chunks_fts MATCH ?%1 ORDER BY bm25(chunks_fts) LIMIT %2")
            .arg(clauses.isEmpty() ? QString() : QStringLiteral(" AND ") + clauses.join(QStringLiteral(" AND ")))
            .arg(limit);

    QSqlDatabase db = QSqlDatabase::database(connectionName(), false);
    QSqlQuery q(db);
    q.prepare(sql);
    q.addBindValue(terms.join(QStringLiteral(" OR ")));
    if (!q.exec()) {
        if (error)
            *error = q.lastError().text();
        return out;
    }
    while (q.next()) {
        Chunk chunk;
        chunk.id = q.value(0).toLongLong();
        chunk.project = q.value(1).toString();
        chunk.kind = q.value(2).toString();
        chunk.title = q.value(3).toString();
        chunk.loader = q.value(4).toString();
        chunk.gameVersion = q.value(5).toString();
        chunk.text = q.value(6).toString();
        chunk.sourceUrl = q.value(7).toString();
        chunk.scale = float(q.value(9).toDouble());
        const QByteArray blob = q.value(10).toByteArray();
        chunk.vector.resize(blob.size());
        std::memcpy(chunk.vector.data(), blob.constData(), blob.size());
        out.append(chunk);
    }
    return out;
}

QVector<qint8> Index::quantise(const QVector<float> &vector, float *scale)
{
    float maxAbs = 0.0f;
    for (const float value : vector)
        maxAbs = qMax(maxAbs, qAbs(value));
    const float s = maxAbs > 0.0f ? maxAbs / 127.0f : 1.0f;
    if (scale)
        *scale = s;
    QVector<qint8> out;
    out.resize(vector.size());
    for (int i = 0; i < vector.size(); ++i)
        out[i] = qint8(qBound(-127, qRound(vector.at(i) / s), 127));
    return out;
}

QVector<float> Index::dequantise(const QVector<qint8> &vector, float scale)
{
    QVector<float> out;
    out.resize(vector.size());
    for (int i = 0; i < vector.size(); ++i)
        out[i] = float(vector.at(i)) * scale;
    return out;
}

double Index::dot(const QVector<float> &a, const QVector<float> &b)
{
    const int n = qMin(a.size(), b.size());
    double sum = 0.0;
    for (int i = 0; i < n; ++i)
        sum += double(a.at(i)) * b.at(i);
    return sum;
}

double Index::cosineQuantised(const Chunk &a, const Chunk &b)
{
    const int n = qMin(a.vector.size(), b.vector.size());
    double sum = 0.0;
    double na = 0.0;
    double nb = 0.0;
    for (int i = 0; i < n; ++i) {
        sum += double(a.vector.at(i)) * b.vector.at(i);
        na += double(a.vector.at(i)) * a.vector.at(i);
        nb += double(b.vector.at(i)) * b.vector.at(i);
    }
    const double denom = std::sqrt(na * nb);
    return denom <= 1e-12 ? 0.0 : sum / denom;
}

double Index::cosineQuantised(const Chunk &chunk, const QVector<float> &query)
{
    const int n = qMin(chunk.vector.size(), query.size());
    double sum = 0.0;
    double cnorm = 0.0;
    double qnorm = 0.0;
    for (int i = 0; i < n; ++i) {
        sum += double(chunk.vector.at(i)) * query.at(i);
        cnorm += double(chunk.vector.at(i)) * chunk.vector.at(i);
        qnorm += double(query.at(i)) * query.at(i);
    }
    const double denom = std::sqrt(cnorm * qnorm);
    return denom <= 1e-12 ? 0.0 : sum / denom;
}

} // namespace rag
