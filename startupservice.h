#ifndef STARTUPSERVICE_H
#define STARTUPSERVICE_H

#include <QtGlobal>
#include <QString>

class StartupSplash;

namespace StartupService {
/// splash 可为空；非空时在关键步骤更新文案并刷新事件，错误弹窗前会隐藏启动页。
bool RunStartupSequence(StartupSplash *splash = nullptr);

/// 校验「通讯端口」文本（保存设置前与重载前共用）。
bool TryParseHttpListenPort(const QString &text, quint16 *out, QString *errorMessage = nullptr);

/// 设置页保存到本机后调用：按磁盘上的最新配置重启 HTTP 监听、RabbitMQ 消费与文件监控。
void ApplySavedAppConfigReload();

/// 最近一次启动是否因「HTTP 端口已被占用」而跳过监听（仍可进入主界面）。
bool HttpListenSkippedDueToAddressInUse();
}

#endif // STARTUPSERVICE_H
