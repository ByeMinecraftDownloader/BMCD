#pragma once

#include <QString>
#include <QVector>

#include <memory>

// Text embedding used by the knowledge index (0.7).
//
// Two implementations live here:
//   * E5Embedder        – multilingual-e5-small exported to int8 ONNX, mean
//                         pooled and L2-normalised. Requires onnxruntime at
//                         build time and the model files at run time.
//   * HashingEmbedder   – deterministic character n-gram hashing into the same
//                         384 dimensions. Always available, used for tests and
//                         as the graceful degradation path; FTS still works
//                         when this is the only embedder.
namespace rag {

class Embedder
{
public:
    virtual ~Embedder() = default;

    virtual QString name() const = 0;
    virtual int dim() const = 0;
    // True when the vectors come from a trained model (quality gate applies).
    virtual bool neural() const = 0;

    virtual bool embed(const QString &text, QVector<float> *out) = 0;
};

// Picks the best available embedder for `modelDir`. `note`, when non-null,
// receives a short human-readable explanation of the choice.
std::unique_ptr<Embedder> makeEmbedder(const QString &modelDir, QString *note);

// Directory searched by default: $MC_MODEL_DIR, else <cache>/models/e5-small.
QString defaultModelDir();

// Directory names for the two files an ONNX export must provide.
QString modelFile(const QString &dir);
QString tokenizerFile(const QString &dir);

class HashingEmbedder final : public Embedder
{
public:
    explicit HashingEmbedder(int dim = 384);
    QString name() const override { return QStringLiteral("hashing-char-ngram"); }
    int dim() const override { return m_dim; }
    bool neural() const override { return false; }
    bool embed(const QString &text, QVector<float> *out) override;

private:
    int m_dim;
};

} // namespace rag
