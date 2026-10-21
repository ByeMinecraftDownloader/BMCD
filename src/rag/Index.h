#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

namespace rag {

// One indexed passage. `vector` is kept quantised (int8 + per-vector scale) so
// 50k chunks fit in ~19 MB of RAM and on disk.
struct Chunk
{
    qint64 id = -1;
    QString project;
    QString kind;          // mod / modpack / doc / resourcepack
    QString title;
    QString loader;        // comma separated: fabric,neoforge,...
    QString gameVersion;   // comma separated
    QString text;
    QString sourceUrl;
    QVector<qint8> vector;
    float scale = 0.0f;

    int dim() const { return int(vector.size()); }
};

struct Filters
{
    QString loader;
    QString gameVersion;
    QString kind;

    bool isEmpty() const { return loader.isEmpty() && gameVersion.isEmpty() && kind.isEmpty(); }
};

struct SearchHit
{
    Chunk chunk;
    double score = 0.0;
    int ftsRank = -1;
    int vectorRank = -1;
};

class Index
{
public:
    static QString defaultPath();

    bool open(const QString &path, QString *error);
    void close();
    bool isValid() const { return m_valid; }
    QString path() const { return m_path; }

    // Drops every chunk and recreates the schema.
    bool reset(QString *error);

    bool beginBulk(QString *error);
    bool addChunk(Chunk *chunk, QString *error);
    bool endBulk(bool commit, QString *error);

    qint64 count() const;
    int dim() const;

    bool setMeta(const QString &key, const QString &value);
    QString meta(const QString &key) const;

    // Loads every chunk (vectors dequantised) for brute-force scoring.
    QVector<Chunk> chunks(QString *error) const;

    // FTS5/BM25 candidates. Empty query returns nothing.
    QVector<Chunk> ftsSearch(const QString &query, int limit, const Filters &filters,
                             QString *error) const;

    static QVector<qint8> quantise(const QVector<float> &vector, float *scale);
    static QVector<float> dequantise(const QVector<qint8> &vector, float scale);
    static double dot(const QVector<float> &a, const QVector<float> &b);
    static double cosineQuantised(const Chunk &a, const Chunk &b);
    static double cosineQuantised(const Chunk &chunk, const QVector<float> &query);

private:
    QString m_path;
    bool m_valid = false;
};

} // namespace rag
