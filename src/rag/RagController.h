#pragma once

#include "rag/Embedder.h"
#include "rag/Index.h"
#include "rag/Retriever.h"

#include <QObject>
#include <QStringList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <memory>

// Exposes the local knowledge index to QML. The index is opened lazily so the
// GUI starts even when no index was built yet; a missing index only disables
// suggestions instead of failing the window.
class RagController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(int chunkCount READ chunkCount NOTIFY changed)
    Q_PROPERTY(QString embedderName READ embedderName NOTIFY changed)
    Q_PROPERTY(QString lastNote READ lastNote NOTIFY changed)

public:
    explicit RagController(QObject *parent = nullptr);
    ~RagController() override;

    bool available() const { return m_available; }
    int chunkCount() const { return int(m_count); }
    QString embedderName() const { return m_embedderName; }
    QString lastNote() const { return m_lastNote; }

    // Human readable "Title — snippet" entries for the current query.
    Q_INVOKABLE QStringList suggest(const QString &query, int top = 4);
    Q_INVOKABLE QVariantMap stats() const;

signals:
    void changed();

private:
    bool ensureOpen();

    rag::Index m_index;
    std::unique_ptr<rag::Embedder> m_embedder;
    std::unique_ptr<rag::Retriever> m_retriever;
    bool m_attempted = false;
    bool m_available = false;
    qint64 m_count = 0;
    QString m_embedderName;
    QString m_lastNote;
};
