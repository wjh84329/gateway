#ifndef APPCONFIG_H
#define APPCONFIG_H

#include <QString>

struct AppConfigValues
{
    QString merchantName;
    QString gatewayId;
    QString website;
    QString yxsmDir;
    QString wxValidPath;
    QString paidDir;
    QString restUrl;
    QString port;
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
};

namespace AppConfig {
QString ConfigFilePath();
void EnsureConfigExists();
AppConfigValues Load();
void Save(const AppConfigValues &values);
bool IsAutoStartEnabled();
void SetAutoStartEnabled(bool enabled);
}

#endif // APPCONFIG_H
