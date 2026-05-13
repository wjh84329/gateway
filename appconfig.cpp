#include "appconfig.h"
#include "applogger.h"
#include "gatewayapiclient.h"
#include "machinecode.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
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
    settings.setValue(QStringLiteral("MerchantUuid"), QStringLiteral(""));
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
    settings.setValue(QStringLiteral("PaidDir"), QStringLiteral(""));
    settings.setValue(QStringLiteral("Port"), QStringLiteral("9527"));
    settings.setValue(QStringLiteral("GatewayAdvertisedIp"), QStringLiteral(""));
    settings.setValue(QStringLiteral("GatewayAdvertisedIpAutoDetect"), true);
    settings.setValue(QStringLiteral("SqlConnectionStr"), QStringLiteral("Data Source=.;Initial Catalog=MuOnline18;Integrated Security=True"));
    settings.setValue(QStringLiteral("UpdateManifestUrl"), QStringLiteral(""));
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

bool AppConfig::MergeIniAddMissingKeys(const QString &userPath, const QString &packagePath, const QString &destPath, QString *errorMessage)
{
    if (!QFileInfo::exists(userPath)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("本机配置不存在：%1").arg(userPath);
        }
        return false;
    }
    if (!QFileInfo::exists(packagePath)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("安装包配置不存在：%1").arg(packagePath);
        }
        return false;
    }

    QSettings user(userPath, QSettings::IniFormat);
    QSettings pkg(packagePath, QSettings::IniFormat);

    QFile::remove(destPath);
    QSettings out(destPath, QSettings::IniFormat);

    const QStringList userKeys = user.allKeys();
    for (const QString &k : userKeys) {
        out.setValue(k, user.value(k));
    }
    const QStringList pkgKeys = pkg.allKeys();
    for (const QString &k : pkgKeys) {
        if (!user.contains(k)) {
            out.setValue(k, pkg.value(k));
        }
    }
    out.sync();

    if (out.status() != QSettings::NoError) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("写入合并配置失败：%1").arg(destPath);
        }
        QFile::remove(destPath);
        return false;
    }
    return true;
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
    values.merchantUuid = settings.value(QStringLiteral("MerchantUuid"), QStringLiteral("")).toString();
    values.website = settings.value(QStringLiteral("Domain"), QStringLiteral("http://new.7xpay.com/")).toString();
    values.installStartPath = settings.value(QStringLiteral("InstallStartPath"), QStringLiteral("D:/")).toString();
    values.yxsmDir = NormalizeMonitorRootPathList(settings.value(QStringLiteral("YxsmDir"), QStringLiteral("")).toString());
    values.yxsmDirVolumeRootsSeeded = settings.value(QStringLiteral("YxsmDirVolumeRootsSeeded"), false).toBool();
    values.wxValidPath = NormalizeMonitorRootPathList(settings.value(QStringLiteral("WxValidPath"), QStringLiteral("D:/")).toString());
    values.paidDir = settings.value(QStringLiteral("PaidDir"), QStringLiteral("")).toString();
    {
        const QString legacyPaid = QDir::fromNativeSeparators(values.paidDir.trimmed());
        if (legacyPaid.compare(QStringLiteral("D:/aaa.txt"), Qt::CaseInsensitive) == 0) {
            values.paidDir.clear();
            settings.setValue(QStringLiteral("PaidDir"), QStringLiteral(""));
            settings.sync();
        }
    }
    values.restUrl = settings.value(QStringLiteral("RestUrl"), QStringLiteral("http://newapi.7xpay.com/")).toString();
    values.port = settings.value(QStringLiteral("Port"), QStringLiteral("9527")).toString();
    values.gatewayAdvertisedIp = settings.value(QStringLiteral("GatewayAdvertisedIp"), QStringLiteral("")).toString();
    values.gatewayAdvertisedIpAutoDetect = settings.value(QStringLiteral("GatewayAdvertisedIpAutoDetect"), true).toBool();
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
    values.mainWindowCloseAction = settings.value(QStringLiteral("MainWindowCloseAction"), QString()).toString().trimmed();
    values.updateManifestUrl = settings.value(QStringLiteral("UpdateManifestUrl"), QString()).toString().trimmed();

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
    settings.setValue(QStringLiteral("MerchantUuid"), values.merchantUuid);
    settings.setValue(QStringLiteral("Domain"), values.website);
    settings.setValue(QStringLiteral("YxsmDir"), NormalizeMonitorRootPathList(values.yxsmDir));
    settings.setValue(QStringLiteral("YxsmDirVolumeRootsSeeded"), values.yxsmDirVolumeRootsSeeded);
    settings.setValue(QStringLiteral("WxValidPath"), NormalizeMonitorRootPathList(values.wxValidPath));
    settings.setValue(QStringLiteral("PaidDir"), values.paidDir);
    settings.setValue(QStringLiteral("Port"), values.port);
    settings.setValue(QStringLiteral("GatewayAdvertisedIp"), values.gatewayAdvertisedIp);
    settings.setValue(QStringLiteral("GatewayAdvertisedIpAutoDetect"), values.gatewayAdvertisedIpAutoDetect);
    settings.setValue(QStringLiteral("SqlConnectionStr"), values.sqlConnectionStr);
    settings.setValue(QStringLiteral("SecretKey"), values.secretKey);
    settings.setValue(QStringLiteral("SignKey"), values.signKey);
    settings.setValue(QStringLiteral("IsOpenLog"), values.isOpenLog);
    settings.setValue(QStringLiteral("IsOpenOrderReissue"), values.isOpenOrderReissue);
    settings.setValue(QStringLiteral("IsWeixinZq"), values.isWeixinZq);
    settings.setValue(QStringLiteral("IsWeixinMb"), values.isWeixinMb);
    settings.setValue(QStringLiteral("BootUp"), values.bootUp);
    settings.setValue(QStringLiteral("IsSm"), values.isSm);
    settings.setValue(QStringLiteral("MainWindowCloseAction"), values.mainWindowCloseAction);
    settings.setValue(QStringLiteral("UpdateManifestUrl"), values.updateManifestUrl);
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

namespace {
struct AdvertisedIpResolveOutcome {
    QString ip;
    bool usedLocalFallback = false;
    QString platDetail;
    QString probeDetail;
};

AdvertisedIpResolveOutcome ResolveAdvertisedIpDetailed(const AppConfigValues &values)
{
    AdvertisedIpResolveOutcome o;
    o.ip = MachineCode::PreferredLocalIPv4();
    o.usedLocalFallback = true;

    const QString rest = values.restUrl.trimmed();
    const QString sec = values.secretKey.trimmed();
    if (!rest.isEmpty() && !sec.isEmpty()) {
        QString platErr;
        const GatewayApiClient api(values);
        const QString raw = api.UpdateClientIp(&platErr).trimmed();
        QString v4;
        if (MachineCode::TryNormalizeAdvertisedIpString(raw, &v4) && !v4.isEmpty()) {
            o.ip = v4;
            o.usedLocalFallback = false;
            return o;
        }
        o.platDetail = platErr.trimmed().isEmpty() ? QStringLiteral("响应无效：%1").arg(raw.left(120)) : platErr.trimmed();
    } else {
        o.platDetail = QStringLiteral("未配置 RestUrl 或通讯密钥");
    }

    QString probeErr;
    QString probeIp;
    if (MachineCode::TryFetchPublicIPv4(&probeIp, &probeErr) && !probeIp.isEmpty()) {
        o.ip = probeIp;
        o.usedLocalFallback = false;
        return o;
    }
    o.probeDetail = probeErr.trimmed().isEmpty() ? QStringLiteral("超时或响应无效") : probeErr.trimmed();
    return o;
}
} // namespace

QString AppConfig::ResolveGatewayAdvertisedIpForAutoDetect(const AppConfigValues &values)
{
    return ResolveAdvertisedIpDetailed(values).ip;
}

void AppConfig::RefreshGatewayAdvertisedIpForCurrentMachine(AppConfigValues &values)
{
    if (!values.gatewayAdvertisedIpAutoDetect) {
        return;
    }
    const AdvertisedIpResolveOutcome r = ResolveAdvertisedIpDetailed(values);
    if (r.usedLocalFallback) {
        AppLogger::WriteLog(QStringLiteral("获取网关对外 IP：平台与备用探测均不可用，已使用本机 IPv4 %1（平台：%2；备用：%3）")
                                .arg(r.ip,
                                     r.platDetail.isEmpty() ? QStringLiteral("—") : r.platDetail,
                                     r.probeDetail.isEmpty() ? QStringLiteral("—") : r.probeDetail));
    }
    if (values.gatewayAdvertisedIp.trimmed() == r.ip.trimmed()) {
        return;
    }
    values.gatewayAdvertisedIp = r.ip;
    Save(values);
}
