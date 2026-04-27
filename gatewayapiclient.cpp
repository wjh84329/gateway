#include "gatewayapiclient.h"

#include "appconfig.h"
#include "applogger.h"
#include "machinecode.h"

#include <QEventLoop>
#include <QFile>
#include <QJsonParseError>
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
constexpr auto kGroupApi = "/api/Client/GetClientGroups";
constexpr auto kOrdersApi = "/api/Client/GetClientOrders";
constexpr auto kInstallPartitionApi = "/api/Client/InstallClientPartition";
constexpr auto kUpdatePartitionApi = "/api/Client/UpdateClientPartition";
constexpr auto kDeletePartitionApi = "/api/Client/DelClientPartition";
constexpr auto kProductApi = "/api/Recharge/GetProducts";
constexpr auto kEngineListApi = "/api/Client/GetEngineList";
constexpr auto kLoadPartitionApi = "/api/Client/ClientLoadPartition";
constexpr auto kUpdateOrderApi = "/api/Client/UpdateOrder";
constexpr auto kPaidApplyApi = "/api/Client/PaidApply";
constexpr auto kWxValidProcessApi = "/api/Client/WxValidProcess";
constexpr auto kCreateRechargeFileApi = "/api/Client/CreateRechargeFileAsync";
constexpr auto kGetEquipApi = "/api/Client/GetEquip";
constexpr auto kAddEquipNameApi = "/api/Client/AddEquipName";
constexpr auto kUpdateIpApi = "/api/Client/UpdateClientIp";

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

QJsonDocument GatewayApiClient::Get(const QString &apiPath,
                                    const QMap<QString, QString> &parameters,
                                    QString *errorMessage) const
{
    QString responseText;
    if (!ExecuteTextRequest(BuildUrl(apiPath), QStringLiteral("GET"), ToJsonObject(parameters), &responseText, errorMessage)) {
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

QJsonArray GatewayApiClient::GetProducts(QString *errorMessage) const
{
    return Get(kProductApi, {}, errorMessage).array();
}

QJsonArray GatewayApiClient::GetEngines(QString *errorMessage) const
{
    return Get(kEngineListApi, {}, errorMessage).array();
}

QJsonObject GatewayApiClient::GetEquip(QString *errorMessage) const
{
    const QJsonDocument document = Get(kGetEquipApi, AppendSecretKey(QMap<QString, QString>{}, true), errorMessage);
    if (document.isObject()) {
        return document.object();
    }

    if (document.isArray() && !document.array().isEmpty() && document.array().first().isObject()) {
        return document.array().first().toObject();
    }

    return {};
}

QString GatewayApiClient::UpdateClientIp(QString *errorMessage) const
{
    return GetText(kUpdateIpApi, AppendSecretKey(QMap<QString, QString>{}), errorMessage);
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
    return IsSuccessText(response, {QStringLiteral("更新分区成功"), QStringLiteral("操作成功")});
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

bool GatewayApiClient::AddEquipName(const QString &gatewayId, QString *errorMessage) const
{
    const QString trimmedGatewayId = gatewayId.trimmed();
    if (trimmedGatewayId.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("网关标识不能为空");
        }
        return false;
    }

    MachineCode machineCode;
    QString ipErrorMessage;
    const QString ip = UpdateClientIp(&ipErrorMessage);
    if (ip.isEmpty() && !ipErrorMessage.isEmpty()) {
        AppLogger::WriteLog(QStringLiteral("更新客户端 IP 失败：%1").arg(ipErrorMessage));
    }

    const QJsonObject body{
        {QStringLiteral("MachineCode"), m_machineCode},
        {QStringLiteral("GatewayId"), trimmedGatewayId},
        {QStringLiteral("GatewayName"), trimmedGatewayId},
        {QStringLiteral("EquipName"), trimmedGatewayId},
        {QStringLiteral("Name"), trimmedGatewayId},
        {QStringLiteral("MachineUserName"), machineCode.GetCurrentUserName()},
        {QStringLiteral("Ip"), ip}
    };
    const QString response = PostText(kAddEquipNameApi, AppendSecretKey(body, true), errorMessage);
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
                                          QString *errorMessage) const
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

    timer.start(kRequestTimeoutMs);
    loop.exec();
    timer.stop();

    const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const bool success = reply->error() == QNetworkReply::NoError && statusCode >= 200 && statusCode < 300;
    const QString response = QString::fromUtf8(reply->readAll());

    if (!success) {
        const QString message = !response.trimmed().isEmpty()
                                    ? response.trimmed()
                                    : (statusCode > 0
                                           ? QStringLiteral("HTTP 状态码：%1").arg(statusCode)
                                           : reply->errorString());
        if (errorMessage) {
            *errorMessage = message;
        }
        AppLogger::WriteLog(QStringLiteral("接口请求失败：%1 %2").arg(url, message));
    } else if (responseText) {
        *responseText = response;
    }

    reply->deleteLater();
    return success;
}
