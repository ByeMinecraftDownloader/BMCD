#include "mods/ModAliases.h"

#include <QFile>
#include <QJsonDocument>

namespace mods {

QString expandAlias(const QString &query, const QJsonObject &table)
{
    const QString key = query.trimmed().toLower();
    const auto it = table.find(key);
    if (it == table.end() || !it.value().isString())
        return query;
    const QString expanded = it.value().toString().trimmed();
    return expanded.isEmpty() ? query : expanded;
}

QJsonObject loadAliasTable()
{
    QFile file(QStringLiteral(":/data/mod-aliases.json"));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

} // namespace mods
