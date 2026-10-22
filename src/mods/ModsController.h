#pragma once

#include <QNetworkAccessManager>
#include <QJsonObject>
#include <QObject>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include "mods/ModResolver.h"

class QNetworkReply;

// Search Modrinth, keep the user's selection, and turn it into a Pack plan.
// Network calls are asynchronous for search; dependency resolution runs in a
// worker thread because it walks several endpoints.
class ModsController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QVariantList results READ results NOTIFY resultsChanged)
    Q_PROPERTY(QVariantList selected READ selected NOTIFY selectedChanged)
    Q_PROPERTY(int selectedCount READ selectedCount NOTIFY selectedChanged)
    Q_PROPERTY(bool searching READ searching NOTIFY searchingChanged)
    Q_PROPERTY(bool planning READ planning NOTIFY planningChanged)
    Q_PROPERTY(bool hasMore READ hasMore NOTIFY resultsChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(QString planError READ planError NOTIFY planningChanged)
    Q_PROPERTY(bool awaitingConfirmation READ awaitingConfirmation NOTIFY pendingChanged)
    Q_PROPERTY(QStringList awaitingDependencies READ awaitingDependencies NOTIFY pendingChanged)
    Q_PROPERTY(QString note READ note NOTIFY noteChanged)
    Q_PROPERTY(QString loader READ loader NOTIFY contextChanged)
    Q_PROPERTY(QString gameVersion READ gameVersion NOTIFY contextChanged)

public:
    explicit ModsController(QObject *parent = nullptr);

    QVariantList results() const { return m_results; }
    QVariantList selected() const { return m_selected; }
    int selectedCount() const { return int(m_selected.size()); }
    bool searching() const { return m_searching; }
    bool planning() const { return m_planning; }
    bool hasMore() const { return m_hasMore; }
    QString error() const { return m_error; }
    QString planError() const { return m_planError; }
    bool awaitingConfirmation() const { return m_awaitingConfirmation; }
    QStringList awaitingDependencies() const { return m_awaitingDependencies; }
    QString note() const { return m_note; }
    QString loader() const { return m_loader; }
    QString gameVersion() const { return m_gameVersion; }

    Q_INVOKABLE void setContext(const QString &loader, const QString &gameVersion);
    Q_INVOKABLE void search(const QString &query);
    Q_INVOKABLE void loadMore();
    Q_INVOKABLE void addProject(const QString &slug, const QString &title, const QString &iconUrl = {});
    Q_INVOKABLE void removeProject(const QString &slug);
    Q_INVOKABLE bool isSelected(const QString &slug) const;
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE void buildPlan();
    Q_INVOKABLE void confirmPlan();
    Q_INVOKABLE void cancelPlan();

signals:
    void resultsChanged();
    void selectedChanged();
    void searchingChanged();
    void planningChanged();
    void errorChanged();
    void noteChanged();
    void contextChanged();
    void planReady(const QString &json);
    void planConfirmationRequested();
    void pendingChanged();
    void message(const QString &text);

private:
    void requestPage(const QString &query, int offset);
    void finishSearch(QNetworkReply *reply, int generation);
    QVariantMap parseHit(const QJsonObject &object) const;
    void applyPlan(const mods::ResolveResult &result);

    QNetworkAccessManager m_nam;
    QNetworkReply *m_searchReply = nullptr;
    QJsonObject m_aliases;
    QString m_query;
    int m_offset = 0;
    int m_generation = 0;

    QVariantList m_results;
    QVariantList m_selected;
    bool m_searching = false;
    bool m_planning = false;
    bool m_hasMore = false;
    QString m_error;
    QString m_planError;
    QString m_pendingPlan;
    QStringList m_awaitingDependencies;
    bool m_awaitingConfirmation = false;
    QString m_note;
    QString m_loader = QStringLiteral("fabric");
    QString m_gameVersion;
};
