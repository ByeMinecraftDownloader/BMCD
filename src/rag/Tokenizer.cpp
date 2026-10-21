#include "rag/Tokenizer.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

namespace rag {
namespace {

constexpr char kMetaspace[] = "\xE2\x96\x81"; // U+2581 LOWER ONE EIGHTH BLOCK

QString normalise(const QString &text)
{
    // The reference normalizer is a Precompiled charmap that is effectively
    // NFKC for the scripts we care about; Qt ships the same tables.
    QString out = text.normalized(QString::NormalizationForm_KC);
    out.replace(QLatin1Char('\t'), QLatin1Char(' '));
    out.replace(QLatin1Char('\n'), QLatin1Char(' '));
    return out;
}

QString metaspace(const QString &text)
{
    QString out;
    out.reserve(text.size() + 4);
    out += QString::fromUtf8(kMetaspace);
    bool lastSpace = false;
    for (const QChar ch : text) {
        if (ch.isSpace()) {
            if (!lastSpace)
                out += QString::fromUtf8(kMetaspace);
            lastSpace = true;
        } else {
            out += ch;
            lastSpace = false;
        }
    }
    return out;
}

} // namespace

bool UnigramTokenizer::load(const QString &tokenizerJsonPath, QString *error)
{
    m_loaded = false;
    m_ids.clear();
    m_scores.clear();

    QFile file(tokenizerJsonPath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("无法打开分词器 %1").arg(tokenizerJsonPath);
        return false;
    }
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error)
            *error = QStringLiteral("分词器 JSON 无法解析：%1").arg(parseError.errorString());
        return false;
    }

    const QJsonObject root = doc.object();
    const QJsonObject model = root.value(QStringLiteral("model")).toObject();
    if (model.value(QStringLiteral("type")).toString() != QLatin1String("Unigram")) {
        if (error)
            *error = QStringLiteral("只支持 Unigram 分词器（当前为 %1）")
                             .arg(model.value(QStringLiteral("type")).toString());
        return false;
    }

    const QJsonArray vocab = model.value(QStringLiteral("vocab")).toArray();
    if (vocab.isEmpty()) {
        if (error)
            *error = QStringLiteral("分词器缺少 vocab");
        return false;
    }
    m_ids.reserve(vocab.size());
    m_scores.reserve(vocab.size());
    for (int id = 0; id < vocab.size(); ++id) {
        const QJsonArray entry = vocab.at(id).toArray();
        if (entry.size() < 2)
            continue;
        const QString piece = entry.at(0).toString();
        const float score = float(entry.at(1).toDouble());
        m_ids.insert(piece, id);
        m_scores.insert(piece, score);
        m_minScore = qMin(m_minScore, score);
        m_maxPieceLength = qMax(m_maxPieceLength, piece.size());
    }

    // Special tokens move with the model; respect whatever ids they carry.
    const QJsonArray added = root.value(QStringLiteral("added_tokens")).toArray();
    for (const QJsonValue &value : added) {
        const QJsonObject token = value.toObject();
        const QString content = token.value(QStringLiteral("content")).toString();
        const qint32 id = qint32(token.value(QStringLiteral("id")).toInt());
        if (content.isEmpty() || id < 0)
            continue;
        m_ids.insert(content, id);
        m_scores.insert(content, 0.0f);
        m_maxPieceLength = qMax(m_maxPieceLength, content.size());
        if (content == QLatin1String("<s>"))
            m_bos = id;
        else if (content == QLatin1String("</s>"))
            m_eos = id;
    }

    m_unkScore = m_minScore - 10.0f;
    m_loaded = m_ids.contains(QLatin1String("<unk>"));
    if (!m_loaded && error)
        *error = QStringLiteral("分词器缺少 <unk> 词元");
    return m_loaded;
}

QVector<qint32> UnigramTokenizer::segment(const QString &normalised) const
{
    const int n = normalised.size();
    QVector<float> best(n + 1, -1e30f);
    QVector<int> from(n + 1, -1);
    QVector<qint32> pieceAt(n + 1, -1);
    const float unkScore = m_unkScore;
    best[0] = 0.0f;

    for (int i = 1; i <= n; ++i) {
        const int limit = qMin(m_maxPieceLength, i);
        for (int len = 1; len <= limit; ++len) {
            const int start = i - len;
            const QString piece = normalised.mid(start, len);
            const auto it = m_scores.constFind(piece);
            if (it == m_scores.constEnd())
                continue;
            const float score = best[start] + it.value();
            if (score > best[i]) {
                best[i] = score;
                from[i] = start;
                pieceAt[i] = m_ids.value(piece);
            }
        }
        // Every UTF-16 code unit can at least fall back to <unk>, so the path is
        // always complete; this mirrors the reference implementation.
        const float unk = best[i - 1] + unkScore;
        if (unk > best[i]) {
            best[i] = unk;
            from[i] = i - 1;
            pieceAt[i] = kUnknown;
        }
    }

    // Walk the best path back from the end. Each piece is prepended, so the
    // result already reads left-to-right (reversing it would scramble the order).
    QVector<qint32> pieces;
    for (int i = n; i > 0; i = from[i]) {
        if (from[i] < 0)
            break;
        pieces.prepend(pieceAt[i]);
    }
    return pieces;
}

QVector<qint32> UnigramTokenizer::encode(const QString &text, int maxPieces) const
{
    QVector<qint32> ids;
    if (!m_loaded)
        return ids;

    const QString prepared = metaspace(normalise(text));
    const QVector<qint32> pieces = segment(prepared);
    ids.reserve(qMin(pieces.size() + 2, maxPieces));
    ids.append(m_bos);
    for (const qint32 id : pieces) {
        if (ids.size() >= maxPieces - 1)
            break;
        ids.append(id < 0 ? kUnknown : id);
    }
    ids.append(m_eos);
    return ids;
}

} // namespace rag
