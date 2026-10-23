#pragma once

#include <QProcess>

namespace platform {

// Thin platform-layer process type. Keep QProcess out of core so process
// launch policy (environment scrubbing, console handling, sandbox rules) has a
// single place to live; callers still get the normal Qt process API.
class Process : public QProcess
{
public:
    using QProcess::QProcess;
};

} // namespace platform
