#pragma once

#include <QSortFilterProxyModel>
#include <QString>
#include <qqmlintegration.h>

// Filters the manifest by channel ("all" / "release" / "snapshot") and by a
// case-insensitive substring of the version id.
class VersionFilterModel : public QSortFilterProxyModel
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString searchText READ searchText WRITE setSearchText NOTIFY searchTextChanged)
    Q_PROPERTY(QString channel READ channel WRITE setChannel NOTIFY channelChanged)

public:
    explicit VersionFilterModel(QObject *parent = nullptr);

    QString searchText() const { return m_searchText; }
    void setSearchText(const QString &text);

    QString channel() const { return m_channel; }
    void setChannel(const QString &channel);

signals:
    void searchTextChanged();
    void channelChanged();

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
    QString m_searchText;
    QString m_channel = QStringLiteral("release");
};
