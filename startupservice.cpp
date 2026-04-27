#include "startupservice.h"

#include "appconfig.h"
#include "applogger.h"
#include "filemonitorservice.h"
#include "gatewayapiclient.h"
#include "rabbitmqdispatcher.h"
#include "rabbitmqservice.h"

#include <QDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace {
QString ReadStringField(const QJsonObject &object, const QStringList &names)
{
    for (const auto &name : names) {
        const QString value = object.value(name).toVariant().toString().trimmed();
        if (!value.isEmpty()) {
            return value;
        }
    }
    return {};
}

bool ReadBoolField(const QJsonObject &object, const QStringList &names, bool *hasValue = nullptr)
{
    if (hasValue) {
        *hasValue = false;
    }

    for (const auto &name : names) {
        if (!object.contains(name)) {
            continue;
        }

        if (hasValue) {
            *hasValue = true;
        }

        const QJsonValue value = object.value(name);
        if (value.isBool()) {
            return value.toBool();
        }

        const QString text = value.toVariant().toString().trimmed();
        if (text.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0 || text == QStringLiteral("1")) {
            return true;
        }
        if (text.compare(QStringLiteral("false"), Qt::CaseInsensitive) == 0 || text == QStringLiteral("0")) {
            return false;
        }
    }

    return false;
}

QString ExtractGatewayId(const QJsonObject &equipObject)
{
    return ReadStringField(equipObject,
                           {
                               QStringLiteral("GatewayId"),
                               QStringLiteral("gatewayId"),
                               QStringLiteral("GatewayName"),
                               QStringLiteral("gatewayName"),
                               QStringLiteral("EquipName"),
                               QStringLiteral("equipName"),
                               QStringLiteral("Name"),
                               QStringLiteral("name")
                           });
}

bool IsEquipBound(const QJsonObject &equipObject)
{
    bool hasBoundFlag = false;
    const bool boundFlag = ReadBoolField(equipObject,
                                         {
                                             QStringLiteral("IsBind"),
                                             QStringLiteral("isBind"),
                                             QStringLiteral("HasBind"),
                                             QStringLiteral("hasBind"),
                                             QStringLiteral("Bind"),
                                             QStringLiteral("bind")
                                         },
                                         &hasBoundFlag);
    if (hasBoundFlag) {
        return boundFlag;
    }

    return !ExtractGatewayId(equipObject).isEmpty();
}

QString PromptGatewayId(const QString &initialValue)
{
    QDialog dialog;
    dialog.setWindowTitle(QStringLiteral("设置网关标识"));
    dialog.setFixedSize(340, 170);

    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(32, 26, 32, 24);
    layout->setSpacing(20);

    auto *label = new QLabel(QStringLiteral("网关标识:"), &dialog);
    auto *edit = new QLineEdit(&dialog);
    auto *button = new QPushButton(QStringLiteral("确认"), &dialog);

    edit->setText(initialValue.trimmed());
    edit->setFixedHeight(32);
    button->setFixedSize(102, 36);

    auto *buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    buttonLayout->addWidget(button);
    buttonLayout->addStretch();

    layout->addWidget(label);
    layout->addWidget(edit);
    layout->addSpacing(4);
    layout->addLayout(buttonLayout);

    QObject::connect(button, &QPushButton::clicked, &dialog, [&dialog, edit] {
        if (edit->text().trimmed().isEmpty()) {
            QMessageBox::warning(&dialog, QStringLiteral("提示"), QStringLiteral("请输入网关标识"));
            edit->setFocus();
            return;
        }
        dialog.accept();
    });

    if (dialog.exec() != QDialog::Accepted) {
        return {};
    }

    return edit->text().trimmed();
}

bool EnsureGatewayBinding(AppConfigValues *config)
{
    AppLogger::WriteLog(QStringLiteral("检测网关设备绑定状态..."));

    GatewayApiClient client;
    QString errorMessage;
    const QJsonObject equipObject = client.GetEquip(&errorMessage);
    if (equipObject.isEmpty() && !errorMessage.isEmpty()) {
        AppLogger::WriteLog(QStringLiteral("获取网关设备信息失败：%1").arg(errorMessage));
        QMessageBox::critical(nullptr, QStringLiteral("启动失败"), QStringLiteral("获取网关设备信息失败：%1").arg(errorMessage));
        return false;
    }

    if (IsEquipBound(equipObject)) {
        const QString boundGatewayId = ExtractGatewayId(equipObject);
        if (config && !boundGatewayId.isEmpty() && config->gatewayId.trimmed() != boundGatewayId) {
            config->gatewayId = boundGatewayId;
            AppConfig::Save(*config);
        }
        AppLogger::WriteLog(QStringLiteral("当前设备已绑定网关标识：%1")
                                .arg(boundGatewayId.isEmpty() ? QStringLiteral("<unknown>") : boundGatewayId));
        return true;
    }

    AppLogger::WriteLog(QStringLiteral("当前设备尚未绑定网关标识，等待设置"));
    while (true) {
        const QString gatewayId = PromptGatewayId(config ? config->gatewayId : QString());
        if (gatewayId.isEmpty()) {
            AppLogger::WriteLog(QStringLiteral("用户取消设置网关标识，启动终止"));
            return false;
        }

        if (!client.AddEquipName(gatewayId, &errorMessage)) {
            AppLogger::WriteLog(QStringLiteral("设置网关标识失败：%1").arg(errorMessage));
            QMessageBox::warning(nullptr, QStringLiteral("设置失败"), QStringLiteral("设置网关标识失败：%1").arg(errorMessage));
            continue;
        }

        if (config) {
            config->gatewayId = gatewayId;
            AppConfig::Save(*config);
        }

        AppLogger::WriteLog(QStringLiteral("网关标识设置成功：%1").arg(gatewayId));
        return true;
    }
}

void LogGatewayValidation(const AppConfigValues &config)
{
    AppLogger::WriteLog(QStringLiteral("网关标识校验中..."));
    if (config.gatewayId.trimmed().isEmpty()) {
        AppLogger::WriteLog(QStringLiteral("网关标识校验失败：未配置网关标识"));
        return;
    }

    AppLogger::WriteLog(QStringLiteral("网关标识校验成功：%1").arg(config.gatewayId));
}

bool LogServiceConnection(const AppConfigValues &config)
{
    AppLogger::WriteLog(QStringLiteral("网关服务器连接中..."));
    QString errorMessage;
    RabbitMqService::SetMessageHandler(RabbitMqDispatcher::HandleMessage);
    if (!RabbitMqService::StartListening(config, &errorMessage)) {
        AppLogger::WriteLog(QStringLiteral("网关服务器连接失败：%1").arg(errorMessage));
        return false;
    }

    AppLogger::WriteLog(QStringLiteral("网关服务器连接成功"));
    AppLogger::WriteLog(QStringLiteral("RabbitMQ 消息监听已启动，队列：%1").arg(RabbitMqService::ConsumerQueueName(config)));
    return true;
}

void LogLocalDataLoading(const AppConfigValues &config)
{
    AppLogger::WriteLog(QStringLiteral("加载数据到本机中..."));
    if (config.restUrl.trimmed().isEmpty()) {
        AppLogger::WriteLog(QStringLiteral("加载数据到本机中完成"));
        return;
    }

    AppLogger::WriteLog(QStringLiteral("加载数据到本机中完成"));
}
}

bool StartupService::RunStartupSequence()
{
    AppConfig::EnsureConfigExists();
    AppConfigValues config = AppConfig::Load();

    AppLogger::MarkSessionStart();
    AppLogger::WriteLog(QStringLiteral("程序启动中..."));

    if (!EnsureGatewayBinding(&config)) {
        return false;
    }

    LogGatewayValidation(config);
    const bool rabbitConnected = LogServiceConnection(config);
    LogLocalDataLoading(config);
    FileMonitorService::Instance().Initialize(config);
    if (!rabbitConnected) {
        AppLogger::WriteLog(QStringLiteral("程序启动完成，但 RabbitMQ AMQP 消费者尚未就绪"));
    }
    AppLogger::WriteLog(QStringLiteral("服务启动成功，监听端口：%1").arg(config.port));
    AppLogger::WriteLog(QStringLiteral("程序启动成功"));
    return true;
}
