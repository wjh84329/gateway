#include "appversion.h"

QString GatewayApp::versionString()
{
    return QStringLiteral("2.4.7");
}

QString GatewayApp::versionDisplay()
{
    return QStringLiteral("V") + versionString();
}
