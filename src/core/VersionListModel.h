#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QList>
#include <QString>
#include <qqmlintegration.h>

struct VersionEntry
{
    QString id;
    QString type; // release | snapshot | old_beta | old_alpha
    QString url;
    QDateTime releaseTime;
    bool latest = false;
};

class VersionListModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        TypeRole,
        UrlRole,
        ReleaseTimeRole,
        LatestRole,
    };

    explicit VersionListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void reset(QList<VersionEntry> entries);
    QString urlFor(const QString &versionId) const;

private:
    QList<VersionEntry> m_entries;
};
