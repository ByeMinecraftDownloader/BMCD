#include "rag/Eval.h"

#include "rag/Embedder.h"
#include "rag/Retriever.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtMath>

namespace rag {

namespace {

bool isRelevant(const QString &project, const QStringList &relevant)
{
    for (const QString &slug : relevant) {
        if (project.compare(slug, Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

} // namespace

QVector<JudgedQuery> loadQrels(const QString &path, QString *error)
{
    QVector<JudgedQuery> out;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("无法读取 %1").arg(path);
        return out;
    }
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error)
            *error = QStringLiteral("qrels JSON 无效：%1").arg(parseError.errorString());
        return out;
    }
    const QJsonArray queries = document.object().value(QStringLiteral("queries")).toArray();
    for (const QJsonValue &value : queries) {
        const QJsonObject object = value.toObject();
        JudgedQuery judged;
        judged.query = object.value(QStringLiteral("query")).toString();
        for (const QJsonValue &entry : object.value(QStringLiteral("relevant")).toArray())
            judged.relevant.append(entry.toString());
        if (!judged.query.isEmpty() && !judged.relevant.isEmpty())
            out.append(judged);
    }
    if (out.isEmpty() && error)
        *error = QStringLiteral("%1 里没有可用的查询（需要 query + relevant）").arg(path);
    return out;
}

double ndcg(const QStringList &ranked, const QStringList &relevant, int top)
{
    // One project can own several chunks; retrieval quality is judged per
    // project, so only the first occurrence of a project earns a rank slot.
    QStringList unique;
    unique.reserve(ranked.size());
    for (const QString &project : ranked) {
        if (!unique.contains(project, Qt::CaseInsensitive))
            unique.append(project);
    }

    double dcg = 0.0;
    for (int i = 0; i < unique.size() && i < top; ++i) {
        if (isRelevant(unique.at(i), relevant))
            dcg += 1.0 / qLn(double(i) + 2.0) / qLn(2.0);
    }
    const int ideal = qMin(int(relevant.size()), top);
    double idcg = 0.0;
    for (int i = 0; i < ideal; ++i)
        idcg += 1.0 / qLn(double(i) + 2.0) / qLn(2.0);
    return idcg > 0.0 ? dcg / idcg : 0.0;
}

Evaluator::Evaluator(Index *index, Embedder *embedder)
    : m_index(index)
    , m_embedder(embedder)
{
}

EvalReport Evaluator::run(const QVector<JudgedQuery> &qrels, int top, QString *error) const
{
    EvalReport report;
    report.top = top;
    if (!m_index || !m_index->isValid() || !m_embedder) {
        if (error)
            *error = QStringLiteral("索引或嵌入器未就绪");
        return report;
    }
    if (qrels.isEmpty())
        return report;

    Retriever retriever(m_index, m_embedder);
    double fusedSum = 0.0;
    double bm25Sum = 0.0;
    for (const JudgedQuery &judged : qrels) {
        QString queryError;
        const QVector<SearchHit> hits = retriever.search(judged.query, top, {}, &queryError);
        const QVector<Chunk> fts = m_index->ftsSearch(judged.query, top, {}, &queryError);
        if (!hits.isEmpty())
            ++report.judged;

        QStringList fusedRanked;
        fusedRanked.reserve(hits.size());
        for (const SearchHit &hit : hits)
            fusedRanked.append(hit.chunk.project);

        QStringList bm25Ranked;
        bm25Ranked.reserve(fts.size());
        for (const Chunk &chunk : fts)
            bm25Ranked.append(chunk.project);

        fusedSum += ndcg(fusedRanked, judged.relevant, top);
        bm25Sum += ndcg(bm25Ranked, judged.relevant, top);
        ++report.queries;
    }
    if (report.queries > 0) {
        report.fusedNdcg = fusedSum / report.queries;
        report.bm25Ndcg = bm25Sum / report.queries;
    }
    return report;
}

} // namespace rag
