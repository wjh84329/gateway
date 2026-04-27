#ifndef GATEWAYAPICLIENT_H
#define GATEWAYAPICLIENT_H

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QString>

class GatewayApiClient
{
public:
    GatewayApiClient();

    QJsonDocument Get(const QString &apiPath,
                      const QMap<QString, QString> &parameters = {},
                      QString *errorMessage = nullptr) const;
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
    QJsonArray GetProducts(QString *errorMessage = nullptr) const;
    QJsonArray GetEngines(QString *errorMessage = nullptr) const;
    QJsonObject GetEquip(QString *errorMessage = nullptr) const;
    QString UpdateClientIp(QString *errorMessage = nullptr) const;

    QJsonDocument GetOrders(const QJsonObject &query, QString *errorMessage = nullptr) const;

    QString InstallClientPartition(const QJsonObject &model, QString *errorMessage = nullptr) const;
    bool UpdateClientPartition(const QJsonObject &model, QString *errorMessage = nullptr) const;
    bool DeletePartition(int partitionId, QString *errorMessage = nullptr) const;
    bool LoadClientPartition(const QJsonObject &model, QString *errorMessage = nullptr) const;
    bool UpdateOrder(const QJsonObject &model, QString *errorMessage = nullptr) const;
    bool PaidApply(const QJsonObject &model, QString *errorMessage = nullptr) const;
    bool WxValidProcess(const QJsonObject &model, QString *errorMessage = nullptr) const;
    bool AddEquipName(const QString &gatewayId, QString *errorMessage = nullptr) const;

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
                            QString *errorMessage) const;

    QString m_baseDomain;
    QString m_secretKey;
    QString m_machineCode;
};

#endif // GATEWAYAPICLIENT_H
