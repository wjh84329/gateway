#include "gatewayapiclient.h"

#include "appconfig.h"
#include "applogger.h"
#include "machinecode.h"

#include <QEventLoop>
#include <QFile>
#include <QJsonParseError>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

namespace {
constexpr auto kRequestTimeoutMs = 5000;
constexpr auto kPartitionApi = "/api/Client/GetClientPartitions";
constexpr auto kTemplateApi = "/api/Client/GetClientTemplates";
constexpr auto kSignalTemplateApi = "/api/Client/GetClientSignalTemplate";
constexpr auto kTempProductRatesApi = "/api/Client/GetTempProductRates";
constexpr auto kInstallClientTemplateApi = "/api/Client/InstallClientTemplate";
constexpr auto kEditClientTemplateApi = "/api/Client/EditClientTemplate";
constexpr auto kDelClientTemplateApi = "/api/Client/DelClientTemplate";
constexpr auto kGroupApi = "/api/Client/GetClientGroups";
constexpr auto kAddClientGroupApi = "/api/Client/AddClientGroup";
constexpr auto kEditClientGroupApi = "/api/Client/EditClientGroup";
constexpr auto kDelClientGroupApi = "/api/Client/DelClientGroup";
constexpr auto kOrdersApi = "/api/Client/GetClientOrders";
constexpr auto kInstallPartitionApi = "/api/Client/InstallClientPartition";
constexpr auto kUpdatePartitionApi = "/api/Client/UpdateClientPartition";
constexpr auto kDeletePartitionApi = "/api/Client/DelClientPartition";
constexpr auto kProductApi = "/api/Recharge/GetProducts";
constexpr auto kEngineListApi = "/api/Client/GetEngineList";
constexpr auto kClientWxmbTemplatesApi = "/api/Client/GetClientWxmbTemplates";
constexpr auto kClientScanTemplatesApi = "/api/Client/GetClientScanTemplates";
constexpr auto kLoadPartitionApi = "/api/Client/ClientLoadPartition";
constexpr auto kUpdateOrderApi = "/api/Client/UpdateOrder";
constexpr auto kPaidApplyApi = "/api/Client/PaidApply";
constexpr auto kWxValidProcessApi = "/api/Client/WxValidProcess";
constexpr auto kCreateRechargeFileApi = "/api/Client/CreateRechargeFileAsync";
constexpr auto kUpdateIpApi = "/api/Client/UpdateClientIp";
constexpr auto kRegisterGatewayEndpointPath = "/api/Client/GatewayEndpoint/Register";
constexpr auto kGatewayEndpointPingPath = "/api/Client/GatewayEndpoint/Ping";
constexpr auto kGatewayEndpointOfflinePath = "/api/Client/GatewayEndpoint/Offline";
constexpr auto kGetGatewayInstallerUpdateInfoPath = "/api/Client/GetGatewayInstallerUpdateInfo";
constexpr auto kGetClientMerchantUuidPath = "/api/Client/GetClientMerchantUuid";
constexpr auto kGetClientInstallScriptFilesApi = "/api/Client/GetClientInstallScriptFiles";
constexpr auto kSaveClientInstallScriptFileApi = "/api/Client/SaveClientInstallScriptFile";
constexpr int kInstallScriptHttpTimeoutMs = 45000;
constexpr int kGatewayUpdateInfoTimeoutMs = 15000;

QString NormalizeBaseUrl(QString url)
{
    url = url.trimmed();
    while (url.endsWith(QLatin1Char('/'))) {
        url.chop(1);
    }
    return url;
}

QString TrimResponseText(QString text)
{
    text = text.trimmed();
    if (!text.isEmpty() && text.front().unicode() == 0xFEFF) {
        text.remove(0, 1);
    }
    if (text.startsWith(QLatin1Char('"')) && text.endsWith(QLatin1Char('"')) && text.size() >= 2) {
        text = text.mid(1, text.size() - 2);
    }
    return text;
}

QJsonDocument ParseJsonDocumentRelaxed(QString text, QString *errorDetail = nullptr)
{
    text = text.trimmed();
    if (!text.isEmpty() && text.front().unicode() == 0xFEFF) {
        text.remove(0, 1);
    }

    auto tryParse = [&](const QString &candidate, QJsonDocument *document) {
        QJsonParseError parseError;
        *document = QJsonDocument::fromJson(candidate.toUtf8(), &parseError);
        if (parseError.error == QJsonParseError::NoError && !document->isNull()) {
            if (errorDetail) {
                errorDetail->clear();
            }
            return true;
        }
        if (errorDetail) {
            *errorDetail = parseError.errorString();
        }
        return false;
    };

    QJsonDocument document;
    if (tryParse(text, &document)) {
        return document;
    }

    const int objectStart = text.indexOf(QLatin1Char('{'));
    const int objectEnd = text.lastIndexOf(QLatin1Char('}'));
    if (objectStart >= 0 && objectEnd > objectStart && tryParse(text.mid(objectStart, objectEnd - objectStart + 1), &document)) {
        return document;
    }

    const int arrayStart = text.indexOf(QLatin1Char('['));
    const int arrayEnd = text.lastIndexOf(QLatin1Char(']'));
    if (arrayStart >= 0 && arrayEnd > arrayStart && tryParse(text.mid(arrayStart, arrayEnd - arrayStart + 1), &document)) {
        return document;
    }

    if (text.startsWith(QLatin1Char('"')) && text.endsWith(QLatin1Char('"'))) {
        QJsonDocument wrapperDocument;
        if (tryParse(QStringLiteral("[") + text + QStringLiteral("]"), &wrapperDocument)
            && wrapperDocument.isArray()
            && !wrapperDocument.array().isEmpty()
            && wrapperDocument.array().first().isString()) {
            if (tryParse(wrapperDocument.array().first().toString(), &document)) {
                return document;
            }
        }
    }

    return {};
}

bool IsSuccessText(const QString &text, const QStringList &successValues)
{
    for (const auto &successValue : successValues) {
        if (text.compare(successValue, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

QJsonObject ToJsonObject(const QMap<QString, QString> &parameters)
{
    QJsonObject object;
    for (auto it = parameters.cbegin(); it != parameters.cend(); ++it) {
        object.insert(it.key(), it.value());
    }
    return object;
}
}

GatewayApiClient::GatewayApiClient()
{
    const AppConfigValues values = AppConfig::Load();
    m_baseDomain = NormalizeBaseUrl(values.restUrl);
    m_secretKey = values.secretKey.trimmed();
    m_machineCode = MachineCode().GetRNum().trimmed();
}

GatewayApiClient::GatewayApiClient(const AppConfigValues &explicitConfig)
    : m_baseDomain(NormalizeBaseUrl(explicitConfig.restUrl.trimmed()))
    , m_secretKey(explicitConfig.secretKey.trimmed())
    , m_machineCode()
{
}

QJsonDocument GatewayApiClient::Get(const QString &apiPath,
                                    const QMap<QString, QString> &parameters,
                                    QString *errorMessage,
                                    int timeoutMs) const
{
    QString responseText;
    if (!ExecuteTextRequest(BuildUrl(apiPath), QStringLiteral("GET"), ToJsonObject(parameters), &responseText, errorMessage, timeoutMs)) {
        return {};
    }

    if (responseText.trimmed().isEmpty()) {
        if (errorMessage) {
            errorMessage->clear();
        }
        return {};
    }

    QString parseError;
    const QJsonDocument document = ParseJsonDocumentRelaxed(responseText, &parseError);
    if (document.isNull() && errorMessage) {
        *errorMessage = QStringLiteral("接口返回的 JSON 无法解析：%1").arg(parseError);
        AppLogger::WriteLog(QStringLiteral("接口 JSON 解析失败，响应片段：%1").arg(responseText.left(200)));
    }
    return document;
}

QJsonDocument GatewayApiClient::Post(const QString &apiPath,
                                     const QJsonObject &body,
                                     QString *errorMessage) const
{
    QString responseText;
    if (!ExecuteTextRequest(BuildUrl(apiPath), QStringLiteral("POST"), body, &responseText, errorMessage)) {
        return {};
    }

    if (responseText.trimmed().isEmpty()) {
        if (errorMessage) {
            errorMessage->clear();
        }
        return {};
    }

    QString parseError;
    const QJsonDocument document = ParseJsonDocumentRelaxed(responseText, &parseError);
    if (document.isNull() && errorMessage) {
        *errorMessage = QStringLiteral("接口返回的 JSON 无法解析：%1").arg(parseError);
        AppLogger::WriteLog(QStringLiteral("接口 JSON 解析失败，响应片段：%1").arg(responseText.left(200)));
    }
    return document;
}

QString GatewayApiClient::GetText(const QString &apiPath,
                                  const QMap<QString, QString> &parameters,
                                  QString *errorMessage) const
{
    QString responseText;
    if (!ExecuteTextRequest(BuildUrl(apiPath), QStringLiteral("GET"), ToJsonObject(parameters), &responseText, errorMessage)) {
        return {};
    }
    return TrimResponseText(responseText);
}

QString GatewayApiClient::PostText(const QString &apiPath,
                                   const QJsonObject &body,
                                   QString *errorMessage) const
{
    QString responseText;
    if (!ExecuteTextRequest(BuildUrl(apiPath), QStringLiteral("POST"), body, &responseText, errorMessage)) {
        return {};
    }
    return TrimResponseText(responseText);
}

QJsonArray GatewayApiClient::GetPartitions(QString *errorMessage) const
{
    return Get(kPartitionApi, AppendSecretKey(QMap<QString, QString>{}, true), errorMessage).array();
}

QJsonArray GatewayApiClient::GetTemplates(QString *errorMessage) const
{
    return Get(kTemplateApi, AppendSecretKey(QMap<QString, QString>{}), errorMessage).array();
}

QJsonArray GatewayApiClient::GetGroups(QString *errorMessage) const
{
    return Get(kGroupApi, AppendSecretKey(QMap<QString, QString>{}), errorMessage).array();
}

bool GatewayApiClient::AddClientGroup(const QString &groupName, QString *errorMessage) const
{
    const QString trimmed = groupName.trimmed();
    if (trimmed.isEmpty()) {
        if (errorMessage)
            *errorMessage = QStringLiteral("分组名称不能为空");
        return false;
    }
    const QString response = GetText(kAddClientGroupApi,
                                       AppendSecretKey(QMap<QString, QString>{{QStringLiteral("groupName"), trimmed}}),
                                       errorMessage);
    return IsSuccessText(response, {QStringLiteral("添加成功"), QStringLiteral("操作成功")});
}

bool GatewayApiClient::EditClientGroup(const QString &groupName, const QString &groupId, QString *errorMessage) const
{
    const QString trimmedName = groupName.trimmed();
    const QString trimmedId = groupId.trimmed();
    if (trimmedName.isEmpty()) {
        if (errorMessage)
            *errorMessage = QStringLiteral("分组名称不能为空");
        return false;
    }
    if (trimmedId.isEmpty()) {
        if (errorMessage)
            *errorMessage = QStringLiteral("分组编号无效");
        return false;
    }
    const QString response = GetText(kEditClientGroupApi,
                                       AppendSecretKey(QMap<QString, QString>{{QStringLiteral("groupName"), trimmedName},
                                                                            {QStringLiteral("groupId"), trimmedId}}),
                                       errorMessage);
    return IsSuccessText(response, {QStringLiteral("编辑成功"), QStringLiteral("操作成功")});
}

bool GatewayApiClient::DeleteClientGroup(const QString &groupId, QString *errorMessage) const
{
    const QString trimmedId = groupId.trimmed();
    if (trimmedId.isEmpty()) {
        if (errorMessage)
            *errorMessage = QStringLiteral("分组编号无效");
        return false;
    }
    const QString response = GetText(kDelClientGroupApi,
                                      AppendSecretKey(QMap<QString, QString>{{QStringLiteral("groupId"), trimmedId}}),
                                      errorMessage);
    return IsSuccessText(response,
                         {QStringLiteral("删除成功"),
                          QStringLiteral("操作成功"),
                          QStringLiteral("分组删除成功")});
}

QJsonArray GatewayApiClient::GetProducts(QString *errorMessage) const
{
    return Get(kProductApi, {}, errorMessage).array();
}

QJsonArray GatewayApiClient::GetEngines(QString *errorMessage) const
{
    return Get(kEngineListApi, {}, errorMessage).array();
}

QJsonArray GatewayApiClient::GetClientWxmbTemplates(QString *errorMessage) const
{
    return Get(kClientWxmbTemplatesApi, AppendSecretKey(QMap<QString, QString>{}), errorMessage).array();
}

QJsonArray GatewayApiClient::GetClientScanTemplates(QString *errorMessage) const
{
    return Get(kClientScanTemplatesApi, AppendSecretKey(QMap<QString, QString>{}), errorMessage).array();
}

QJsonObject GatewayApiClient::GetClientSignalTemplate(int templateId, QString *errorMessage) const
{
    const QJsonDocument doc = Get(kSignalTemplateApi,
                                  AppendSecretKey(QMap<QString, QString>{{QStringLiteral("templateId"), QString::number(templateId)}}),
                                  errorMessage);
    return doc.isObject() ? doc.object() : QJsonObject{};
}

namespace {
QString InstallScriptJsonStringField(const QJsonObject &o, const QStringList &names)
{
    for (const auto &n : names) {
        const QJsonValue v = o.value(n);
        if (v.isString()) {
            return v.toString();
        }
    }
    return {};
}

void MergeInstallScriptFileItemsToMap(const QJsonArray &items, QHash<QString, QString> *outMap)
{
    if (!outMap) {
        return;
    }
    for (const QJsonValue &item : items) {
        if (!item.isObject()) {
            continue;
        }
        const QJsonObject o = item.toObject();
        const QString fileName = InstallScriptJsonStringField(o, {QStringLiteral("fileName"), QStringLiteral("FileName")});
        if (fileName.isEmpty()) {
            continue;
        }
        outMap->insert(fileName,
                       InstallScriptJsonStringField(o, {QStringLiteral("content"), QStringLiteral("Content")}));
    }
}
} // namespace

bool GatewayApiClient::ParseGetClientInstallScriptFilesResponse(const QJsonDocument &doc,
                                                                QHash<QString, QString> *outMergedFiles,
                                                                QHash<QString, QString> *outPartitionOutputOverrides,
                                                                QString *errorMessage)
{
    if (!outMergedFiles) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("内部错误");
        }
        return false;
    }
    outMergedFiles->clear();
    if (outPartitionOutputOverrides) {
        outPartitionOutputOverrides->clear();
    }

    QJsonArray filesArr;
    if (doc.isArray()) {
        filesArr = doc.array();
    } else if (doc.isObject()) {
        const QJsonObject root = doc.object();
        filesArr = root.value(QStringLiteral("files")).toArray();
        if (outPartitionOutputOverrides) {
            const QJsonArray po = root.value(QStringLiteral("partitionOutputOverrides")).toArray();
            MergeInstallScriptFileItemsToMap(po, outPartitionOutputOverrides);
        }
    } else {
        if (errorMessage) {
            *errorMessage = QStringLiteral("获取安装脚本模板失败（平台未返回脚本列表）");
        }
        return false;
    }

    MergeInstallScriptFileItemsToMap(filesArr, outMergedFiles);
    return true;
}

QJsonDocument GatewayApiClient::GetClientInstallScriptFiles(int templateId,
                                                            const QString &gameEngine,
                                                            QString *errorMessage,
                                                            int partitionId) const
{
    if (templateId <= 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("模板编号无效");
        }
        return {};
    }
    QMap<QString, QString> params{{QStringLiteral("templateId"), QString::number(templateId)}};
    const QString ge = gameEngine.trimmed();
    if (!ge.isEmpty()) {
        params.insert(QStringLiteral("gameEngine"), ge);
    }
    if (partitionId > 0) {
        params.insert(QStringLiteral("partitionId"), QString::number(partitionId));
    }
    return Get(kGetClientInstallScriptFilesApi,
               AppendSecretKey(params),
               errorMessage,
               kInstallScriptHttpTimeoutMs);
}

bool GatewayApiClient::SaveClientInstallScriptFile(int templateId,
                                                   const QString &gameEngine,
                                                   const QString &fileName,
                                                   const QString &content,
                                                   QString *errorMessage) const
{
    if (templateId <= 0 || fileName.trimmed().isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("模板或文件名无效");
        }
        return false;
    }
    QJsonObject body;
    body.insert(QStringLiteral("TemplateId"), templateId);
    body.insert(QStringLiteral("GameEngine"), gameEngine);
    body.insert(QStringLiteral("FileName"), fileName.trimmed());
    body.insert(QStringLiteral("Content"), content);
    const QString response = PostText(kSaveClientInstallScriptFileApi, AppendSecretKey(body), errorMessage);
    if (response.isEmpty()) {
        return false;
    }
    return response.contains(QStringLiteral("保存成功"));
}

QJsonArray GatewayApiClient::GetTempProductRates(int templateId, QString *errorMessage) const
{
    return Get(kTempProductRatesApi,
               AppendSecretKey(QMap<QString, QString>{{QStringLiteral("templateId"), QString::number(templateId)}}),
               errorMessage)
        .array();
}

QString GatewayApiClient::InstallClientTemplate(const QJsonObject &model, QString *errorMessage) const
{
    return PostText(kInstallClientTemplateApi, AppendSecretKey(model), errorMessage);
}

bool GatewayApiClient::EditClientTemplate(const QJsonObject &model, QString *errorMessage) const
{
    const QString response = PostText(kEditClientTemplateApi, AppendSecretKey(model), errorMessage);
    if (response.isEmpty() && errorMessage && !errorMessage->isEmpty()) {
        return false;
    }
    return IsSuccessText(response, {QStringLiteral("模板编辑成功"), QStringLiteral("操作成功")});
}

bool GatewayApiClient::DeleteClientTemplate(int templateId, QString *errorMessage) const
{
    if (templateId <= 0) {
        if (errorMessage)
            *errorMessage = QStringLiteral("模板编号无效");
        return false;
    }
    const QString response = GetText(kDelClientTemplateApi,
                                    AppendSecretKey(QMap<QString, QString>{{QStringLiteral("templateId"), QString::number(templateId)}}),
                                    errorMessage);
    return IsSuccessText(response,
                         {QStringLiteral("删除成功"),
                          QStringLiteral("操作成功"),
                          QStringLiteral("模板删除成功")});
}

QString GatewayApiClient::UpdateClientIp(QString *errorMessage) const
{
    return GetText(kUpdateIpApi, AppendSecretKey(QMap<QString, QString>{}), errorMessage);
}

bool GatewayApiClient::SyncClientMerchantIdentity(AppConfigValues &config, QString *errorMessage) const
{
    const QString restBase = NormalizeBaseUrl(config.restUrl.trimmed());
    const QString secret = config.secretKey.trimmed();
    if (restBase.isEmpty() || secret.isEmpty()) {
        if (errorMessage) {
            errorMessage->clear();
        }
        return false;
    }

    const QString url = restBase + QLatin1String(kGetClientMerchantUuidPath);
    const QJsonDocument doc = Get(url,
                                  QMap<QString, QString>{{QStringLiteral("secretKey"), secret}},
                                  errorMessage);
    if (doc.isNull() || !doc.isObject()) {
        return false;
    }

    const QJsonObject o = doc.object();
    const auto pickString = [&o](const QString &a, const QString &b) -> QString {
        QString s = o.value(a).toString().trimmed();
        if (s.isEmpty()) {
            s = o.value(b).toString().trimmed();
        }
        return s;
    };

    const QString apiUuid = pickString(QStringLiteral("Uuid"), QStringLiteral("uuid"));
    const QString apiUser = pickString(QStringLiteral("UserName"), QStringLiteral("userName"));
    const QString apiNick = pickString(QStringLiteral("NickName"), QStringLiteral("nickName"));
    const QString apiAccount = pickString(QStringLiteral("Account"), QStringLiteral("account"));
    QString displayName = pickString(QStringLiteral("DisplayName"), QStringLiteral("displayName"));
    if (displayName.isEmpty()) {
        displayName = apiNick;
    }
    if (displayName.isEmpty()) {
        displayName = apiUser;
    }
    if (displayName.isEmpty()) {
        displayName = apiAccount;
    }

    if (apiUuid.size() < 8) {
        if (errorMessage && errorMessage->isEmpty()) {
            *errorMessage = QStringLiteral("平台返回的商户 Uuid 无效");
        }
        return false;
    }

    bool changed = false;
    if (config.merchantUuid.trimmed() != apiUuid) {
        config.merchantUuid = apiUuid;
        changed = true;
    }

    // 同步成功则始终覆盖本地展示名，避免旧配置里残留错误数字等
    if (displayName.isEmpty()) {
        displayName = QStringLiteral("商户");
    }
    if (config.merchantName.trimmed() != displayName) {
        config.merchantName = displayName;
        changed = true;
    }

    if (changed) {
        AppConfig::Save(config);
    }
    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

bool GatewayApiClient::RegisterGatewayEndpoint(AppConfigValues &config, QString *errorMessage) const
{
    const QString restBase = NormalizeBaseUrl(config.restUrl.trimmed());
    const QString secret = config.secretKey.trimmed();
    if (restBase.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("未配置 RestUrl");
        }
        return false;
    }
    if (secret.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("未配置通讯密钥");
        }
        return false;
    }

    QString identityError;
    QString merchantUuid = config.merchantUuid.trimmed();
    if (merchantUuid.size() < 8) {
        (void)SyncClientMerchantIdentity(config, &identityError);
        merchantUuid = config.merchantUuid.trimmed();
    }
    if (merchantUuid.size() < 8) {
        if (errorMessage) {
            *errorMessage = identityError.isEmpty()
                ? QStringLiteral("无法根据通讯密钥获取商户 Uuid（请检查 RestUrl 与通讯密钥，或在设置中手动填写商户 Uuid）")
                : identityError;
        }
        return false;
    }

    bool ok = false;
    const int listenPort = config.port.trimmed().toInt(&ok);
    if (!ok || listenPort < 1 || listenPort > 65535) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("通讯端口无效（须为 1-65535）");
        }
        return false;
    }

    MachineCode machineCode;
    const QString deviceInstanceId = machineCode.GetRNum().trimmed();
    if (deviceInstanceId.size() < 8) {
        if (errorMessage) {
            *errorMessage = QStringLiteral(
                "无法生成设备实例标识（请检查：通讯端口为 1-65535、商户 Uuid 至少 8 位；"
                "若填写了网关对外 IP 须为有效 IPv4）");
        }
        return false;
    }

    const QString advertisedHost = MachineCode::EffectiveGatewayAdvertisedHostForConfig(config);
    QJsonObject body{{QStringLiteral("SecretKey"), secret},
                     {QStringLiteral("MerchantUuid"), merchantUuid},
                     {QStringLiteral("DeviceInstanceId"), deviceInstanceId},
                     {QStringLiteral("ListenPort"), listenPort},
                     {QStringLiteral("AdvertisedHost"), advertisedHost}};
    const QString userName = machineCode.GetCurrentUserName().trimmed();
    if (!userName.isEmpty()) {
        body.insert(QStringLiteral("MachineUserName"), userName);
    }

    const QString fullUrl = restBase + QLatin1String(kRegisterGatewayEndpointPath);
    const QString response = PostText(fullUrl, body, errorMessage);
    return IsSuccessText(response, {QStringLiteral("操作成功")});
}

namespace {
bool BuildGatewayPresenceRequest(AppConfigValues *cfg,
                                 QString *restBaseOut,
                                 QJsonObject *bodyOut,
                                 QString *errorMessage)
{
    if (!cfg || !restBaseOut || !bodyOut) {
        return false;
    }
    const QString restBase = NormalizeBaseUrl(cfg->restUrl.trimmed());
    if (restBase.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("未配置 RestUrl");
        }
        return false;
    }
    if (cfg->secretKey.trimmed().isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("未配置通讯密钥");
        }
        return false;
    }

    if (cfg->merchantUuid.trimmed().size() < 8) {
        GatewayApiClient api;
        api.SyncClientMerchantIdentity(*cfg, errorMessage);
    }
    const QString merchantUuid = cfg->merchantUuid.trimmed();
    if (merchantUuid.size() < 8) {
        if (errorMessage && errorMessage->isEmpty()) {
            *errorMessage = QStringLiteral("商户 Uuid 无效");
        }
        return false;
    }

    MachineCode machineCode;
    const QString deviceInstanceId = machineCode.GetRNum().trimmed();
    if (deviceInstanceId.size() < 8) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("无法生成设备实例标识");
        }
        return false;
    }

    *restBaseOut = restBase;
    *bodyOut = QJsonObject{{QStringLiteral("SecretKey"), cfg->secretKey.trimmed()},
                           {QStringLiteral("MerchantUuid"), merchantUuid},
                           {QStringLiteral("DeviceInstanceId"), deviceInstanceId}};
    return true;
}
} // namespace

bool GatewayApiClient::PingGatewayPresence(QString *errorMessage) const
{
    AppConfigValues cfg = AppConfig::Load();
    QString restBase;
    QJsonObject body;
    if (!BuildGatewayPresenceRequest(&cfg, &restBase, &body, errorMessage)) {
        return false;
    }
    const QString response = PostText(restBase + QLatin1String(kGatewayEndpointPingPath), body, errorMessage);
    return IsSuccessText(response, {QStringLiteral("操作成功")});
}

bool GatewayApiClient::SetGatewayEndpointOffline(QString *errorMessage) const
{
    AppConfigValues cfg = AppConfig::Load();
    QString restBase;
    QJsonObject body;
    if (!BuildGatewayPresenceRequest(&cfg, &restBase, &body, errorMessage)) {
        return false;
    }
    const QString response = PostText(restBase + QLatin1String(kGatewayEndpointOfflinePath), body, errorMessage);
    return IsSuccessText(response, {QStringLiteral("操作成功")});
}

QJsonObject GatewayApiClient::GetGatewayInstallerUpdateInfo(QString *errorMessage) const
{
    const QJsonDocument doc = Get(QStringLiteral("/api/Client/GetGatewayInstallerUpdateInfo"),
                                  AppendSecretKey(QMap<QString, QString>{}),
                                  errorMessage,
                                  kGatewayUpdateInfoTimeoutMs);
    if (!doc.isObject()) {
        if (errorMessage && errorMessage->isEmpty()) {
            *errorMessage = QStringLiteral("接口未返回有效的 JSON 对象");
        }
        return {};
    }
    return doc.object();
}

bool GatewayApiClient::IsGatewayEndpointOccupiedError(const QString &responseOrErrorText)
{
    const auto isOccupiedText = [](const QString &s) -> bool {
        return s.contains(QStringLiteral("已被当前商户下其他")) && s.contains(QStringLiteral("网关实例占用"));
    };
    const QString t = responseOrErrorText.trimmed();
    if (isOccupiedText(t)) {
        return true;
    }
    QString parseDetail;
    const QJsonDocument doc = ParseJsonDocumentRelaxed(t, &parseDetail);
    if (!doc.isObject()) {
        return false;
    }
    const QString msg = doc.object().value(QStringLiteral("message")).toString();
    return isOccupiedText(msg);
}

QJsonDocument GatewayApiClient::GetOrders(const QJsonObject &query, QString *errorMessage) const
{
    return Post(kOrdersApi, AppendSecretKey(query, true), errorMessage);
}

QString GatewayApiClient::InstallClientPartition(const QJsonObject &model, QString *errorMessage) const
{
    return PostText(kInstallPartitionApi, AppendSecretKey(model), errorMessage);
}

bool GatewayApiClient::UpdateClientPartition(const QJsonObject &model, QString *errorMessage) const
{
    const QString response = PostText(kUpdatePartitionApi, AppendSecretKey(model), errorMessage);
    // 与 TenantServer ClientController.UpdateClientPartition 返回值一致（含定时开区、下发安装脚本）
    return IsSuccessText(response,
                         {QStringLiteral("更新分区成功"),
                          QStringLiteral("操作成功"),
                          QStringLiteral("定时安装分区指令发送成功"),
                          QStringLiteral("安装分区指令发送成功")});
}

bool GatewayApiClient::DeletePartition(int partitionId, QString *errorMessage) const
{
    const QString response = GetText(kDeletePartitionApi,
                                     AppendSecretKey(QMap<QString, QString>{{QStringLiteral("partitionId"), QString::number(partitionId)}}),
                                     errorMessage);
    return IsSuccessText(response, {QStringLiteral("删除成功"), QStringLiteral("操作成功")});
}

bool GatewayApiClient::LoadClientPartition(const QJsonObject &model, QString *errorMessage) const
{
    const QString response = PostText(kLoadPartitionApi, AppendSecretKey(model), errorMessage);
    return IsSuccessText(response, {QStringLiteral("操作成功"), QStringLiteral("加载分区指令发送成功")});
}

bool GatewayApiClient::UpdateOrder(const QJsonObject &model, QString *errorMessage) const
{
    const QString response = PostText(kUpdateOrderApi, AppendSecretKey(model), errorMessage);
    return IsSuccessText(response, {QStringLiteral("操作成功")});
}

bool GatewayApiClient::PaidApply(const QJsonObject &model, QString *errorMessage) const
{
    const QString response = PostText(kPaidApplyApi, AppendSecretKey(model, true), errorMessage);
    return IsSuccessText(response, {QStringLiteral("操作成功")});
}

bool GatewayApiClient::WxValidProcess(const QJsonObject &model, QString *errorMessage) const
{
    const QString response = PostText(kWxValidProcessApi, AppendSecretKey(model, true), errorMessage);
    return IsSuccessText(response, {QStringLiteral("操作成功")});
}

QString GatewayApiClient::GetRechargeFileDownloadUrl(const QString &groupUuid) const
{
    if (groupUuid.trimmed().isEmpty()) {
        return {};
    }

    QUrl url(BuildUrl(kCreateRechargeFileApi));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("group"), groupUuid.trimmed());
    url.setQuery(query);
    return url.toString();
}

bool GatewayApiClient::DownloadRechargeFile(const QString &downloadUrl,
                                            const QString &savePath,
                                            QString *errorMessage) const
{
    QString responseText;
    if (!ExecuteTextRequest(downloadUrl.trimmed(), QStringLiteral("GET"), {}, &responseText, errorMessage)) {
        return false;
    }

    QFile file(savePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("无法写入文件：%1").arg(savePath);
        }
        return false;
    }

    file.write(responseText.toUtf8());
    file.close();
    return true;
}

QString GatewayApiClient::BuildUrl(const QString &apiPath) const
{
    if (apiPath.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive)
        || apiPath.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive)) {
        return apiPath;
    }

    const QString normalizedApiPath = apiPath.startsWith(QLatin1Char('/')) ? apiPath : QStringLiteral("/") + apiPath;
    return m_baseDomain + normalizedApiPath;
}

QMap<QString, QString> GatewayApiClient::AppendSecretKey(const QMap<QString, QString> &parameters,
                                                         bool includeMachineCode) const
{
    QMap<QString, QString> merged = parameters;
    if (!m_secretKey.isEmpty()) {
        merged.insert(QStringLiteral("secretKey"), m_secretKey);
    }
    if (includeMachineCode && !m_machineCode.isEmpty()) {
        merged.insert(QStringLiteral("machineCode"), m_machineCode);
    }
    return merged;
}

QJsonObject GatewayApiClient::AppendSecretKey(const QJsonObject &body,
                                              bool includeMachineCode) const
{
    QJsonObject merged = body;
    if (!m_secretKey.isEmpty()) {
        merged.insert(QStringLiteral("SecretKey"), m_secretKey);
    }
    if (includeMachineCode && !m_machineCode.isEmpty()) {
        merged.insert(QStringLiteral("MachineCode"), m_machineCode);
    }
    return merged;
}

bool GatewayApiClient::ExecuteTextRequest(const QString &url,
                                          const QString &method,
                                          const QJsonObject &body,
                                          QString *responseText,
                                          QString *errorMessage,
                                          int timeoutMs) const
{
    QNetworkAccessManager manager;
    QNetworkRequest request{QUrl(url)};

    QNetworkReply *reply = nullptr;
    if (method.compare(QStringLiteral("GET"), Qt::CaseInsensitive) == 0) {
        QUrl requestUrl(url);
        QUrlQuery query(requestUrl);
        for (auto it = body.begin(); it != body.end(); ++it) {
            query.addQueryItem(it.key(), it.value().toVariant().toString());
        }
        requestUrl.setQuery(query);
        request.setUrl(requestUrl);
        reply = manager.get(request);
    } else {
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        reply = manager.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    }

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);

    QObject::connect(&timer, &QTimer::timeout, &loop, [&] {
        if (errorMessage) {
            *errorMessage = QStringLiteral("HTTP 请求超时：%1").arg(url);
        }
        if (reply && reply->isRunning()) {
            reply->abort();
        }
        loop.quit();
    });
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);

    const int effectiveTimeout = timeoutMs > 0 ? timeoutMs : kRequestTimeoutMs;
    timer.start(effectiveTimeout);
    loop.exec();
    timer.stop();

    const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const bool success = reply->error() == QNetworkReply::NoError && statusCode >= 200 && statusCode < 300;
    const QString response = QString::fromUtf8(reply->readAll());

    if (!success) {
        QString message = !response.trimmed().isEmpty()
                              ? response.trimmed()
                              : (statusCode > 0
                                     ? QStringLiteral("HTTP 状态码：%1").arg(statusCode)
                                     : reply->errorString());
        if (statusCode == 404) {
            message = QStringLiteral("%1\n\n请求地址：%2\n\n若为新网关登记接口：请确认 RestUrl 为 TenantServer 根地址（与其它 Client 接口相同），且线上已发布包含 GatewayEndpoint/Register、GetClientMerchantUuid 的 TenantServer 版本。")
                          .arg(message, url);
        }
        if (errorMessage) {
            *errorMessage = message;
        }
        if (!GatewayApiClient::IsGatewayEndpointOccupiedError(message)) {
            AppLogger::WriteLog(QStringLiteral("接口请求失败：%1 %2").arg(url, message));
        }
    } else if (responseText) {
        *responseText = response;
    }

    reply->deleteLater();
    return success;
}
