#pragma once

#include <QJsonObject>
#include <QString>

namespace mods {

// Basic lookup table: a Chinese/alias term expands to a Modrinth search term.
// This is deliberately a table, not an embedding: 1.1 must not load a model.
QString expandAlias(const QString &query, const QJsonObject &table);
QJsonObject loadAliasTable();

} // namespace mods
