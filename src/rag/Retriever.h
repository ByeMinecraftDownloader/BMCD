#pragma once

#include "rag/Embedder.h"
#include "rag/Index.h"

#include <QString>
#include <QVector>

// Hybrid retrieval: FTS5/BM25 and int8 vector search are fused with reciprocal
// rank fusion (k=60) and the fused list is diversified with MMR (λ=0.7).
namespace rag {

class Retriever
{
public:
    Retriever(Index *index, Embedder *embedder) : m_index(index), m_embedder(embedder) {}

    // `top` is the number of passages returned after MMR (roadmap: top 20).
    QVector<SearchHit> search(const QString &query, int top = 10, const Filters &filters = {},
                              QString *error = nullptr);

    // Exposed for tests and the evaluator.
    static QVector<SearchHit> fuse(const QVector<Chunk> &fts, const QVector<Chunk> &vector,
                                   int k = 60);
    static QVector<SearchHit> mmr(const QVector<SearchHit> &hits, const QVector<float> &queryVector,
                                  int top, double lambda = 0.7);

    QString lastNote() const { return m_note; }

private:
    Index *m_index = nullptr;
    Embedder *m_embedder = nullptr;
    QString m_note;
};

} // namespace rag
