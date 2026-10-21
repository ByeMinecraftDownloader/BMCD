#include "rag/Retriever.h"

#include <QHash>
#include <QtMath>

namespace rag {

QVector<SearchHit> Retriever::fuse(const QVector<Chunk> &fts, const QVector<Chunk> &vector, int k)
{
    QHash<qint64, SearchHit> merged;
    auto accumulate = [&merged, k](const QVector<Chunk> &list, int which) {
        for (int rank = 0; rank < list.size(); ++rank) {
            const Chunk &chunk = list.at(rank);
            auto it = merged.find(chunk.id);
            if (it == merged.end()) {
                SearchHit hit;
                hit.chunk = chunk;
                it = merged.insert(chunk.id, hit);
            }
            it->score += 1.0 / double(k + rank + 1);
            if (which == 0)
                it->ftsRank = rank + 1;
            else
                it->vectorRank = rank + 1;
        }
    };
    accumulate(fts, 0);
    accumulate(vector, 1);

    QVector<SearchHit> out;
    out.reserve(merged.size());
    for (auto it = merged.constBegin(); it != merged.constEnd(); ++it)
        out.append(it.value());
    std::sort(out.begin(), out.end(), [](const SearchHit &a, const SearchHit &b) {
        if (a.score != b.score)
            return a.score > b.score;
        return a.chunk.id < b.chunk.id;
    });
    return out;
}

QVector<SearchHit> Retriever::mmr(const QVector<SearchHit> &hits, const QVector<float> &queryVector,
                                  int top, double lambda)
{
    QVector<SearchHit> remaining = hits;
    QVector<SearchHit> selected;
    if (hits.isEmpty() || top <= 0)
        return selected;

    double maxScore = 0.0;
    for (const SearchHit &hit : hits)
        maxScore = qMax(maxScore, hit.score);
    if (maxScore <= 0.0)
        maxScore = 1.0;

    while (selected.size() < top && !remaining.isEmpty()) {
        int bestIndex = -1;
        double bestValue = -1e30;
        for (int i = 0; i < remaining.size(); ++i) {
            const SearchHit &candidate = remaining.at(i);
            const double relevance = candidate.score / maxScore;
            double maxSim = 0.0;
            for (const SearchHit &chosen : selected) {
                const double sim = candidate.chunk.dim() == chosen.chunk.dim()
                        ? Index::cosineQuantised(candidate.chunk, chosen.chunk)
                        : 0.0;
                maxSim = qMax(maxSim, sim);
            }
            const double value = lambda * relevance - (1.0 - lambda) * maxSim;
            if (value > bestValue) {
                bestValue = value;
                bestIndex = i;
            }
        }
        selected.append(remaining.takeAt(bestIndex));
    }
    return selected;
}

QVector<SearchHit> Retriever::search(const QString &query, int top, const Filters &filters,
                                     QString *error)
{
    QVector<SearchHit> out;
    if (!m_index || !m_index->isValid() || !m_embedder) {
        if (error)
            *error = QStringLiteral("索引或嵌入器未就绪");
        return out;
    }

    QVector<float> queryVector;
    // e5 expects the symmetric query prefix; hashing embedder ignores it but it
    // stays harmless.
    if (!m_embedder->embed(QStringLiteral("query: ") + query, &queryVector) || queryVector.isEmpty()) {
        if (error)
            *error = QStringLiteral("无法为查询生成向量");
        return out;
    }

    QString ftsError;
    const QVector<Chunk> fts = m_index->ftsSearch(query, 100, filters, &ftsError);
    if (!ftsError.isEmpty() && error)
        *error = ftsError;

    QVector<Chunk> all = m_index->chunks(error);
    QVector<Chunk> vector;
    vector.reserve(128);
    for (const Chunk &chunk : all) {
        if (!filters.loader.isEmpty() && !chunk.loader.contains(filters.loader, Qt::CaseInsensitive))
            continue;
        if (!filters.gameVersion.isEmpty()
            && !chunk.gameVersion.contains(filters.gameVersion, Qt::CaseInsensitive))
            continue;
        if (!filters.kind.isEmpty() && chunk.kind != filters.kind)
            continue;
        if (chunk.dim() != queryVector.size())
            continue;
        vector.append(chunk);
    }
    std::sort(vector.begin(), vector.end(), [&queryVector](const Chunk &a, const Chunk &b) {
        return Index::cosineQuantised(a, queryVector) > Index::cosineQuantised(b, queryVector);
    });
    if (vector.size() > 100)
        vector.resize(100);

    out = mmr(fuse(fts, vector), queryVector, top);
    m_note = QStringLiteral("%1 个 FTS 候选 / %2 个向量候选，融合后 %3 条，MMR 取 %4 条")
                     .arg(fts.size())
                     .arg(vector.size())
                     .arg(fuse(fts, vector).size())
                     .arg(out.size());
    return out;
}

} // namespace rag
