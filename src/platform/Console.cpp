#include "platform/Console.h"

#include <QCoreApplication>
#include <QNetworkProxyFactory>

namespace platform {

void installSystemProxy()
{
    const QString disable = qEnvironmentVariable("MC_NO_SYSTEM_PROXY").trimmed().toLower();
    if (disable == QLatin1String("1") || disable == QLatin1String("true"))
        return;
    QNetworkProxyFactory::setUseSystemConfiguration(true);
}

QString userAgent()
{
    const QString version = QCoreApplication::applicationVersion();
    return QStringLiteral("BMCD/") + (version.isEmpty() ? QStringLiteral("dev") : version);
}

} // namespace platform
