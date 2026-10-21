#include "rag/Embedder.h"

#include "platform/Paths.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QtMath>
#include <cstdio>

#ifdef BMCD_HAVE_ONNXRUNTIME
#include <onnxruntime_cxx_api.h>
#include "rag/Tokenizer.h"
#endif

namespace rag {
namespace {

quint32 fnv1a(const char *data, int size)
{
    quint32 hash = 2166136261u;
    for (int i = 0; i < size; ++i) {
        hash ^= quint8(data[i]);
        hash *= 16777619u;
    }
    return hash;
}

void normaliseInPlace(QVector<float> *vector)
{
    double sum = 0.0;
    for (const float value : *vector)
        sum += double(value) * value;
    const double norm = std::sqrt(sum);
    if (norm <= 1e-12)
        return;
    for (float &value : *vector)
        value = float(value / norm);
}

#ifdef BMCD_HAVE_ONNXRUNTIME
class E5Embedder final : public Embedder
{
public:
    bool init(const QString &dir, QString *error);

    QString name() const override { return QStringLiteral("multilingual-e5-small-int8"); }
    int dim() const override { return m_dim; }
    bool neural() const override { return true; }
    bool embed(const QString &text, QVector<float> *out) override;

private:
    std::unique_ptr<Ort::Env> m_env;
    std::unique_ptr<Ort::Session> m_session;
    std::unique_ptr<Ort::MemoryInfo> m_memory;
    UnigramTokenizer m_tokenizer;
    std::vector<std::string> m_inputNames;
    std::vector<std::string> m_outputNames;
    int m_dim = 384;
    bool m_hasTokenTypes = false;
    bool m_ok = false;
};

bool E5Embedder::init(const QString &dir, QString *error)
{
    QFileInfo model(modelFile(dir));
    QFileInfo tokenizer(tokenizerFile(dir));
    if (!model.exists()) {
        if (error)
            *error = QStringLiteral("缺少模型文件 %1").arg(model.absoluteFilePath());
        return false;
    }
    if (!tokenizer.exists()) {
        if (error)
            *error = QStringLiteral("缺少分词器 %1").arg(tokenizer.absoluteFilePath());
        return false;
    }
    if (!m_tokenizer.load(tokenizer.absoluteFilePath(), error))
        return false;

    try {
        m_env = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "bmcd");
        Ort::SessionOptions options;
        options.SetIntraOpNumThreads(2);
        options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        m_session = std::make_unique<Ort::Session>(*m_env, model.absoluteFilePath().toUtf8().constData(),
                                                  options);
        m_memory = std::make_unique<Ort::MemoryInfo>(
                Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault));

        Ort::AllocatorWithDefaultOptions allocator;
        for (size_t i = 0; i < m_session->GetInputCount(); ++i) {
            // The allocated string owns the bytes; copying it out before the
            // temporary dies is essential (otherwise the name dangles).
            auto name = m_session->GetInputNameAllocated(i, allocator);
            m_inputNames.emplace_back(name.get());
        }
        for (size_t i = 0; i < m_session->GetOutputCount(); ++i) {
            auto name = m_session->GetOutputNameAllocated(i, allocator);
            m_outputNames.emplace_back(name.get());
        }
        m_hasTokenTypes = std::find(m_inputNames.begin(), m_inputNames.end(),
                                    std::string("token_type_ids"))
                != m_inputNames.end();
        m_ok = m_inputNames.size() >= 2 && !m_outputNames.empty();
        if (!m_ok && error)
            *error = QStringLiteral("模型输入输出不符合预期");
        return m_ok;
    } catch (const std::exception &e) {
        if (error)
            *error = QString::fromUtf8(e.what());
        return false;
    }
}

bool E5Embedder::embed(const QString &text, QVector<float> *out)
{
    if (!m_ok || !out)
        return false;

    const QVector<qint32> ids = m_tokenizer.encode(text, 256);
    if (ids.isEmpty())
        return false;
    const std::array<int64_t, 2> shape{1, int64_t(ids.size())};
    std::vector<int64_t> inputIds(ids.begin(), ids.end());
    std::vector<int64_t> mask(ids.size(), 1);
    std::vector<int64_t> types(ids.size(), 0);

    try {
        // Order matters: bind each tensor to the input name the model declares.
        std::vector<Ort::Value> inputs;
        std::vector<const char *> inputNames;
        for (const std::string &name : m_inputNames) {
            if (name == "input_ids")
                inputs.push_back(Ort::Value::CreateTensor<int64_t>(*m_memory, inputIds.data(),
                                                                   inputIds.size(), shape.data(),
                                                                   shape.size()));
            else if (name == "attention_mask")
                inputs.push_back(Ort::Value::CreateTensor<int64_t>(*m_memory, mask.data(), mask.size(),
                                                                   shape.data(), shape.size()));
            else if (name == "token_type_ids")
                inputs.push_back(Ort::Value::CreateTensor<int64_t>(*m_memory, types.data(), types.size(),
                                                                   shape.data(), shape.size()));
            else
                return false;
            inputNames.push_back(name.c_str());
        }
        std::vector<const char *> outputNames;
        for (const std::string &name : m_outputNames)
            outputNames.push_back(name.c_str());

        auto outputs = m_session->Run(Ort::RunOptions{nullptr}, inputNames.data(), inputs.data(),
                                      inputs.size(), outputNames.data(), 1);
        const Ort::TensorTypeAndShapeInfo info = outputs.front().GetTensorTypeAndShapeInfo();
        const std::vector<int64_t> outShape = info.GetShape();
        const float *data = outputs.front().GetTensorData<float>();

        if (outShape.size() == 3) {         // [1, seq, dim] -> mean pool
            const int seq = int(outShape[1]);
            const int dim = int(outShape[2]);
            m_dim = dim;
            out->fill(0.0f, dim);
            // Padding would dilute the sentence vector, so weight by the
            // attention mask when the model exposes one.
            const bool masked = int(mask.size()) == seq;
            float weightSum = 0.0f;
            for (int t = 0; t < seq; ++t) {
                const float w = masked ? float(mask[size_t(t)]) : 1.0f;
                if (w == 0.0f)
                    continue;
                for (int d = 0; d < dim; ++d)
                    (*out)[d] += w * data[t * dim + d];
                weightSum += w;
            }
            const float divisor = weightSum > 0.0f ? weightSum : float(seq);
            for (float &value : *out)
                value /= divisor;
        } else if (outShape.size() == 2) {  // already pooled
            const int dim = int(outShape[1]);
            m_dim = dim;
            out->resize(dim);
            std::copy(data, data + dim, out->begin());
        } else {
            return false;
        }
        normaliseInPlace(out);
        return true;
    } catch (const std::exception &e) {
        fprintf(stderr, "embed failed: %s\n", e.what());
        return false;
    }
}
#endif // BMCD_HAVE_ONNXRUNTIME

} // namespace

QString defaultModelDir()
{
    const QString override = qEnvironmentVariable("MC_MODEL_DIR");
    if (!override.isEmpty())
        return override;
    return platform::cacheDir() + QStringLiteral("/models/e5-small");
}

QString modelFile(const QString &dir)
{
    return QDir(dir).absoluteFilePath(QStringLiteral("model_quantized.onnx"));
}

QString tokenizerFile(const QString &dir)
{
    return QDir(dir).absoluteFilePath(QStringLiteral("tokenizer.json"));
}

HashingEmbedder::HashingEmbedder(int dim) : m_dim(qMax(16, dim)) {}

bool HashingEmbedder::embed(const QString &text, QVector<float> *out)
{
    if (!out)
        return false;
    out->fill(0.0f, m_dim);

    const QString folded = text.toCaseFolded();
    const QByteArray utf8 = folded.toUtf8();
    if (utf8.isEmpty())
        return false;

    // Character bigrams and trigrams: language agnostic, cheap and stable
    // enough to rank short Chinese/English mod descriptions.
    const int length = utf8.size();
    for (int i = 0; i < length; ++i) {
        const int maxN = qMin(3, length - i);
        for (int n = 2; n <= maxN; ++n) {
            const quint32 hash = fnv1a(utf8.constData() + i, n);
            const int bucket = int(hash % quint32(m_dim));
            const float sign = (hash & 0x80000000u) ? -1.0f : 1.0f;
            (*out)[bucket] += sign;
        }
    }
    normaliseInPlace(out);
    return true;
}

std::unique_ptr<Embedder> makeEmbedder(const QString &modelDir, QString *note)
{
#ifdef BMCD_HAVE_ONNXRUNTIME
    auto e5 = std::make_unique<E5Embedder>();
    QString error;
    if (e5->init(modelDir, &error)) {
        if (note)
            *note = QStringLiteral("使用 multilingual-e5-small（%1）").arg(modelDir);
        return e5;
    }
    if (note)
        *note = QStringLiteral("e5 模型不可用（%1），回退字符哈希嵌入").arg(error);
#else
    Q_UNUSED(modelDir)
    if (note)
        *note = QStringLiteral("此构建未启用 onnxruntime，使用字符哈希嵌入");
#endif
    return std::make_unique<HashingEmbedder>();
}

} // namespace rag
