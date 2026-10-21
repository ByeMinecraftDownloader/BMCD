#include "core/VersionListModel.h"

VersionListModel::VersionListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int VersionListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_entries.size());
}

QVariant VersionListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size())
        return {};

    const VersionEntry &entry = m_entries.at(index.row());
    switch (role) {
    case IdRole:
        return entry.id;
    case TypeRole:
        return entry.type;
    case UrlRole:
        return entry.url;
    case ReleaseTimeRole:
        return entry.releaseTime;
    case LatestRole:
        return entry.latest;
    default:
        return {};
    }
}

QHash<int, QByteArray> VersionListModel::roleNames() const
{
    return {
            {IdRole, "id"},
            {TypeRole, "type"},
            {UrlRole, "url"},
            {ReleaseTimeRole, "releaseTime"},
            {LatestRole, "latest"},
    };
}

void VersionListModel::reset(QList<VersionEntry> entries)
{
    beginResetModel();
    m_entries = std::move(entries);
    endResetModel();
}

QString VersionListModel::urlFor(const QString &versionId) const
{
    for (const VersionEntry &entry : m_entries) {
        if (entry.id == versionId)
            return entry.url;
    }
    return {};
}
