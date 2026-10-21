#pragma once

#include "rag/Embedder.h"
#include "rag/Index.h"

#include <QHash>
#include <QString>
#include <QVector>

#include <functional>

// Corpus ingestion for the knowledge index: Markdown documents on disk and
// Modrinth project pages. Everything is cleaned to plain text, chunked by
// headings, embedded and written to the SQLite index.
namespace rag {

struct BuildReport
{
    int projects = 0;
    int chunks = 0;
    int skipped = 0;
    QString embedder;
};

class CorpusBuilder
{
public:
    CorpusBuilder(Index *index, Embedder *embedder) : m_index(index), m_embedder(embedder) {}

    bool buildFromDocs(const QString &dir, BuildReport *report, QString *error);
    bool buildFromModrinth(const QString &query, int limit, BuildReport *report, QString *error,
                           const std::function<void(int, int)> &progress = {});

    // Plain text from Markdown: drops code fences, images, links become labels,
    // collapses whitespace. Exposed for tests.
    static QString cleanMarkdown(const QString &markdown);
    // Splits a body on `##` headings first, then slides a window. Titles are
    // repeated in each chunk so a chunk is self-contained.
    static QVector<QString> chunkText(const QString &title, const QString &body,
                                      int maxChars = 1600, int overlap = 240);

    // Adds `chunk` with `passage: ` prefix (e5 convention), embedding it first.
    bool addChunk(const QString &project, const QString &kind, const QString &title,
                  const QString &loader, const QString &gameVersion, const QString &text,
                  const QString &sourceUrl, QString *error);

private:
    Index *m_index = nullptr;
    Embedder *m_embedder = nullptr;
    QHash<QString, bool> m_seen;
};

} // namespace rag
