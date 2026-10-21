#include "rag/RagController.h"

namespace {

QString shorten(const QString &text, int limit = 120)
{
    QString compact = text.simplified();
    if (compact.size() > limit)
        compact = compact.left(limit) + QStringLiteral("…");
    return compact;
}

} // namespace

RagController::RagController(QObject *parent)
    : QObject(parent)
{
}

RagController::~RagController() = default;

bool RagController::ensureOpen()
{
    if (m_attempted)
        return m_available;
    m_attempted = true;

    QString error;
    if (!m_index.open(rag::Index::defaultPath(), &error)) {
        m_lastNote = QStringLiteral("索引不可用：%1").arg(error);
        return false;
    }
    m_count = m_index.count();
    if (m_count == 0) {
        m_lastNote = QStringLiteral("还没有本地索引，运行 bmcd --rag-build 可建立");
        return false;
    }

    QString note;
    m_embedder = rag::makeEmbedder(rag::defaultModelDir(), &note);
    if (!m_embedder) {
        m_lastNote = QStringLiteral("嵌入器不可用");
        return false;
    }
    m_embedderName = m_embedder->name();
    m_retriever = std::make_unique<rag::Retriever>(&m_index, m_embedder.get());
    m_available = true;
    m_lastNote = note;
    emit changed();
    return true;
}

QStringList RagController::suggest(const QString &query, int top)
{
    QStringList out;
    const QString trimmed = query.trimmed();
    if (trimmed.size() < 2 || !ensureOpen())
        return out;

    const QVector<rag::SearchHit> hits = m_retriever->search(trimmed, top, {}, nullptr);
    for (const rag::SearchHit &hit : hits) {
        const QString kind = hit.chunk.kind.isEmpty() ? QStringLiteral("mod") : hit.chunk.kind;
        out.append(QStringLiteral("%1 — %2 [%3]").arg(hit.chunk.title, shorten(hit.chunk.text), kind));
    }
    m_lastNote = m_retriever->lastNote();
    emit changed();
    return out;
}

QVariantMap RagController::stats() const
{
    QVariantMap map;
    map.insert(QStringLiteral("available"), m_available);
    map.insert(QStringLiteral("chunks"), m_count);
    map.insert(QStringLiteral("embedder"), m_embedderName);
    map.insert(QStringLiteral("note"), m_lastNote);
    map.insert(QStringLiteral("indexPath"), rag::Index::defaultPath());
    return map;
}
