#include "appconfig.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>

namespace {
QString NormalizeMonitorRootPath(QString path)
{
    path = QDir::fromNativeSeparators(path.trimmed());
    if (path.isEmpty()) {
        return {};
    }

    const QString absolutePath = QFileInfo(path).absoluteFilePath();
    if (absolutePath.size() >= 3 && absolutePath.at(1) == QLatin1Char(':')
        && (absolutePath.at(2) == QLatin1Char('/') || absolutePath.at(2) == QLatin1Char('\\'))) {
        return QDir::toNativeSeparators(absolutePath.left(3));
    }

    return QDir::toNativeSeparators(QDir(absolutePath).rootPath());
}

QString NormalizeMonitorRootPathList(const QString &paths)
{
    QStringList normalizedPaths;
    for (const auto &part : paths.split(QLatin1Char('|'), Qt::SkipEmptyParts)) {
        const QString normalizedPath = NormalizeMonitorRootPath(part);
        if (!normalizedPath.isEmpty() && !normalizedPaths.contains(normalizedPath, Qt::CaseInsensitive)) {
            normalizedPaths.append(normalizedPath);
        }
    }
    return normalizedPaths.join(QLatin1Char('|'));
}

QString ConfigDirectoryPath()
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("Libs"));
}

void WriteDefaultConfig(QSettings &settings)
{
    settings.beginGroup(QStringLiteral("ConnectionStrings"));
    settings.setValue(QStringLiteral("Socket"), QStringLiteral("ZX4k0rGlJDXMCDWqeam1Ie/KOx8yG7QHLzG45p3v5EswGjizVd8kjp/ST9TGez5m69OlSOyMRtYHt74svujSJA=="));
    settings.endGroup();

    settings.beginGroup(QStringLiteral("AppSettings"));
    settings.setValue(QStringLiteral("MerchantName"), QStringLiteral("Tenant"));
    settings.setValue(QStringLiteral("GatewayId"), QStringLiteral("1122"));
    settings.setValue(QStringLiteral("IsOpenLog"), true);
    settings.setValue(QStringLiteral("IsOpenOrderReissue"), true);
    settings.setValue(QStringLiteral("IsSm"), false);
    settings.setValue(QStringLiteral("BootUp"), false);
    settings.setValue(QStringLiteral("Domain"), QStringLiteral("http://new.7xpay.com/"));
    settings.setValue(QStringLiteral("PayWebDomain"), QStringLiteral("http://ww5.7xpay.com/"));
    settings.setValue(QStringLiteral("RestUrl"), QStringLiteral("http://newapi.7xpay.com/"));
    settings.setValue(QStringLiteral("RabbitMqHost"), QStringLiteral("127.0.0.1"));
    settings.setValue(QStringLiteral("RabbitMqAmqpPort"), 5672);
    settings.setValue(QStringLiteral("RabbitMqManagementPort"), 15672);
    settings.setValue(QStringLiteral("RabbitMqVirtualHost"), QStringLiteral("/"));
    settings.setValue(QStringLiteral("RabbitMqQueue"), QStringLiteral("gateway.queue"));
    settings.setValue(QStringLiteral("RabbitMqUser"), QStringLiteral("admin"));
    settings.setValue(QStringLiteral("RabbitMqPassword"), QStringLiteral("admin"));
    settings.setValue(QStringLiteral("SecretKey"), QStringLiteral("2875a52f071c43d19f6d1289070d3d1a"));
    settings.setValue(QStringLiteral("SignKey"), QStringLiteral("2875a52f071c43d19f6d1289070d3d1a"));
    settings.setValue(QStringLiteral("IsWeixinMb"), false);
    settings.setValue(QStringLiteral("IsWeixinZq"), false);
    settings.setValue(QStringLiteral("InstallStartPath"), QStringLiteral("D:/"));
    settings.setValue(QStringLiteral("WxValidPath"), QStringLiteral("D:/"));
    settings.setValue(QStringLiteral("YxsmDir"), QStringLiteral(""));
    settings.setValue(QStringLiteral("PaidDir"), QStringLiteral("D:/aaa.txt"));
    settings.setValue(QStringLiteral("Port"), QStringLiteral("9527"));
    settings.setValue(QStringLiteral("SqlConnectionStr"), QStringLiteral("Data Source=.;Initial Catalog=MuOnline18;Integrated Security=True"));
    settings.endGroup();
}

#ifdef Q_OS_WIN
QString AutoStartEntryName()
{
    const QString applicationName = QCoreApplication::applicationName().trimmed();
    if (!applicationName.isEmpty()) {
        return applicationName;
    }

    return QFileInfo(QCoreApplication::applicationFilePath()).completeBaseName();
}

QString AutoStartCommand()
{
    return QStringLiteral("\"") + QDir::toNativeSeparators(QCoreApplication::applicationFilePath()) + QStringLiteral("\"");
}
#endif
}

QString AppConfig::ConfigFilePath()
{
    return QDir(ConfigDirectoryPath()).filePath(QStringLiteral("setting.config"));
}

void AppConfig::EnsureConfigExists()
{
    if (QFileInfo::exists(ConfigFilePath())) {
        return;
    }

    QDir().mkpath(ConfigDirectoryPath());

    QSettings settings(ConfigFilePath(), QSettings::IniFormat);
    WriteDefaultConfig(settings);
    settings.sync();
}

AppConfigValues AppConfig::Load()
{
    EnsureConfigExists();

    QSettings settings(ConfigFilePath(), QSettings::IniFormat);
    settings.beginGroup(QStringLiteral("AppSettings"));

    AppConfigValues values;
    values.merchantName = settings.value(QStringLiteral("MerchantName"), QStringLiteral("Tenant")).toString();
    values.gatewayId = settings.value(QStringLiteral("GatewayId"), QStringLiteral("1122")).toString();
    values.website = settings.value(QStringLiteral("Domain"), QStringLiteral("http://new.7xpay.com/")).toString();
    values.installStartPath = settings.value(QStringLiteral("InstallStartPath"), QStringLiteral("D:/")).toString();
    values.yxsmDir = NormalizeMonitorRootPathList(settings.value(QStringLiteral("YxsmDir"), QStringLiteral("")).toString());
    values.wxValidPath = NormalizeMonitorRootPathList(settings.value(QStringLiteral("WxValidPath"), QStringLiteral("D:/")).toString());
    values.paidDir = settings.value(QStringLiteral("PaidDir"), QStringLiteral("D:/aaa.txt")).toString();
    values.restUrl = settings.value(QStringLiteral("RestUrl"), QStringLiteral("http://newapi.7xpay.com/")).toString();
    values.port = settings.value(QStringLiteral("Port"), QStringLiteral("9527")).toString();
    values.rabbitMqHost = settings.value(QStringLiteral("RabbitMqHost"), QStringLiteral("127.0.0.1")).toString();
    values.rabbitMqAmqpPort = settings.value(QStringLiteral("RabbitMqAmqpPort"), 5672).toInt();
    values.rabbitMqManagementPort = settings.value(QStringLiteral("RabbitMqManagementPort"), 15672).toInt();
    values.rabbitMqVirtualHost = settings.value(QStringLiteral("RabbitMqVirtualHost"), QStringLiteral("/")).toString();
    values.rabbitMqQueue = settings.value(QStringLiteral("RabbitMqQueue"), QStringLiteral("gateway.queue")).toString();
    values.rabbitMqUser = settings.value(QStringLiteral("RabbitMqUser"), QStringLiteral("admin")).toString();
    values.rabbitMqPassword = settings.value(QStringLiteral("RabbitMqPassword"), QStringLiteral("admin")).toString();
    values.sqlConnectionStr = settings.value(QStringLiteral("SqlConnectionStr"), QStringLiteral("Data Source=.;Initial Catalog=MuOnline18;Integrated Security=True")).toString();
    values.secretKey = settings.value(QStringLiteral("SecretKey")).toString();
    values.signKey = settings.value(QStringLiteral("SignKey")).toString();
    values.isOpenLog = settings.value(QStringLiteral("IsOpenLog"), true).toBool();
    values.isOpenOrderReissue = settings.value(QStringLiteral("IsOpenOrderReissue"), true).toBool();
    values.isWeixinZq = settings.value(QStringLiteral("IsWeixinZq"), false).toBool();
    values.isWeixinMb = settings.value(QStringLiteral("IsWeixinMb"), false).toBool();
    values.bootUp = settings.value(QStringLiteral("BootUp"), false).toBool();
    values.isSm = settings.value(QStringLiteral("IsSm"), false).toBool();

    settings.endGroup();

    settings.beginGroup(QStringLiteral("ConnectionStrings"));
    values.socketConnectionString = settings.value(QStringLiteral("Socket")).toString();
    settings.endGroup();

    return values;
}

void AppConfig::Save(const AppConfigValues &values)
{
    EnsureConfigExists();

    QSettings settings(ConfigFilePath(), QSettings::IniFormat);
    settings.beginGroup(QStringLiteral("AppSettings"));
    settings.setValue(QStringLiteral("MerchantName"), values.merchantName);
    settings.setValue(QStringLiteral("GatewayId"), values.gatewayId);
    settings.setValue(QStringLiteral("Domain"), values.website);
    settings.setValue(QStringLiteral("YxsmDir"), NormalizeMonitorRootPathList(values.yxsmDir));
    settings.setValue(QStringLiteral("WxValidPath"), NormalizeMonitorRootPathList(values.wxValidPath));
    settings.setValue(QStringLiteral("PaidDir"), values.paidDir);
    settings.setValue(QStringLiteral("Port"), values.port);
    settings.setValue(QStringLiteral("SqlConnectionStr"), values.sqlConnectionStr);
    settings.setValue(QStringLiteral("SecretKey"), values.secretKey);
    settings.setValue(QStringLiteral("SignKey"), values.signKey);
    settings.setValue(QStringLiteral("IsOpenLog"), values.isOpenLog);
    settings.setValue(QStringLiteral("IsOpenOrderReissue"), values.isOpenOrderReissue);
    settings.setValue(QStringLiteral("IsWeixinZq"), values.isWeixinZq);
    settings.setValue(QStringLiteral("IsWeixinMb"), values.isWeixinMb);
    settings.setValue(QStringLiteral("BootUp"), values.bootUp);
    settings.setValue(QStringLiteral("IsSm"), values.isSm);
    settings.endGroup();
    settings.sync();
}

bool AppConfig::IsAutoStartEnabled()
{
#ifdef Q_OS_WIN
    QSettings settings(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"), QSettings::NativeFormat);
    return settings.value(AutoStartEntryName()).toString() == AutoStartCommand();
#else
    return Load().bootUp;
#endif
}

void AppConfig::SetAutoStartEnabled(bool enabled)
{
#ifdef Q_OS_WIN
    QSettings settings(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"), QSettings::NativeFormat);
    if (enabled) {
        settings.setValue(AutoStartEntryName(), AutoStartCommand());
    } else {
        settings.remove(AutoStartEntryName());
    }
#else
    Q_UNUSED(enabled);
#endif
}
