#ifndef MACHINECODE_H
#define MACHINECODE_H

#include <QString>

struct AppConfigValues;

/// 设备实例标识（原「机器码」）：由 服务器IP + 通讯端口 + 商户 Uuid 稳定派生，
/// 与硬件、通讯密钥无关；变更 IP/端口/Uuid 后值会变，需在平台侧更新分区绑定。
class MachineCode
{
public:
    MachineCode();
    /// 未配置网关对外 IP 时用于机器码与界面展示的默认 IPv4（首个非回环 IPv4，无则 127.0.0.1）。
    static QString PreferredLocalIPv4();
    /// 与 GetRNum 使用的对外 IPv4 一致（配置非空用配置，否则本机首选 IPv4），登记网关端点时应与之一并提交 AdvertisedHost。
    static QString EffectiveGatewayAdvertisedHostForConfig(const AppConfigValues &cfg);
    /// 通过 HTTPS 请求 icanhazip.com 获取当前公网 IPv4（超时/失败返回 false，由调用方回退内网 IP）。
    static bool TryFetchPublicIPv4(QString *outIp, QString *errorMessage = nullptr);
    /// 将平台/探测返回的文本规范为 IPv4（支持 IPv4 字面量及 ::ffff:x.x.x.x）；无法解析则返回 false。
    static bool TryNormalizeAdvertisedIpString(const QString &text, QString *outIpv4);
    /// 返回 64 位十六进制字符串（SHA256）；配置不完整时返回空。
    QString GetRNum() const;
    QString GetCurrentUserName() const;

private:
    QString GetCurrentUser() const;
};

#endif // MACHINECODE_H
