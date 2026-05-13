#include "startupservice.h"

#include "appconfig.h"
#include "applogger.h"
#include "filemonitorservice.h"
#include "gatewayhttpserver.h"
#include "gatewayapiclient.h"
#include "partitionpathcache.h"
#include "rabbitmqdispatcher.h"
#include "rabbitmqservice.h"
#include "startupsplash.h"

#include <QJsonArray>
#include <QMessageBox>

namespace {

void SplashStep(StartupSplash *splash, const QString &message)
{
    if (!splash) {
        return;
    }
    splash->setStatusText(message);
    splash->pumpEvents();
}

void HideSplashForDialog(StartupSplash *splash)
{
    if (splash) {
        splash->hide();
    }
}
bool EnsureGatewayEndpointRegistered(AppConfigValues *config, StartupSplash *splash)
{
    if (!config) {
        return false;
    }
    SplashStep(splash, QStringLiteral("正在登记网关端点…"));
    QString errorMessage;
    GatewayApiClient client;
    if (!client.RegisterGatewayEndpoint(*config, &errorMessage)) {
        if (GatewayApiClient::IsGatewayEndpointOccupiedError(errorMessage)) {
            AppLogger::WriteLog(QStringLiteral("网关端点登记跳过：当前「网关对外 IP + 通讯端口」已被本商户下其他在线网关占用。%1")
                                    .arg(errorMessage));
            return true;
        }
        AppLogger::WriteLog(QStringLiteral("网关端点登记失败：%1").arg(errorMessage));
        HideSplashForDialog(splash);
        QMessageBox::critical(nullptr,
                              QStringLiteral("启动失败"),
                              QStringLiteral("网关启动失败，请联系客服处理。\n"
                                             "（可将程序所在目录下 log 文件夹中「当天日期.log」提供给客服，便于排查网络与配置。）"));
        return false;
    }
    return true;
}

void LogGatewayListenConfig(const AppConfigValues &config)
{
    Q_UNUSED(config);
}

bool LogServiceConnection(const AppConfigValues &config)
{
    QString errorMessage;
    RabbitMqService::SetMessageHandler(RabbitMqDispatcher::HandleMessage);
    if (!RabbitMqService::StartListening(config, &errorMessage)) {
        AppLogger::WriteLog(QStringLiteral("RabbitMQ 连接失败：%1").arg(errorMessage));
        return false;
    }

    return true;
}

void LogLocalDataLoading(const AppConfigValues &config)
{
    Q_UNUSED(config);
}
} // namespace

static bool g_httpListenSkippedInUse = false;

bool StartupService::TryParseHttpListenPort(const QString &text, quint16 *out, QString *errorMessage)
{
    if (!out) {
        return false;
    }
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("未配置通讯端口");
        }
        return false;
    }
    bool ok = false;
    const int n = trimmed.toInt(&ok);
    if (!ok || n < 1 || n > 65535) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("通讯端口无效（须为 1-65535 的整数）");
        }
        return false;
    }
    *out = static_cast<quint16>(n);
    return true;
}

bool StartupService::HttpListenSkippedDueToAddressInUse()
{
    return g_httpListenSkippedInUse;
}

void StartupService::ApplySavedAppConfigReload()
{
    const AppConfigValues config = AppConfig::Load();

    quint16 httpListenPort = 0;
    QString portError;
    if (!TryParseHttpListenPort(config.port, &httpListenPort, &portError)) {
        AppLogger::WriteLog(QStringLiteral("保存后重载：通讯端口无效，已跳过 HTTP 监听重启：%1").arg(portError));
    } else {
        g_httpListenSkippedInUse = false;
        QString httpListenError;
        auto &httpServer = GatewayHttpServer::instance();
        if (!httpServer.start(httpListenPort, &httpListenError)) {
            if (httpServer.listenErrorIsAddressInUse()) {
                g_httpListenSkippedInUse = true;
                AppLogger::WriteLog(QStringLiteral(
                    "保存后重载：通讯端口 %1 已被本机占用，HTTP 未监听；请更换端口或关闭占用进程后再次保存。")
                                        .arg(httpListenPort));
            } else {
                AppLogger::WriteLog(QStringLiteral("保存后重载：网关 HTTP 监听失败：%1").arg(httpListenError));
            }
        }
    }

    RabbitMqService::SetMessageHandler(RabbitMqDispatcher::HandleMessage);
    QString mqErr;
    RabbitMqService::StopListening();
    if (!RabbitMqService::StartListening(config, &mqErr)) {
        AppLogger::WriteLog(QStringLiteral("保存后重载：RabbitMQ 连接失败：%1").arg(mqErr));
    }

    FileMonitorService::Instance().Initialize(config);

    AppLogger::WriteLog(QStringLiteral("系统设置保存成功，运行配置已重载。"));
}

bool StartupService::RunStartupSequence(StartupSplash *splash)
{
    g_httpListenSkippedInUse = false;
    AppConfig::EnsureConfigExists();
    AppConfigValues config = AppConfig::Load();
    // 自动检测：拉取公网 IP 写入配置（失败则回退本机 IPv4），避免复制部署沿用旧地址
    if (config.gatewayAdvertisedIpAutoDetect) {
        SplashStep(splash, QStringLiteral("正在获取公网 IP…"));
    }
    AppConfig::RefreshGatewayAdvertisedIpForCurrentMachine(config);

    AppLogger::MarkSessionStart();
    SplashStep(splash, QStringLiteral("正在读取配置…"));

    quint16 httpListenPort = 0;
    QString portError;
    if (!TryParseHttpListenPort(config.port, &httpListenPort, &portError)) {
        AppLogger::WriteLog(QStringLiteral("通讯端口配置无效：%1").arg(portError));
        HideSplashForDialog(splash);
        QMessageBox::critical(nullptr, QStringLiteral("启动失败"), portError);
        return false;
    }

    SplashStep(splash, QStringLiteral("正在同步商户信息…"));
    {
        QString merchantSyncErr;
        if (!GatewayApiClient().SyncClientMerchantIdentity(config, &merchantSyncErr)) {
            if (!merchantSyncErr.trimmed().isEmpty()) {
                AppLogger::WriteLog(QStringLiteral("启动时同步商户信息失败（昵称/Uuid 将沿用本地配置）：%1").arg(merchantSyncErr));
            }
        }
    }

    if (!EnsureGatewayEndpointRegistered(&config, splash)) {
        return false;
    }

    SplashStep(splash, QStringLiteral("正在同步分区安装路径…"));
    {
        GatewayApiClient api;
        QString partErr;
        const QJsonArray parts = api.GetPartitions(&partErr);
        PartitionPathCache::UpdateFromPartitionsArray(parts);
        if (parts.isEmpty() && !partErr.isEmpty()) {
            AppLogger::WriteLog(QStringLiteral("启动时拉取分区列表失败（二维码相对路径可能无法按分区盘符解析）：%1").arg(partErr));
        }
    }

    SplashStep(splash, QStringLiteral("正在启动通讯端口服务…"));
    QString httpListenError;
    auto &httpServer = GatewayHttpServer::instance();
    if (!httpServer.start(httpListenPort, &httpListenError)) {
        if (httpServer.listenErrorIsAddressInUse()) {
            g_httpListenSkippedInUse = true;
            AppLogger::WriteLog(QStringLiteral(
                "通讯端口 %1 已被本机其他进程占用（常见于其他目录下的网关使用相同端口），本进程不监听该端口但仍进入主界面。"
                "请在本目录「系统设置」中更换通讯端口，或关闭占用该端口的程序后保存并重启。")
                                    .arg(httpListenPort));
        } else {
            AppLogger::WriteLog(QStringLiteral("网关通讯端口监听失败：%1").arg(httpListenError));
            HideSplashForDialog(splash);
            QMessageBox::critical(nullptr,
                                  QStringLiteral("启动失败"),
                                  QStringLiteral("网关通讯端口监听失败：%1").arg(httpListenError));
            return false;
        }
    }

    LogGatewayListenConfig(config);
    SplashStep(splash, QStringLiteral("正在连接消息服务…"));
    const bool rabbitConnected = LogServiceConnection(config);
    LogLocalDataLoading(config);
    SplashStep(splash, QStringLiteral("正在初始化文件监控…"));
    FileMonitorService::Instance().Initialize(config);
    if (!rabbitConnected) {
        AppLogger::WriteLog(QStringLiteral("RabbitMQ 消费者未就绪，消息功能不可用"));
    }
    SplashStep(splash, QStringLiteral("正在加载主界面…"));
    return true;
}
