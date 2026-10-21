#include "core/VersionFilterModel.h"

#include "core/VersionListModel.h"

VersionFilterModel::VersionFilterModel(QObject *parent)
    : QSortFilterProxyModel(parent)
{
}

void VersionFilterModel::setSearchText(const QString &text)
{
    if (m_searchText == text)
        return;
    m_searchText = text;
    beginFilterChange();
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
    emit searchTextChanged();
}

void VersionFilterModel::setChannel(const QString &channel)
{
    if (m_channel == channel)
        return;
    m_channel = channel;
    beginFilterChange();
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
    emit channelChanged();
}

bool VersionFilterModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    const QModelIndex index = sourceModel()->index(sourceRow, 0, sourceParent);
    if (!index.isValid())
        return false;

    const QString type = index.data(VersionListModel::TypeRole).toString();
    if (m_channel == QLatin1String("release")) {
        if (type != QLatin1String("release"))
            return false;
    } else if (m_channel == QLatin1String("snapshot")) {
        if (type != QLatin1String("snapshot"))
            return false;
    }

    if (!m_searchText.isEmpty()) {
        const QString id = index.data(VersionListModel::IdRole).toString();
        if (!id.contains(m_searchText, Qt::CaseInsensitive))
            return false;
    }

    return true;
}
