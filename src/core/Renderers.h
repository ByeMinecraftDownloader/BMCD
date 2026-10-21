#pragma once

#include "core/LicenseGate.h"
#include "core/Pack.h"

#include <functional>

// Two exits from the serve screen, one model:
//  * MrpackRenderer  -> reference-only .mrpack (Modrinth format, no jars inside)
//  * FtbRenderer     -> reference-only FTB manifest (files with urls + hashes)
//  * SelfContainedRenderer -> archives the jars themselves (needs the licence gate)
struct RenderOptions
{
    QString outputPath;
    QString sourceRoot;                  // defaults to Pack::target
    QStringList confirmedPaths;          // licence confirmations
    std::function<void(int done, int total, const QString &step)> progress;
};

struct RenderResult
{
    bool ok = false;
    QString error;
    qint64 bytes = 0;
    int fileCount = 0;
};

RenderResult renderMrpack(const Pack &pack, const RenderOptions &options);
RenderResult renderFtb(const Pack &pack, const RenderOptions &options);
RenderResult renderSelfContained(const Pack &pack, const RenderOptions &options,
                                 const license::GateResult &gate);
