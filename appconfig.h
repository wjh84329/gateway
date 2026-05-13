#ifndef APPCONFIG_H
#define APPCONFIG_H

#include <QString>

struct AppConfigValues
{
    /// 商户展示名：启动/保存时由平台同步（优先 NickName，否则 UserName、Account），写入 MerchantName。
    QString merchantName;
    /// 商户 Uuid（与平台 ApplicationUser.Uuid 一致，与 SecretKey 独立，密钥轮换后不变）
    QString merchantUuid;
    QString website;
    QString installStartPath;
    QString yxsmDir;
    /// 是否已在首次启动时按本机卷根重写过 YxsmDir（仅一次；之后由安装脚本等追加盘符）
    bool yxsmDirVolumeRootsSeeded = false;
    QString wxValidPath;
    QString paidDir;
    QString restUrl;
    QString port;
    /// 参与设备实例标识（机器码）的对外 IP；与网页安装分区时填的网关 IP 一致。
    /// 若 gatewayAdvertisedIpAutoDetect 为 true，启动时优先请求租户平台 UpdateClientIp（平台看到的出口 IPv4），失败再试 icanhazip.com，最后本机首选 IPv4；换机复制不会沿用旧 IP。
    QString gatewayAdvertisedIp;
    /// 为 true 时机器码始终用本机当前首选 IPv4；为 false 时用 gatewayAdvertisedIp（NAT/固定公网时关闭并手动填写）
    bool gatewayAdvertisedIpAutoDetect = true;
    QString rabbitMqHost;
    int rabbitMqAmqpPort = 5672;
    int rabbitMqManagementPort = 15672;
    QString rabbitMqVirtualHost;
    QString rabbitMqQueue;
    QString rabbitMqUser;
    QString rabbitMqPassword;
    QString socketConnectionString;
    QString sqlConnectionStr;
    QString secretKey;
    QString signKey;
    bool isOpenLog = true;
    bool isOpenOrderReissue = true;
    bool isWeixinZq = false;
    bool isWeixinMb = false;
    bool bootUp = false;
    bool isSm = false;
    /// 主窗口点关闭：空=每次询问，tray=最小化到托盘，exit=退出程序
    QString mainWindowCloseAction;
    /// 在线更新：版本清单 JSON 的 HTTPS/HTTP 地址（空则仅可手动在设置页填写后再检查）
    QString updateManifestUrl;
};

namespace AppConfig {
QString ConfigFilePath();
/// 合并 INI：保留 userPath 中全部键值；仅当某键在 userPath 中不存在时，从 packagePath 写入默认值（用于在线更新引入新配置项）。
bool MergeIniAddMissingKeys(const QString &userPath, const QString &packagePath, const QString &destPath, QString *errorMessage = nullptr);
void EnsureConfigExists();
AppConfigValues Load();
void Save(const AppConfigValues &values);
/// 自动探测链：平台 UpdateClientIp → icanhazip → 本机首选 IPv4（与 Refresh 所用一致）。
QString ResolveGatewayAdvertisedIpForAutoDetect(const AppConfigValues &values);
/// 自动检测模式下按 ResolveGatewayAdvertisedIpForAutoDetect 更新 gatewayAdvertisedIp 并保存
void RefreshGatewayAdvertisedIpForCurrentMachine(AppConfigValues &values);
bool IsAutoStartEnabled();
void SetAutoStartEnabled(bool enabled);
}

#endif // APPCONFIG_H
