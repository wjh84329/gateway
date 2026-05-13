#ifndef GATEWAYAPICLIENT_H
#define GATEWAYAPICLIENT_H

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QString>

struct AppConfigValues;

class GatewayApiClient
{
public:
    GatewayApiClient();
    /// 使用显式配置发起请求（不读取磁盘）；machineCode 为空，适用于仅需 secretKey 的接口（如 UpdateClientIp）。
    explicit GatewayApiClient(const AppConfigValues &explicitConfig);

    /// @param timeoutMs 传 0 使用默认 5s；大响应（安装脚本）可传更大值
    QJsonDocument Get(const QString &apiPath,
                      const QMap<QString, QString> &parameters = {},
                      QString *errorMessage = nullptr,
                      int timeoutMs = 0) const;
    QJsonDocument Post(const QString &apiPath,
                       const QJsonObject &body,
                       QString *errorMessage = nullptr) const;

    QString GetText(const QString &apiPath,
                    const QMap<QString, QString> &parameters = {},
                    QString *errorMessage = nullptr) const;
    QString PostText(const QString &apiPath,
                     const QJsonObject &body,
                     QString *errorMessage = nullptr) const;

    QJsonArray GetPartitions(QString *errorMessage = nullptr) const;
    QJsonArray GetTemplates(QString *errorMessage = nullptr) const;
    QJsonArray GetGroups(QString *errorMessage = nullptr) const;
    /// GET /api/Client/AddClientGroup — 成功返回「添加成功」等纯文本
    bool AddClientGroup(const QString &groupName, QString *errorMessage = nullptr) const;
    /// GET /api/Client/EditClientGroup
    bool EditClientGroup(const QString &groupName, const QString &groupId, QString *errorMessage = nullptr) const;
    /// GET /api/Client/DelClientGroup?groupId=
    bool DeleteClientGroup(const QString &groupId, QString *errorMessage = nullptr) const;
    QJsonArray GetProducts(QString *errorMessage = nullptr) const;
    QJsonArray GetEngines(QString *errorMessage = nullptr) const;
    /// GET /api/Client/GetClientWxmbTemplates — 密保模板下拉 [{Id,Name}]
    QJsonArray GetClientWxmbTemplates(QString *errorMessage = nullptr) const;
    /// GET /api/Client/GetClientScanTemplates — 扫码模板下拉 [{Id,Name}]
    QJsonArray GetClientScanTemplates(QString *errorMessage = nullptr) const;

    /// GET /api/Client/GetClientSignalTemplate — 返回完整 Template JSON（含子集合）
    QJsonObject GetClientSignalTemplate(int templateId, QString *errorMessage = nullptr) const;
    /// GET /api/Client/GetClientInstallScriptFiles — 合并后的安装脚本 [{fileName,content},...]
    /// 带 partitionId 时服务端可能返回对象 { files, partitionOutputOverrides }（Market_Def 单文件覆盖）
    /// @param partitionId 大于 0 时服务端按分区替换 VariableBindingsJson 中的 Partition.* 等
    QJsonDocument GetClientInstallScriptFiles(int templateId,
                                              const QString &gameEngine,
                                              QString *errorMessage = nullptr,
                                              int partitionId = 0) const;
    /// 解析 GetClientInstallScriptFiles 的 JSON（兼容纯数组或带 partitionOutputOverrides 的对象）
    static bool ParseGetClientInstallScriptFilesResponse(const QJsonDocument &doc,
                                                         QHash<QString, QString> *outMergedFiles,
                                                         QHash<QString, QString> *outPartitionOutputOverrides = nullptr,
                                                         QString *errorMessage = nullptr);
    /// POST /api/Client/SaveClientInstallScriptFile — 保存商户覆盖的安装脚本正文
    bool SaveClientInstallScriptFile(int templateId,
                                     const QString &gameEngine,
                                     const QString &fileName,
                                     const QString &content,
                                     QString *errorMessage = nullptr) const;
    /// GET /api/Client/GetTempProductRates
    QJsonArray GetTempProductRates(int templateId, QString *errorMessage = nullptr) const;
    /// POST /api/Client/InstallClientTemplate — 返回服务端纯文本
    QString InstallClientTemplate(const QJsonObject &model, QString *errorMessage = nullptr) const;
    /// POST /api/Client/EditClientTemplate — 成功返回 true（HTTP 200 且文案为成功）
    bool EditClientTemplate(const QJsonObject &model, QString *errorMessage = nullptr) const;
    /// GET /api/Client/DelClientTemplate?templateId= — 与 DelClientPartition 同属 Client 删除类接口
    bool DeleteClientTemplate(int templateId, QString *errorMessage = nullptr) const;

    QString UpdateClientIp(QString *errorMessage = nullptr) const;
    /// GET /api/Client/GetClientMerchantUuid — 同步 MerchantUuid、MerchantName（优先 NickName，否则 UserName，再 Account）。
    /// @return 成功拉取并解析到有效 Uuid 时为 true；网络失败或密钥错误时为 false（不修改已有配置字段）。
    bool SyncClientMerchantIdentity(AppConfigValues &config, QString *errorMessage = nullptr) const;
    /// POST /api/Client/GatewayEndpoint/Register — 写入平台 GatewayEquips（含 AdvertisedHost，与机器码对外 IP 一致，用于按 IP+端口 判重）。
    /// 若本地未填商户 Uuid，会再尝试调用 SyncClientMerchantIdentity 补拉（启动时已同步过则通常不会重复请求）。
    bool RegisterGatewayEndpoint(AppConfigValues &config, QString *errorMessage = nullptr) const;
    /// 平台返回「本 IP+端口已被同商户其他网关占用」时可忽略登记失败并继续启动。
    static bool IsGatewayEndpointOccupiedError(const QString &responseOrErrorText);

    /// POST /api/Client/GatewayEndpoint/Ping — 刷新在线心跳（约每分钟由 UI 侧定时调用）。
    bool PingGatewayPresence(QString *errorMessage = nullptr) const;
    /// POST /api/Client/GatewayEndpoint/Offline — 优雅退出时置离线（任务管理器杀进程无法触发）。
    bool SetGatewayEndpointOffline(QString *errorMessage = nullptr) const;

    /// GET /api/Client/GetGatewayInstallerUpdateInfo — 平台发布的网关版本与下载地址（与商户端「下载网关」同源）。
    QJsonObject GetGatewayInstallerUpdateInfo(QString *errorMessage = nullptr) const;

    QJsonDocument GetOrders(const QJsonObject &query, QString *errorMessage = nullptr) const;

    QString InstallClientPartition(const QJsonObject &model, QString *errorMessage = nullptr) const;
    bool UpdateClientPartition(const QJsonObject &model, QString *errorMessage = nullptr) const;
    bool DeletePartition(int partitionId, QString *errorMessage = nullptr) const;
    bool LoadClientPartition(const QJsonObject &model, QString *errorMessage = nullptr) const;
    bool UpdateOrder(const QJsonObject &model, QString *errorMessage = nullptr) const;
    bool PaidApply(const QJsonObject &model, QString *errorMessage = nullptr) const;
    bool WxValidProcess(const QJsonObject &model, QString *errorMessage = nullptr) const;
    QString GetRechargeFileDownloadUrl(const QString &groupUuid) const;
    bool DownloadRechargeFile(const QString &downloadUrl,
                              const QString &savePath,
                              QString *errorMessage = nullptr) const;

private:
    QString BuildUrl(const QString &apiPath) const;
    QMap<QString, QString> AppendSecretKey(const QMap<QString, QString> &parameters,
                                           bool includeMachineCode = false) const;
    QJsonObject AppendSecretKey(const QJsonObject &body,
                                bool includeMachineCode = false) const;

    bool ExecuteTextRequest(const QString &url,
                            const QString &method,
                            const QJsonObject &body,
                            QString *responseText,
                            QString *errorMessage,
                            int timeoutMs = 0) const;

    QString m_baseDomain;
    QString m_secretKey;
    QString m_machineCode;
};

#endif // GATEWAYAPICLIENT_H
