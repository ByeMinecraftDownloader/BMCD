#pragma once

#include <QHash>
#include <QString>
#include <QVector>

// SentencePiece-Unigram tokenizer for the multilingual-e5 models.
//
// This is a deliberately small implementation: it reads the `tokenizer.json`
// that ships next to an ONNX export (HuggingFace `tokenizers` format) and
// reproduces the pieces that matter for embedding — NFKC normalisation,
// metaspace pre-tokenisation and Viterbi segmentation over the piece scores.
// Post-processing is hard-coded to XLM-R's `<s> … </s>` template because that is
// the only family BMCD embeds with.
namespace rag {

class UnigramTokenizer
{
public:
    static constexpr int kUnknown = 3;

    bool load(const QString &tokenizerJsonPath, QString *error);

    bool isLoaded() const { return m_loaded; }

    // Encodes `text`, adds the sentence markers and truncates to `maxPieces`
    // (including the two markers).
    QVector<qint32> encode(const QString &text, int maxPieces = 256) const;

private:
    QVector<qint32> segment(const QString &normalised) const;

    QHash<QString, qint32> m_ids;
    QHash<QString, float> m_scores;
    int m_maxPieceLength = 1;   // in UTF-16 code units
    float m_unkScore = -20.0f;
    float m_minScore = 1e9f;
    qint32 m_bos = 0;
    qint32 m_eos = 2;
    bool m_loaded = false;
};

} // namespace rag
