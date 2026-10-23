#include "core/VersionFilterModel.h"

#include "core/VersionListModel.h"

VersionFilterModel::VersionFilterModel(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    const auto notifyCount = [this] { emit countChanged(); };
    connect(this, &QAbstractItemModel::rowsInserted, this, notifyCount);
    connect(this, &QAbstractItemModel::rowsRemoved, this, notifyCount);
    connect(this, &QAbstractItemModel::modelReset, this, notifyCount);
    connect(this, &QAbstractItemModel::layoutChanged, this, notifyCount);
}

int VersionFilterModel::indexOfVersion(const QString &versionId) const
{
    for (int row = 0; row < rowCount(); ++row) {
        if (index(row, 0).data(VersionListModel::IdRole).toString() == versionId)
            return row;
    }
    return -1;
}

void VersionFilterModel::setSearchText(const QString &text)
{
    if (m_searchText == text)
        return;
    m_searchText = text;
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    beginFilterChange();
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
#else
    invalidateRowsFilter();
#endif
    emit countChanged();
    emit searchTextChanged();
}

void VersionFilterModel::setChannel(const QString &channel)
{
    if (m_channel == channel)
        return;
    m_channel = channel;
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    beginFilterChange();
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
#else
    invalidateRowsFilter();
#endif
    emit countChanged();
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
