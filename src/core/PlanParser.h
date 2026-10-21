#pragma once

#include "Plan.h"

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QSet>

// Turns a Mojang version json (+ its asset index json) into a flat download
// plan. Pure functions only: no network, no disk, so the whole "what needs to
// be downloaded" logic can be exercised from the command line.
namespace PlanParser {

// Which OS family the current binary should pick when a library only ships
// per-OS artifacts. Values match the "os.name" field in version jsons.
QString hostOsName();

// Builds the plan. Returns false and fills *error when the version json is
// something we cannot flatten (inherited versions, missing client jar, ...).
bool build(const QJsonObject &versionJson,
           const QJsonObject &assetIndexJson,
           TaskPlan *plan,
           QString *error);

// Same plan, plus the version json itself: an installed version is recognised
// by versions/<id>/<id>.json, so a download that omits it leaves a directory
// every launcher (ours included) will ignore.
bool build(const QJsonObject &versionJson,
           const QByteArray &rawVersionJson,
           const QString &versionJsonUrl,
           const QJsonObject &assetIndexJson,
           TaskPlan *plan,
           QString *error);

// Asset objects are content-addressed, so the index lists names while the URL
// is derived from the hash. Shared with the downloader's integrity check.
QString assetUrl(const QString &hash);
QString assetPath(const QString &hash);

} // namespace PlanParser
