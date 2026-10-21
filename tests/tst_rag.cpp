#include "rag/Corpus.h"
#include "rag/Eval.h"
#include "rag/Embedder.h"
#include "rag/Index.h"
#include "rag/Retriever.h"
#include "rag/Tokenizer.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

class TstRag : public QObject
{
    Q_OBJECT

private slots:
    void tokenizerSegmentsUnigram();
    void hashingEmbedderIsStableAndNormalised();
    void indexRoundTrip();
    void ftsFindsTokens();
    void fusionAndMmr();
    void corpusChunking();
    void endToEndWithDocs();
    void ndcgMatchesHandComputation();
    void e5ModelIfAvailable();
};

namespace {

// Tiny Unigram tokenizer: metaspace, a few pieces and the XLM-R specials.
QString writeTokenizer(const QDir &dir)
{
    QJsonArray vocab;
    auto add = [&vocab](const char *piece, double score) {
        vocab.append(QJsonArray{QString::fromUtf8(piece), score});
    };
    add("<s>", 0.0);
    add("<pad>", 0.0);
    add("</s>", 0.0);
    add("<unk>", 0.0);
    add("\xE2\x96\x81", -2.0);
    add("hello", -3.0);
    add("\xE2\x96\x81world", -3.5);
    add("mod", -4.0);
    add("\xE5\x91\xBD", -4.0);          // 命
    add("\xE4\xBB\xA4", -4.5);          // 令

    QJsonObject model;
    model.insert(QStringLiteral("type"), QStringLiteral("Unigram"));
    model.insert(QStringLiteral("unk_id"), 3);
    model.insert(QStringLiteral("vocab"), vocab);

    QJsonArray added;
    for (int id = 0; id < 4; ++id) {
        QJsonObject token;
        token.insert(QStringLiteral("id"), id);
        token.insert(QStringLiteral("content"),
                     QStringList{QStringLiteral("<s>"), QStringLiteral("<pad>"), QStringLiteral("</s>"),
                                 QStringLiteral("<unk>")}
                             .at(id));
        added.append(token);
    }

    QJsonObject root;
    root.insert(QStringLiteral("model"), model);
    root.insert(QStringLiteral("added_tokens"), added);

    const QString path = dir.absoluteFilePath(QStringLiteral("tokenizer.json"));
    QFile file(path);
    if (file.open(QIODevice::WriteOnly))
        file.write(QJsonDocument(root).toJson());
    return path;
}

} // namespace

void TstRag::tokenizerSegmentsUnigram()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = writeTokenizer(QDir(temp.path()));

    rag::UnigramTokenizer tokenizer;
    QString error;
    QVERIFY2(tokenizer.load(path, &error), qPrintable(error));

    const QVector<qint32> ids = tokenizer.encode(QStringLiteral("hello world"), 16);
    QCOMPARE(ids.first(), 0);
    QCOMPARE(ids.last(), 2);
    QVERIFY(ids.size() > 2);
    // "hello" is a known piece: it must be present, not <unk>-thon.
    QVERIFY(ids.contains(5));
    QVERIFY(!ids.contains(3) || ids.count(3) < 2);

    const QVector<qint32> chinese = tokenizer.encode(QStringLiteral("命令"), 16);
    QVERIFY(chinese.contains(8) || chinese.contains(9));

    QVERIFY(!tokenizer.load(QStringLiteral("/nonexistent/tokenizer.json"), &error));
}

void TstRag::hashingEmbedderIsStableAndNormalised()
{
    rag::HashingEmbedder embedder(384);
    QVector<float> a;
    QVector<float> b;
    QVERIFY(embedder.embed(QStringLiteral("创造模式 自动整理 箱子"), &a));
    QVERIFY(embedder.embed(QStringLiteral("创造模式 自动整理 箱子"), &b));
    QCOMPARE(a.size(), 384);
    QCOMPARE(a, b);

    double norm = 0.0;
    for (const float value : a)
        norm += double(value) * value;
    QVERIFY(qAbs(norm - 1.0) < 1e-4);

    QVector<float> other;
    QVERIFY(embedder.embed(QStringLiteral("光影 shader performance"), &other));
    QVERIFY(rag::Index::dot(a, other) < 0.99);
    QCOMPARE(embedder.dim(), 384);
    QVERIFY(!embedder.neural());
}

void TstRag::indexRoundTrip()
{
    QTemporaryDir temp;
    rag::Index index;
    QString error;
    QVERIFY2(index.open(QDir(temp.path()).absoluteFilePath(QStringLiteral("index.sqlite")), &error),
             qPrintable(error));
    QVERIFY(index.reset(&error));
    QVERIFY(index.beginBulk(&error));

    rag::HashingEmbedder embedder;
    QVector<float> vector;
    QVERIFY(embedder.embed(QStringLiteral("sodium 优化 渲染"), &vector));
    rag::Chunk chunk;
    chunk.project = QStringLiteral("sodium");
    chunk.kind = QStringLiteral("mod");
    chunk.title = QStringLiteral("Sodium");
    chunk.loader = QStringLiteral("fabric");
    chunk.gameVersion = QStringLiteral("1.20.1");
    chunk.text = QStringLiteral("sodium 优化 渲染 管线");
    chunk.vector = rag::Index::quantise(vector, &chunk.scale);
    QVERIFY2(index.addChunk(&chunk, &error), qPrintable(error));
    QVERIFY(index.endBulk(true, &error));

    QCOMPARE(index.count(), qint64(1));
    const QVector<rag::Chunk> chunks = index.chunks(&error);
    QCOMPARE(chunks.size(), 1);
    QCOMPARE(chunks.first().loader, QStringLiteral("fabric"));
    const QVector<float> restored = rag::Index::dequantise(chunks.first().vector, chunks.first().scale);
    const double cosine = rag::Index::dot(restored, vector);
    QVERIFY(cosine > 0.99);
}

void TstRag::ftsFindsTokens()
{
    QTemporaryDir temp;
    rag::Index index;
    QString error;
    QVERIFY(index.open(QDir(temp.path()).absoluteFilePath(QStringLiteral("index.sqlite")), &error));
    QVERIFY(index.reset(&error));
    QVERIFY(index.beginBulk(&error));

    rag::HashingEmbedder embedder;
    auto add = [&](const QString &title, const QString &text, const QString &loader) {
        QVector<float> vector;
        embedder.embed(title + text, &vector);
        rag::Chunk chunk;
        chunk.project = title;
        chunk.kind = QStringLiteral("mod");
        chunk.title = title;
        chunk.loader = loader;
        chunk.text = text;
        chunk.vector = rag::Index::quantise(vector, &chunk.scale);
        return index.addChunk(&chunk, &error);
    };
    QVERIFY(add(QStringLiteral("Sodium"), QStringLiteral("渲染优化 performance render"), QStringLiteral("fabric")));
    QVERIFY(add(QStringLiteral("Create"), QStringLiteral("机械 齿轮 contraption"), QStringLiteral("forge")));
    QVERIFY(index.endBulk(true, &error));

    const QVector<rag::Chunk> hits = index.ftsSearch(QStringLiteral("performance"), 10, {}, &error);
    QCOMPARE(hits.size(), 1);
    QCOMPARE(hits.first().title, QStringLiteral("Sodium"));

    rag::Filters filters;
    filters.loader = QStringLiteral("forge");
    const QVector<rag::Chunk> forgeHits = index.ftsSearch(QStringLiteral("optimization"), 10, filters, &error);
    QCOMPARE(forgeHits.size(), 0);
}

void TstRag::fusionAndMmr()
{
    auto chunk = [](qint64 id) {
        rag::Chunk chunk;
        chunk.id = id;
        chunk.title = QStringLiteral("chunk-%1").arg(id);
        chunk.vector.fill(1, 4);
        chunk.scale = 1.0f;
        return chunk;
    };
    const QVector<rag::Chunk> fts{chunk(1), chunk(2), chunk(3)};
    const QVector<rag::Chunk> vector{chunk(2), chunk(3), chunk(4)};

    const QVector<rag::SearchHit> fused = rag::Retriever::fuse(fts, vector);
    QCOMPARE(fused.size(), 4);
    QCOMPARE(fused.first().chunk.id, qint64(2));
    QVERIFY(fused.first().score > fused.last().score);

    const QVector<rag::SearchHit> picked = rag::Retriever::mmr(fused, QVector<float>(4, 1.0f), 2, 0.7);
    QCOMPARE(picked.size(), 2);
    QCOMPARE(picked.first().chunk.id, fused.first().chunk.id);
}

void TstRag::corpusChunking()
{
    const QString body = QStringLiteral("intro text that is long enough to matter\n"
                                        "## 安装\nbrew install mod\n"
                                        "## 特性\n非常长的特性说明，应该被切分成多块，"
                                        "这里重复一些内容让它超过窗口大小，以便验证滑动窗口逻辑真的生效。");
    const QVector<QString> chunks = rag::CorpusBuilder::chunkText(QStringLiteral("Test"), body, 60, 10);
    QVERIFY(chunks.size() >= 2);
    for (const QString &chunk : chunks)
        QVERIFY(chunk.startsWith(QStringLiteral("# Test")));

    const QString cleaned = rag::CorpusBuilder::cleanMarkdown(
            QStringLiteral("# Title\n**bold** [label](https://x) ![img](y) `code` <b>html</b>"));
    QVERIFY(cleaned.contains(QStringLiteral("label")));
    QVERIFY(!cleaned.contains(QStringLiteral("https://x")));
    QVERIFY(!cleaned.contains(QStringLiteral("<b>")));
}

void TstRag::endToEndWithDocs()
{
    QTemporaryDir temp;
    QDir docs(QDir(temp.path()).absoluteFilePath(QStringLiteral("docs")));
    QVERIFY(docs.mkpath(QStringLiteral(".")));
    {
        QFile file(docs.absoluteFilePath(QStringLiteral("sodium.md")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("# Sodium\n\nSodium 是 Fabric 上的渲染优化模组，提升帧率，降低卡顿。\n");
    }
    {
        QFile file(docs.absoluteFilePath(QStringLiteral("create.md")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("# Create\n\nCreate 提供机械动力系统：齿轮、传送带、风车。\n");
    }

    rag::Index index;
    QString error;
    QVERIFY(index.open(QDir(temp.path()).absoluteFilePath(QStringLiteral("index.sqlite")), &error));
    QVERIFY(index.reset(&error));
    rag::HashingEmbedder embedder;
    rag::CorpusBuilder builder(&index, &embedder);
    rag::BuildReport report;
    QVERIFY2(builder.buildFromDocs(docs.absolutePath(), &report, &error), qPrintable(error));
    QCOMPARE(report.projects, 2);
    QVERIFY(report.chunks >= 2);
    QCOMPARE(index.count(), qint64(report.chunks));

    rag::Retriever retriever(&index, &embedder);
    const QVector<rag::SearchHit> hits = retriever.search(QStringLiteral("渲染 优化"), 3, {}, &error);
    QVERIFY(!hits.isEmpty());
    QCOMPARE(hits.first().chunk.project, QStringLiteral("sodium"));
}

void TstRag::ndcgMatchesHandComputation()
{
    const QStringList relevant{QStringLiteral("a"), QStringLiteral("b")};
    QCOMPARE(rag::ndcg({QStringLiteral("a"), QStringLiteral("b")}, relevant, 10), 1.0);

    // Relevant answer in second place: DCG = 1/log2(3), IDCG = 1.
    const double expected = qLn(2.0) / qLn(3.0);   // 1/log2(3)
    QCOMPARE(rag::ndcg({QStringLiteral("b"), QStringLiteral("a")}, relevant, 10), 1.0);
    QVERIFY(qAbs(rag::ndcg({QStringLiteral("x"), QStringLiteral("a")}, {QStringLiteral("a")}, 10)
                 - expected) < 1e-9);

    // Duplicate chunks of the same project only earn one rank slot.
    QCOMPARE(rag::ndcg({QStringLiteral("a"), QStringLiteral("a"), QStringLiteral("b")}, relevant, 10), 1.0);
    QCOMPARE(rag::ndcg({QStringLiteral("x")}, relevant, 10), 0.0);

    QString error;
    const QVector<rag::JudgedQuery> qrels =
            rag::loadQrels(QStringLiteral(BMCD_TEST_DATA_DIR "/qrels-docs.json"), &error);
    QVERIFY2(qrels.size() >= 5, qPrintable(error));
    QVERIFY(!qrels.first().relevant.isEmpty());
    QVERIFY(rag::loadQrels(QStringLiteral("/nonexistent.json"), &error).isEmpty());
}

void TstRag::e5ModelIfAvailable()
{
    const QString dir = qEnvironmentVariable("MC_MODEL_DIR");
    if (dir.isEmpty() || !QFile::exists(rag::modelFile(dir)) || !QFile::exists(rag::tokenizerFile(dir)))
        QSKIP("MC_MODEL_DIR 未指向已下载的 e5 模型");

    QString note;
    auto embedder = rag::makeEmbedder(dir, &note);
    QVERIFY(embedder);
    QVERIFY(embedder->neural());

    QVector<float> cat;
    QVector<float> catEn;
    QVector<float> computer;
    QVector<float> paraphrase;
    QVERIFY(embedder->embed(QStringLiteral("passage: 猫"), &cat));
    QVERIFY(embedder->embed(QStringLiteral("passage: cat"), &catEn));
    QVERIFY(embedder->embed(QStringLiteral("passage: 计算机"), &computer));
    QVERIFY(embedder->embed(QStringLiteral("passage: 渲染优化模组可以提升帧率"), &paraphrase));

    QVector<float> rendered;
    QVERIFY(embedder->embed(QStringLiteral("passage: 渲染优化模组，提升帧率"), &rendered));
    QCOMPARE(cat.size(), 384);
    // Cross-lingual alignment must beat an unrelated Chinese sentence; int8
    // quantisation makes the gap small, so compare rather than threshold.
    QVERIFY(rag::Index::dot(cat, catEn) > rag::Index::dot(cat, computer));
    QVERIFY(rag::Index::dot(rendered, paraphrase) > 0.95);
}

QTEST_GUILESS_MAIN(TstRag)
#include "tst_rag.moc"
