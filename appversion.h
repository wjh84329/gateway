#ifndef APPVERSION_H
#define APPVERSION_H

#include <QString>

namespace GatewayApp {
/// 语义化版本号（不含前缀 V），与在线更新清单 latestVersion 对比。
QString versionString();
QString versionDisplay();
} // namespace GatewayApp

#endif
