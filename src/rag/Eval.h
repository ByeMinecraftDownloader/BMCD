#pragma once

#include "rag/Index.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace rag {

class Embedder;
class Retriever;

struct JudgedQuery
{
    QString query;
    QStringList relevant;   // project slugs, as stored in chunk.project
};

struct EvalReport
{
    int queries = 0;
    int judged = 0;         // queries that produced at least one hit
    double fusedNdcg = 0.0;
    double bm25Ndcg = 0.0;
    int top = 10;
};

// Reads {"queries": [{"query": "...", "relevant": ["slug", ...]}, ...]}.
QVector<JudgedQuery> loadQrels(const QString &path, QString *error = nullptr);

// Discounted cumulative gain at `top` with binary relevance.
double ndcg(const QStringList &ranked, const QStringList &relevant, int top);

class Evaluator
{
public:
    Evaluator(Index *index, Embedder *embedder);

    EvalReport run(const QVector<JudgedQuery> &qrels, int top = 10, QString *error = nullptr) const;

private:
    Index *m_index = nullptr;
    Embedder *m_embedder = nullptr;
};

} // namespace rag
