#ifndef RABBITMQSERVICE_H
#define RABBITMQSERVICE_H

#include "appconfig.h"

#include <functional>
#include <QString>

namespace RabbitMqService {
using MessageHandler = std::function<bool(const QString &)>;

QString ConsumerQueueName(const AppConfigValues &config);
void SetMessageHandler(MessageHandler handler);
bool TestConnection(const AppConfigValues &config, QString *errorMessage = nullptr);
bool StartListening(const AppConfigValues &config, QString *errorMessage = nullptr);
bool Publish(const AppConfigValues &config,
             const QString &exchange,
             const QString &messageType,
             const QString &payload,
             QString *errorMessage = nullptr);
bool UpdateOrder(const AppConfigValues &config, const QString &payload, QString *errorMessage = nullptr);
bool PublishReissueResult(const AppConfigValues &config, const QString &payload, QString *errorMessage = nullptr);
bool UpdateOrderAccount(const AppConfigValues &config, const QString &payload, QString *errorMessage = nullptr);
bool UpdateCheck(const AppConfigValues &config, const QString &payload, QString *errorMessage = nullptr);
bool PaidApply(const AppConfigValues &config, const QString &payload, QString *errorMessage = nullptr);
bool YouXiSaomaProcess(const AppConfigValues &config, const QString &payload, QString *errorMessage = nullptr);
bool WxValidProcess(const AppConfigValues &config, const QString &payload, QString *errorMessage = nullptr);
bool OperationLogProcess(const AppConfigValues &config, const QString &payload, QString *errorMessage = nullptr);
bool RanchResult(const AppConfigValues &config, const QString &payload, QString *errorMessage = nullptr);
bool RanchPTResult(const AppConfigValues &config, const QString &payload, QString *errorMessage = nullptr);
bool OperationTransferProcess(const AppConfigValues &config, const QString &payload, QString *errorMessage = nullptr);
bool VxCodeEventProcess(const AppConfigValues &config, const QString &payload, QString *errorMessage = nullptr);
void StopListening();
}

#endif // RABBITMQSERVICE_H
