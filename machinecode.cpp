#include "machinecode.h"

#include "appconfig.h"

#include <QAbstractSocket>
#include <QCryptographicHash>
#include <QEventLoop>
#include <QHostAddress>
#include <QNetworkAccessManager>
#include <QNetworkInterface>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>
#include <QTimer>
#include <QUrl>

#ifdef Q_OS_WIN
#include <windows.h>
#include <WtsApi32.h>
#endif

MachineCode::MachineCode() = default;

QString MachineCode::PreferredLocalIPv4()
{
    const auto addresses = QNetworkInterface::allAddresses();
    for (const auto &address : addresses) {
        if (address.protocol() == QAbstractSocket::IPv4Protocol && !address.isLoopback()) {
            return address.toString();
        }
    }
    return QStringLiteral("127.0.0.1");
}

bool MachineCode::TryNormalizeAdvertisedIpString(const QString &text, QString *outIpv4)
{
    if (!outIpv4) {
        return false;
    }
    outIpv4->clear();
    QString t = text.trimmed();
    if (t.isEmpty()) {
        return false;
    }
    const int nl = t.indexOf(QLatin1Char('\n'));
    if (nl >= 0) {
        t = t.left(nl).trimmed();
    }
    QHostAddress addr;
    if (!addr.setAddress(t)) {
        return false;
    }
    if (addr.protocol() == QAbstractSocket::IPv4Protocol) {
        *outIpv4 = addr.toString();
        return true;
    }
    if (addr.protocol() == QAbstractSocket::IPv6Protocol) {
        bool ok = false;
        const quint32 v4 = addr.toIPv4Address(&ok);
        if (ok) {
            *outIpv4 = QHostAddress(v4).toString();
            return !outIpv4->isEmpty();
        }
    }
    return false;
}

bool MachineCode::TryFetchPublicIPv4(QString *outIp, QString *errorMessage)
{
    if (outIp) {
        outIp->clear();
    }

    QNetworkAccessManager nam;
    QNetworkRequest req(QUrl(QStringLiteral("https://icanhazip.com/")));
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Gateway/2.0"));

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);

    QNetworkReply *reply = nam.get(req);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, reply, [reply]() {
        if (reply->isRunning()) {
            reply->abort();
        }
    });

    constexpr int kTimeoutMs = 10000;
    timer.start(kTimeoutMs);
    loop.exec();
    timer.stop();

    const QNetworkReply::NetworkError netErr = reply->error();
    const QString errString = reply->errorString();
    const QByteArray raw = reply->readAll();
    reply->deleteLater();

    if (netErr != QNetworkReply::NoError) {
        if (errorMessage) {
            *errorMessage = errString.isEmpty() ? QStringLiteral("网络错误") : errString;
        }
        return false;
    }

    QString text = QString::fromUtf8(raw).trimmed();

    const int nl = text.indexOf(QLatin1Char('\n'));
    if (nl >= 0) {
        text = text.left(nl).trimmed();
    }

    QHostAddress addr;
    if (!addr.setAddress(text) || addr.protocol() != QAbstractSocket::IPv4Protocol) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("公网 IP 响应无效");
        }
        return false;
    }

    if (outIp) {
        *outIp = text;
    }
    return true;
}

QString MachineCode::EffectiveGatewayAdvertisedHostForConfig(const AppConfigValues &cfg)
{
    QString ip = cfg.gatewayAdvertisedIp.trimmed();
    if (ip.isEmpty()) {
        ip = PreferredLocalIPv4();
    }
    return ip;
}

QString MachineCode::GetRNum() const
{
    const AppConfigValues cfg = AppConfig::Load();
    const QString ip = EffectiveGatewayAdvertisedHostForConfig(cfg);

    const QString portText = cfg.port.trimmed();
    bool portOk = false;
    const int portNum = portText.toInt(&portOk);
    if (!portOk || portNum < 1 || portNum > 65535) {
        return {};
    }

    const QString uuid = cfg.merchantUuid.trimmed();
    if (uuid.size() < 8) {
        return {};
    }

    const QString canonical = QStringLiteral("%1|%2|%3")
                                  .arg(ip.trimmed().toLower(),
                                       QString::number(portNum),
                                       uuid.toLower());

    const QByteArray digest = QCryptographicHash::hash(canonical.toUtf8(), QCryptographicHash::Sha256);
    return QString::fromLatin1(digest.toHex());
}

QString MachineCode::GetCurrentUserName() const
{
    return GetCurrentUser();
}

QString MachineCode::GetCurrentUser() const
{
#ifdef Q_OS_WIN
    LPSTR buffer = nullptr;
    DWORD bytesReturned = 0;
    if (WTSQuerySessionInformationA(WTS_CURRENT_SERVER_HANDLE,
                                    WTS_CURRENT_SESSION,
                                    WTSUserName,
                                    &buffer,
                                    &bytesReturned)
        && buffer != nullptr
        && bytesReturned > 1) {
        const QString userName = QString::fromLocal8Bit(buffer).trimmed();
        WTSFreeMemory(buffer);
        if (!userName.isEmpty()) {
            return userName;
        }
    }
#endif

    const QString userName = qEnvironmentVariable("USERNAME").trimmed();
    return userName.isEmpty() ? QStringLiteral("SYSTEM") : userName;
}
