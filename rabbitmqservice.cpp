#include "rabbitmqservice.h"

#include "applogger.h"
#include "legacycryptoutil.h"
#include "machinecode.h"

#include <QApplication>
#include <QCoreApplication>
#include <QEventLoop>
#include <QHostInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QTcpSocket>
#include <QSysInfo>
#include <QTimer>
#include <QUrl>
#include <QVector>

namespace {
QStringList SocketConfigPassPhrases(const AppConfigValues &config)
{
    QStringList passPhrases;
    passPhrases << QStringLiteral("#gddfxyz$123")
                << QStringLiteral("gddfxyz$123")
                << QStringLiteral("hdfgail9xyzgzl88");

    for (const QString &value : {config.secretKey.trimmed(), config.signKey.trimmed()}) {
        if (!value.isEmpty() && !passPhrases.contains(value)) {
            passPhrases << value;
        }
    }

    return passPhrases;
}

QMap<QString, QString> ParseSocketConfig(const QString &connectionString)
{
    QMap<QString, QString> values;
    const auto parts = connectionString.split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const auto &part : parts) {
        const int separatorIndex = part.indexOf(QLatin1Char('='));
        if (separatorIndex <= 0) {
            continue;
        }

        const QString key = part.left(separatorIndex).trimmed().toLower();
        const QString value = part.mid(separatorIndex + 1).trimmed();
        if (!key.isEmpty() && !value.isEmpty()) {
            values.insert(key, value);
        }
    }

    return values;
}

QMap<QString, QString> ResolveSocketConfig(const AppConfigValues &config)
{
    const auto plainConfig = ParseSocketConfig(config.socketConnectionString);
    if (plainConfig.contains(QStringLiteral("server"))
        && plainConfig.contains(QStringLiteral("username"))
        && plainConfig.contains(QStringLiteral("password"))) {
        return plainConfig;
    }

    for (const auto &passPhrase : SocketConfigPassPhrases(config)) {
        if (passPhrase.isEmpty()) {
            continue;
        }

        bool ok = false;
        const QString decrypted = LegacyCryptoUtil::DecryptRijndaelBase64(config.socketConnectionString, passPhrase, &ok);
        if (!ok || decrypted.trimmed().isEmpty()) {
            continue;
        }

        const auto decryptedConfig = ParseSocketConfig(decrypted);
        if (decryptedConfig.contains(QStringLiteral("server"))
            && decryptedConfig.contains(QStringLiteral("username"))
            && decryptedConfig.contains(QStringLiteral("password"))) {
            return decryptedConfig;
        }
    }

    return plainConfig;
}

QString ResolveSocketConfigText(const AppConfigValues &config)
{
    const QString plainText = config.socketConnectionString.trimmed();
    const auto plainConfig = ParseSocketConfig(plainText);
    if (plainConfig.contains(QStringLiteral("server"))
        && plainConfig.contains(QStringLiteral("username"))
        && plainConfig.contains(QStringLiteral("password"))) {
        return plainText;
    }

    for (const auto &passPhrase : SocketConfigPassPhrases(config)) {
        if (passPhrase.isEmpty()) {
            continue;
        }

        bool ok = false;
        const QString decrypted = LegacyCryptoUtil::DecryptRijndaelBase64(config.socketConnectionString, passPhrase, &ok);
        if (ok && !decrypted.trimmed().isEmpty()) {
            return decrypted.trimmed();
        }
    }

    return plainText;
}

QString SocketConfigValue(const QMap<QString, QString> &socketConfig, const QStringList &keys)
{
    for (const auto &key : keys) {
        const QString normalizedKey = key.trimmed().toLower();
        if (socketConfig.contains(normalizedKey)) {
            const QString value = socketConfig.value(normalizedKey).trimmed();
            if (!value.isEmpty()) {
                return value;
            }
        }
    }

    return {};
}

QString ServerHostPart(const QString &serverValue)
{
    const QString trimmedValue = serverValue.trimmed();
    if (trimmedValue.isEmpty()) {
        return {};
    }

    QUrl serverUrl(trimmedValue);
    if (serverUrl.isValid() && !serverUrl.scheme().isEmpty() && !serverUrl.host().isEmpty()) {
        return serverUrl.host().trimmed();
    }

    const int separatorIndex = trimmedValue.lastIndexOf(QLatin1Char(':'));
    if (separatorIndex > 0 && trimmedValue.indexOf(QLatin1Char(']')) < 0) {
        bool ok = false;
        trimmedValue.mid(separatorIndex + 1).toInt(&ok);
        if (ok) {
            return trimmedValue.left(separatorIndex).trimmed();
        }
    }

    return trimmedValue;
}

int ServerPortPart(const QString &serverValue)
{
    const QString trimmedValue = serverValue.trimmed();
    if (trimmedValue.isEmpty()) {
        return 0;
    }

    QUrl serverUrl(trimmedValue);
    if (serverUrl.isValid() && !serverUrl.scheme().isEmpty() && serverUrl.port() > 0) {
        return serverUrl.port();
    }

    const int separatorIndex = trimmedValue.lastIndexOf(QLatin1Char(':'));
    if (separatorIndex > 0 && trimmedValue.indexOf(QLatin1Char(']')) < 0) {
        bool ok = false;
        const int port = trimmedValue.mid(separatorIndex + 1).trimmed().toInt(&ok);
        if (ok && port > 0) {
            return port;
        }
    }

    return 0;
}

QString ResolvedHost(const AppConfigValues &config)
{
    const auto socketConfig = ResolveSocketConfig(config);
    const QString serverValue = SocketConfigValue(socketConfig,
                                                  {QStringLiteral("server"), QStringLiteral("host"), QStringLiteral("hostname")});
    const QString serverHost = ServerHostPart(serverValue);
    if (!serverHost.isEmpty()) {
        return serverHost;
    }

    return config.rabbitMqHost.trimmed().isEmpty() ? QStringLiteral("127.0.0.1") : config.rabbitMqHost.trimmed();
}

int ResolvedManagementPort(const AppConfigValues &config)
{
    const auto socketConfig = ResolveSocketConfig(config);
    bool ok = false;
    const int port = SocketConfigValue(socketConfig,
                                       {QStringLiteral("managementport"), QStringLiteral("httpport"), QStringLiteral("apiport")}).toInt(&ok);
    if (ok && port > 0) {
        return port;
    }

    return config.rabbitMqManagementPort > 0 ? config.rabbitMqManagementPort : 15672;
}

int ResolvedAmqpPort(const AppConfigValues &config)
{
    const auto socketConfig = ResolveSocketConfig(config);

    bool ok = false;
    const int explicitPort = SocketConfigValue(socketConfig,
                                               {QStringLiteral("port"), QStringLiteral("amqpport")}).toInt(&ok);
    if (ok && explicitPort > 0) {
        return explicitPort;
    }

    const int serverPort = ServerPortPart(SocketConfigValue(socketConfig,
                                                            {QStringLiteral("server"), QStringLiteral("host"), QStringLiteral("hostname")}));
    if (serverPort > 0) {
        return serverPort;
    }

    return config.rabbitMqAmqpPort > 0 ? config.rabbitMqAmqpPort : 5672;
}

QString ResolvedUser(const AppConfigValues &config)
{
    const auto socketConfig = ResolveSocketConfig(config);
    if (socketConfig.contains(QStringLiteral("username"))) {
        return socketConfig.value(QStringLiteral("username"));
    }

    return config.rabbitMqUser.trimmed().isEmpty() ? QStringLiteral("admin") : config.rabbitMqUser.trimmed();
}

QString ResolvedPassword(const AppConfigValues &config)
{
    const auto socketConfig = ResolveSocketConfig(config);
    if (socketConfig.contains(QStringLiteral("password"))) {
        return socketConfig.value(QStringLiteral("password"));
    }

    return config.rabbitMqPassword.isEmpty() ? QStringLiteral("admin") : config.rabbitMqPassword;
}

QString ResolvedVirtualHost(const AppConfigValues &config)
{
    const auto socketConfig = ResolveSocketConfig(config);
    const QString serverValue = SocketConfigValue(socketConfig,
                                                  {QStringLiteral("server"), QStringLiteral("host"), QStringLiteral("hostname")});
    QUrl serverUrl(serverValue);
    if (serverUrl.isValid() && !serverUrl.scheme().isEmpty()) {
        const QString path = serverUrl.path().trimmed();
        if (!path.isEmpty() && path != QStringLiteral("/")) {
            return QUrl::fromPercentEncoding(path.mid(1).toUtf8());
        }
    }

    const QString vhost = SocketConfigValue(socketConfig,
                                            {QStringLiteral("virtualhost"), QStringLiteral("virtual-host"), QStringLiteral("vhost")});
    if (!vhost.isEmpty()) {
        return vhost;
    }

    return config.rabbitMqVirtualHost.isEmpty() ? QStringLiteral("/") : config.rabbitMqVirtualHost;
}

QString ResolveQueueName(const AppConfigValues &config)
{
    const QString configuredQueue = config.rabbitMqQueue.trimmed();
    if (!configuredQueue.isEmpty() && configuredQueue != QLatin1String("gateway.queue")) {
        return configuredQueue;
    }

    const QString machineCode = MachineCode().GetRNum().trimmed();
    if (!machineCode.isEmpty()) {
        return machineCode;
    }

    const QString hostName = QHostInfo::localHostName().trimmed();
    if (!hostName.isEmpty()) {
        return hostName;
    }

    return QStringLiteral("gateway.queue");
}

QByteArray AuthorizationHeader(const AppConfigValues &config)
{
    return QByteArray("Basic ") + (ResolvedUser(config) + ":" + ResolvedPassword(config)).toUtf8().toBase64();
}

QString ManagementUrl(const AppConfigValues &config, const QString &suffix)
{
    return QStringLiteral("http://%1:%2%3").arg(ResolvedHost(config)).arg(ResolvedManagementPort(config)).arg(suffix);
}

void AppendUInt16(QByteArray &buffer, quint16 value)
{
    buffer.append(char((value >> 8) & 0xFF));
    buffer.append(char(value & 0xFF));
}

void AppendUInt32(QByteArray &buffer, quint32 value)
{
    buffer.append(char((value >> 24) & 0xFF));
    buffer.append(char((value >> 16) & 0xFF));
    buffer.append(char((value >> 8) & 0xFF));
    buffer.append(char(value & 0xFF));
}

void AppendUInt64(QByteArray &buffer, quint64 value)
{
    for (int shift = 56; shift >= 0; shift -= 8) {
        buffer.append(char((value >> shift) & 0xFF));
    }
}

quint16 ReadUInt16(const QByteArray &buffer, int offset)
{
    return (quint16(quint8(buffer.at(offset))) << 8)
           | quint16(quint8(buffer.at(offset + 1)));
}

quint32 ReadUInt32(const QByteArray &buffer, int offset)
{
    return (quint32(quint8(buffer.at(offset))) << 24)
           | (quint32(quint8(buffer.at(offset + 1))) << 16)
           | (quint32(quint8(buffer.at(offset + 2))) << 8)
           | quint32(quint8(buffer.at(offset + 3)));
}

quint64 ReadUInt64(const QByteArray &buffer, int offset)
{
    quint64 value = 0;
    for (int index = 0; index < 8; ++index) {
        value = (value << 8) | quint64(quint8(buffer.at(offset + index)));
    }
    return value;
}

void AppendShortString(QByteArray &buffer, const QString &text)
{
    QByteArray utf8 = text.toUtf8();
    if (utf8.size() > 255) {
        utf8 = utf8.left(255);
    }
    buffer.append(char(utf8.size()));
    buffer.append(utf8);
}

QString ReadShortString(const QByteArray &buffer, int *offset)
{
    if (!offset || *offset >= buffer.size()) {
        return {};
    }

    const int length = quint8(buffer.at(*offset));
    ++(*offset);
    if (*offset + length > buffer.size()) {
        return {};
    }

    const QString text = QString::fromUtf8(buffer.constData() + *offset, length);
    *offset += length;
    return text;
}

void AppendLongString(QByteArray &buffer, const QByteArray &data)
{
    AppendUInt32(buffer, quint32(data.size()));
    buffer.append(data);
}

void AppendEmptyTable(QByteArray &buffer)
{
    AppendUInt32(buffer, 0);
}

QByteArray BuildFrame(quint8 type, quint16 channel, const QByteArray &payload)
{
    QByteArray frame;
    frame.reserve(8 + payload.size());
    frame.append(char(type));
    AppendUInt16(frame, channel);
    AppendUInt32(frame, quint32(payload.size()));
    frame.append(payload);
    frame.append(char(0xCE));
    return frame;
}

QByteArray BuildMethodFrame(quint16 channel, quint16 classId, quint16 methodId, const QByteArray &arguments)
{
    QByteArray payload;
    AppendUInt16(payload, classId);
    AppendUInt16(payload, methodId);
    payload.append(arguments);
    return BuildFrame(1, channel, payload);
}

QByteArray BuildProtocolHeader()
{
    QByteArray header;
    header.append("AMQP", 4);
    header.append(char(0));
    header.append(char(0));
    header.append(char(9));
    header.append(char(1));
    return header;
}

QByteArray BuildConnectionStartOkArguments(const AppConfigValues &config)
{
    QByteArray arguments;
    AppendEmptyTable(arguments);
    AppendShortString(arguments, QStringLiteral("PLAIN"));

    QByteArray response;
    response.append(char(0));
    response.append(ResolvedUser(config).toUtf8());
    response.append(char(0));
    response.append(ResolvedPassword(config).toUtf8());
    AppendLongString(arguments, response);
    AppendShortString(arguments, QStringLiteral("en_US"));
    return arguments;
}

QByteArray BuildConnectionTuneOkArguments(quint16 channelMax, quint32 frameMax, quint16 heartbeat)
{
    QByteArray arguments;
    AppendUInt16(arguments, channelMax);
    AppendUInt32(arguments, frameMax);
    AppendUInt16(arguments, heartbeat);
    return arguments;
}

QByteArray BuildConnectionOpenArguments(const QString &virtualHost)
{
    QByteArray arguments;
    AppendShortString(arguments, virtualHost.isEmpty() ? QStringLiteral("/") : virtualHost);
    AppendShortString(arguments, QString());
    arguments.append(char(0));
    return arguments;
}

QByteArray BuildChannelOpenArguments()
{
    QByteArray arguments;
    AppendShortString(arguments, QString());
    return arguments;
}

QByteArray BuildQueueDeclareArguments(const QString &queueName)
{
    QByteArray arguments;
    AppendUInt16(arguments, 0);
    AppendShortString(arguments, queueName);
    arguments.append(char(0));
    AppendEmptyTable(arguments);
    return arguments;
}

QByteArray BuildExchangeDeclareArguments(const QString &exchange, const QString &exchangeType, bool durable)
{
    QByteArray arguments;
    AppendUInt16(arguments, 0);
    AppendShortString(arguments, exchange);
    AppendShortString(arguments, exchangeType);
    quint8 flags = 0;
    if (durable) {
        flags |= 0x02;
    }
    arguments.append(char(flags));
    AppendEmptyTable(arguments);
    return arguments;
}

QByteArray BuildBasicPublishArguments(const QString &exchange, const QString &routingKey)
{
    QByteArray arguments;
    AppendUInt16(arguments, 0);
    AppendShortString(arguments, exchange);
    AppendShortString(arguments, routingKey);
    arguments.append(char(0));
    return arguments;
}

QByteArray BuildContentHeaderFrame(quint16 channel,
                                   quint16 classId,
                                   quint64 bodySize,
                                   quint8 deliveryMode,
                                   const QString &messageType)
{
    QByteArray payload;
    AppendUInt16(payload, classId);
    AppendUInt16(payload, 0);
    AppendUInt64(payload, bodySize);

    quint16 propertyFlags = 0;
    if (deliveryMode > 0) {
        propertyFlags |= 0x1000;
    }
    if (!messageType.isEmpty()) {
        propertyFlags |= 0x0020;
    }
    AppendUInt16(payload, propertyFlags);

    if (deliveryMode > 0) {
        payload.append(char(deliveryMode));
    }
    if (!messageType.isEmpty()) {
        AppendShortString(payload, messageType);
    }

    return BuildFrame(2, channel, payload);
}

bool ReadFrameBlocking(QTcpSocket &socket,
                       QByteArray &buffer,
                       quint8 *frameType,
                       quint16 *channel,
                       QByteArray *payload,
                       QString *errorMessage)
{
    while (true) {
        if (buffer.size() >= 8) {
            const quint32 payloadSize = ReadUInt32(buffer, 3);
            const int fullFrameSize = 7 + int(payloadSize) + 1;
            if (buffer.size() >= fullFrameSize) {
                if (quint8(buffer.at(fullFrameSize - 1)) != 0xCE) {
                    if (errorMessage) {
                        *errorMessage = QStringLiteral("RabbitMQ AMQP 帧结束符无效");
                    }
                    return false;
                }

                if (frameType) {
                    *frameType = quint8(buffer.at(0));
                }
                if (channel) {
                    *channel = ReadUInt16(buffer, 1);
                }
                if (payload) {
                    *payload = buffer.mid(7, payloadSize);
                }
                buffer.remove(0, fullFrameSize);
                return true;
            }
        }

        if (!socket.waitForReadyRead(5000)) {
            if (errorMessage) {
                *errorMessage = socket.errorString().isEmpty()
                                    ? QStringLiteral("RabbitMQ AMQP 读取超时")
                                    : socket.errorString();
            }
            return false;
        }

        buffer.append(socket.readAll());
    }
}

bool WaitForMethod(QTcpSocket &socket,
                   QByteArray &buffer,
                   quint16 expectedChannel,
                   quint16 expectedClassId,
                   quint16 expectedMethodId,
                   QByteArray *methodPayload,
                   QString *errorMessage)
{
    while (true) {
        quint8 frameType = 0;
        quint16 channel = 0;
        QByteArray payload;
        if (!ReadFrameBlocking(socket, buffer, &frameType, &channel, &payload, errorMessage)) {
            return false;
        }

        if (frameType == 8) {
            continue;
        }

        if (frameType != 1 || payload.size() < 4) {
            continue;
        }

        const quint16 classId = ReadUInt16(payload, 0);
        const quint16 methodId = ReadUInt16(payload, 2);
        if (channel == expectedChannel && classId == expectedClassId && methodId == expectedMethodId) {
            if (methodPayload) {
                *methodPayload = payload.mid(4);
            }
            return true;
        }

        if (classId == 10 && methodId == 50) {
            int offset = 4;
            const quint16 replyCode = ReadUInt16(payload, offset);
            offset += 2;
            const QString replyText = ReadShortString(payload, &offset);
            if (errorMessage) {
                *errorMessage = QStringLiteral("RabbitMQ 连接被关闭：%1 (%2)").arg(replyText).arg(replyCode);
            }
            return false;
        }

        if (classId == 20 && methodId == 40) {
            int offset = 4;
            const quint16 replyCode = ReadUInt16(payload, offset);
            offset += 2;
            const QString replyText = ReadShortString(payload, &offset);
            if (errorMessage) {
                *errorMessage = QStringLiteral("RabbitMQ 通道被关闭：%1 (%2)").arg(replyText).arg(replyCode);
            }
            return false;
        }
    }
}

bool ExecuteAmqpPublish(const AppConfigValues &config,
                        const QString &exchange,
                        const QString &messageType,
                        const QString &payload,
                        QString *errorMessage)
{
    QTcpSocket socket;
    QByteArray buffer;
    socket.connectToHost(ResolvedHost(config), ResolvedAmqpPort(config));
    if (!socket.waitForConnected(5000)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("RabbitMQ AMQP 连接失败：%1").arg(socket.errorString());
        }
        return false;
    }

    socket.write(BuildProtocolHeader());
    if (!socket.waitForBytesWritten(5000)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("RabbitMQ AMQP 发送协议头失败：%1").arg(socket.errorString());
        }
        return false;
    }

    QByteArray methodPayload;
    if (!WaitForMethod(socket, buffer, 0, 10, 10, &methodPayload, errorMessage)) {
        return false;
    }

    socket.write(BuildMethodFrame(0, 10, 11, BuildConnectionStartOkArguments(config)));
    if (!socket.waitForBytesWritten(5000)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("RabbitMQ AMQP 发送 Start-Ok 失败：%1").arg(socket.errorString());
        }
        return false;
    }

    if (!WaitForMethod(socket, buffer, 0, 10, 30, &methodPayload, errorMessage)) {
        return false;
    }

    if (methodPayload.size() < 8) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("RabbitMQ AMQP Tune 数据无效");
        }
        return false;
    }

    int offset = 0;
    const quint16 channelMax = ReadUInt16(methodPayload, offset);
    offset += 2;
    const quint32 frameMax = ReadUInt32(methodPayload, offset);
    offset += 4;
    const quint16 heartbeat = ReadUInt16(methodPayload, offset);

    socket.write(BuildMethodFrame(0, 10, 31, BuildConnectionTuneOkArguments(channelMax, frameMax, heartbeat)));
    socket.write(BuildMethodFrame(0, 10, 40, BuildConnectionOpenArguments(ResolvedVirtualHost(config))));
    if (!socket.waitForBytesWritten(5000)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("RabbitMQ AMQP 打开连接失败：%1").arg(socket.errorString());
        }
        return false;
    }

    if (!WaitForMethod(socket, buffer, 0, 10, 41, nullptr, errorMessage)) {
        return false;
    }

    socket.write(BuildMethodFrame(1, 20, 10, BuildChannelOpenArguments()));
    if (!socket.waitForBytesWritten(5000)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("RabbitMQ AMQP 打开通道失败：%1").arg(socket.errorString());
        }
        return false;
    }

    if (!WaitForMethod(socket, buffer, 1, 20, 11, nullptr, errorMessage)) {
        return false;
    }

    socket.write(BuildMethodFrame(1, 40, 10, BuildExchangeDeclareArguments(exchange, QStringLiteral("topic"), true)));
    if (!socket.waitForBytesWritten(5000)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("RabbitMQ AMQP 声明交换机失败：%1").arg(socket.errorString());
        }
        return false;
    }

    if (!WaitForMethod(socket, buffer, 1, 40, 11, nullptr, errorMessage)) {
        return false;
    }

    const QByteArray bodyBytes = payload.toUtf8();
    socket.write(BuildMethodFrame(1, 60, 40, BuildBasicPublishArguments(exchange, QString())));
    socket.write(BuildContentHeaderFrame(1, 60, quint64(bodyBytes.size()), 2, messageType));
    socket.write(BuildFrame(3, 1, bodyBytes));
    if (!socket.waitForBytesWritten(5000)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("RabbitMQ AMQP 发送消息失败：%1").arg(socket.errorString());
        }
        return false;
    }

    AppLogger::WriteLog(QStringLiteral("RabbitMQ AMQP 发送消息：exchange=%1, type=%2, payload=%3")
                            .arg(exchange, messageType, payload.left(500)));

    if (socket.waitForReadyRead(500)) {
        quint8 frameType = 0;
        quint16 channel = 0;
        QByteArray replyPayload;
        if (!ReadFrameBlocking(socket, buffer, &frameType, &channel, &replyPayload, errorMessage)) {
            return false;
        }

        if (frameType == 1 && replyPayload.size() >= 4) {
            const quint16 classId = ReadUInt16(replyPayload, 0);
            const quint16 methodId = ReadUInt16(replyPayload, 2);
            if (classId == 10 && methodId == 50) {
                int offset = 4;
                const quint16 replyCode = ReadUInt16(replyPayload, offset);
                offset += 2;
                const QString replyText = ReadShortString(replyPayload, &offset);
                if (errorMessage) {
                    *errorMessage = QStringLiteral("RabbitMQ 发布后连接被关闭：%1 (%2)").arg(replyText).arg(replyCode);
                }
                return false;
            }
            if (classId == 20 && methodId == 40) {
                int offset = 4;
                const quint16 replyCode = ReadUInt16(replyPayload, offset);
                offset += 2;
                const QString replyText = ReadShortString(replyPayload, &offset);
                if (errorMessage) {
                    *errorMessage = QStringLiteral("RabbitMQ 发布后通道被关闭：%1 (%2)").arg(replyText).arg(replyCode);
                }
                return false;
            }
        }
    }

    socket.disconnectFromHost();
    return true;
}

QByteArray BuildBasicConsumeArguments(const QString &queueName)
{
    QByteArray arguments;
    AppendUInt16(arguments, 0);
    AppendShortString(arguments, queueName);
    AppendShortString(arguments, QString());
    arguments.append(char(0x02));
    AppendEmptyTable(arguments);
    return arguments;
}

bool ExecuteRequest(QNetworkReply *reply, QString *errorMessage, const QString &timeoutMessage, const QList<int> &expectedStatusCodes)
{
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);

    QObject::connect(&timer, &QTimer::timeout, &loop, [&] {
        if (errorMessage) {
            *errorMessage = timeoutMessage;
        }
        if (reply->isRunning()) {
            reply->abort();
        }
        loop.quit();
    });
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);

    timer.start(3000);
    loop.exec();
    timer.stop();

    const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const bool success = reply->error() == QNetworkReply::NoError && expectedStatusCodes.contains(statusCode);
    if (!success && errorMessage && errorMessage->isEmpty()) {
        *errorMessage = statusCode > 0
                            ? QStringLiteral("RabbitMQ 管理接口返回状态码：%1").arg(statusCode)
                            : reply->errorString();
    }

    reply->deleteLater();
    return success;
}

bool EnsureQueueExists(const AppConfigValues &config, QString *errorMessage)
{
    const QString virtualHost = ResolvedVirtualHost(config);
    const QString encodedVhost = QString::fromUtf8(QUrl::toPercentEncoding(virtualHost));
    const QString encodedQueue = QString::fromUtf8(QUrl::toPercentEncoding(ResolveQueueName(config)));

    QNetworkAccessManager manager;
    QNetworkRequest request(QUrl(ManagementUrl(config, QStringLiteral("/api/queues/%1/%2").arg(encodedVhost, encodedQueue))));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Authorization", AuthorizationHeader(config));

    const QJsonObject body{
        {QStringLiteral("auto_delete"), false},
        {QStringLiteral("durable"), false},
        {QStringLiteral("arguments"), QJsonObject()}
    };

    return ExecuteRequest(manager.put(request, QJsonDocument(body).toJson(QJsonDocument::Compact)),
                          errorMessage,
                          QStringLiteral("声明 RabbitMQ 队列超时"),
                          QList<int>{201, 204});
}

bool PublishWithKnownRoute(const AppConfigValues &config,
                           const QString &exchange,
                           const QString &messageType,
                           const QString &payload,
                           QString *errorMessage)
{
    return RabbitMqService::Publish(config, exchange, messageType, payload, errorMessage);
}

RabbitMqService::MessageHandler g_messageHandler;

class RabbitMqListener : public QObject
{
public:
    explicit RabbitMqListener(QObject *parent = nullptr)
        : QObject(parent)
    {
        connect(&m_socket, &QTcpSocket::connected, this, [this] { OnConnected(); });
        connect(&m_socket, &QTcpSocket::readyRead, this, [this] { OnReadyRead(); });
        connect(&m_socket, &QTcpSocket::disconnected, this, [this] { OnDisconnected(); });
        connect(&m_socket, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) { OnSocketError(); });
        connect(&m_reconnectTimer, &QTimer::timeout, this, [this] { ConnectToBroker(); });
        connect(&m_heartbeatTimer, &QTimer::timeout, this, [this] { SendHeartbeat(); });

        m_reconnectTimer.setSingleShot(true);
        m_heartbeatTimer.setSingleShot(false);
    }

    bool Start(const AppConfigValues &config, QString *errorMessage)
    {
        Stop();
        m_config = config;
        m_stopping = false;
        m_lastError.clear();
        m_startupError.clear();
        m_consumerReady = false;
        m_buffer.clear();
        m_pendingMessageBody.clear();
        m_pendingMessageBodySize = 0;
        m_waitingForMessageBody = false;

        ConnectToBroker();

        QEventLoop loop;
        QTimer timeoutTimer;
        QTimer stateChecker;
        timeoutTimer.setSingleShot(true);
        stateChecker.setInterval(50);

        QObject::connect(&timeoutTimer, &QTimer::timeout, &loop, &QEventLoop::quit);
        QObject::connect(&stateChecker, &QTimer::timeout, &loop, [&] {
            if (m_consumerReady || !m_startupError.isEmpty()) {
                loop.quit();
            }
        });

        timeoutTimer.start(5000);
        stateChecker.start();
        loop.exec();
        stateChecker.stop();

        if (m_consumerReady) {
            return true;
        }

        if (errorMessage) {
            *errorMessage = m_startupError.isEmpty() ? QStringLiteral("连接 RabbitMQ AMQP 超时") : m_startupError;
        }
        return false;
    }

    void Stop()
    {
        m_stopping = true;
        m_reconnectTimer.stop();
        m_heartbeatTimer.stop();
        m_consumerReady = false;
        m_buffer.clear();
        m_pendingMessageBody.clear();
        m_pendingMessageBodySize = 0;
        m_waitingForMessageBody = false;
        if (m_socket.state() != QAbstractSocket::UnconnectedState) {
            m_socket.abort();
        }
        m_lastError.clear();
    }

private:
    void ConnectToBroker()
    {
        if (m_socket.state() != QAbstractSocket::UnconnectedState) {
            return;
        }

        m_socket.connectToHost(ResolvedHost(m_config), ResolvedAmqpPort(m_config));
    }

    void OnConnected()
    {
        m_startupError.clear();
        m_lastError.clear();
        AppLogger::WriteLog(QStringLiteral("RabbitMQ AMQP TCP 已连接：%1:%2")
                                .arg(ResolvedHost(m_config))
                                .arg(ResolvedAmqpPort(m_config)));
        m_socket.write(BuildProtocolHeader());
        m_socket.flush();
        AppLogger::WriteLog(QStringLiteral("RabbitMQ AMQP 协议头已发送"));
    }

    void OnReadyRead()
    {
        m_buffer.append(m_socket.readAll());
        while (true) {
            if (m_buffer.size() < 8) {
                return;
            }

            const quint8 frameType = quint8(m_buffer.at(0));
            const quint16 channel = ReadUInt16(m_buffer, 1);
            const quint32 payloadSize = ReadUInt32(m_buffer, 3);
            const int fullFrameSize = 7 + int(payloadSize) + 1;
            if (m_buffer.size() < fullFrameSize) {
                return;
            }

            const QByteArray payload = m_buffer.mid(7, payloadSize);
            const quint8 frameEnd = quint8(m_buffer.at(fullFrameSize - 1));
            m_buffer.remove(0, fullFrameSize);

            if (frameEnd != 0xCE) {
                FailConnection(QStringLiteral("RabbitMQ AMQP 帧结束符无效"));
                return;
            }

            ProcessFrame(frameType, channel, payload);
        }
    }

    void ProcessFrame(quint8 frameType, quint16 channel, const QByteArray &payload)
    {
        Q_UNUSED(channel);

        if (frameType == 1) {
            ProcessMethodFrame(payload);
            return;
        }
        if (frameType == 2) {
            ProcessHeaderFrame(payload);
            return;
        }
        if (frameType == 3) {
            ProcessBodyFrame(payload);
            return;
        }
    }

    void ProcessMethodFrame(const QByteArray &payload)
    {
        if (payload.size() < 4) {
            return;
        }

        const quint16 classId = ReadUInt16(payload, 0);
        const quint16 methodId = ReadUInt16(payload, 2);
        int offset = 4;

        if (classId == 10 && methodId == 10) {
            AppLogger::WriteLog(QStringLiteral("RabbitMQ AMQP 收到 Connection.Start，发送 Start-Ok"));
            m_socket.write(BuildMethodFrame(0, 10, 11, BuildConnectionStartOkArguments(m_config)));
            return;
        }
        if (classId == 10 && methodId == 30) {
            const quint16 channelMax = ReadUInt16(payload, offset);
            offset += 2;
            m_frameMax = ReadUInt32(payload, offset);
            offset += 4;
            m_heartbeatSeconds = ReadUInt16(payload, offset);

            AppLogger::WriteLog(QStringLiteral("RabbitMQ AMQP 收到 Connection.Tune，发送 Tune-Ok/Open"));
            m_socket.write(BuildMethodFrame(0, 10, 31, BuildConnectionTuneOkArguments(channelMax, m_frameMax, m_heartbeatSeconds)));
            m_socket.write(BuildMethodFrame(0, 10, 40, BuildConnectionOpenArguments(ResolvedVirtualHost(m_config))));
            if (m_heartbeatSeconds > 0) {
                m_heartbeatTimer.start(qMax(1, int(m_heartbeatSeconds / 2)) * 1000);
            }
            return;
        }
        if (classId == 10 && methodId == 41) {
            AppLogger::WriteLog(QStringLiteral("RabbitMQ AMQP 虚拟主机已打开，发送 Channel.Open"));
            m_socket.write(BuildMethodFrame(1, 20, 10, BuildChannelOpenArguments()));
            return;
        }
        if (classId == 20 && methodId == 11) {
            AppLogger::WriteLog(QStringLiteral("RabbitMQ AMQP 通道已打开，发送 Queue.Declare：%1").arg(ResolveQueueName(m_config)));
            m_socket.write(BuildMethodFrame(1, 50, 10, BuildQueueDeclareArguments(ResolveQueueName(m_config))));
            return;
        }
        if (classId == 50 && methodId == 11) {
            AppLogger::WriteLog(QStringLiteral("RabbitMQ AMQP 队列已声明，发送 Basic.Consume：%1").arg(ResolveQueueName(m_config)));
            m_socket.write(BuildMethodFrame(1, 60, 20, BuildBasicConsumeArguments(ResolveQueueName(m_config))));
            return;
        }
        if (classId == 60 && methodId == 21) {
            m_consumerReady = true;
            AppLogger::WriteLog(QStringLiteral("RabbitMQ AMQP 消费者已启动，队列：%1").arg(ResolveQueueName(m_config)));
            return;
        }
        if (classId == 60 && methodId == 60) {
            AppLogger::WriteLog(QStringLiteral("RabbitMQ AMQP 收到 Basic.Deliver"));
            m_waitingForMessageBody = true;
            m_pendingMessageBody.clear();
            m_pendingMessageBodySize = 0;
            ReadShortString(payload, &offset);
            offset += 8;
            ++offset;
            ReadShortString(payload, &offset);
            ReadShortString(payload, &offset);
            return;
        }
        if (classId == 10 && methodId == 50) {
            const quint16 replyCode = ReadUInt16(payload, offset);
            offset += 2;
            const QString replyText = ReadShortString(payload, &offset);
            m_socket.write(BuildMethodFrame(0, 10, 51, {}));
            FailConnection(QStringLiteral("RabbitMQ 连接被关闭：%1 (%2)").arg(replyText).arg(replyCode));
            return;
        }
        if (classId == 20 && methodId == 40) {
            const quint16 replyCode = ReadUInt16(payload, offset);
            offset += 2;
            const QString replyText = ReadShortString(payload, &offset);
            m_socket.write(BuildMethodFrame(1, 20, 41, {}));
            FailConnection(QStringLiteral("RabbitMQ 通道被关闭：%1 (%2)").arg(replyText).arg(replyCode));
        }
    }

    void ProcessHeaderFrame(const QByteArray &payload)
    {
        if (!m_waitingForMessageBody || payload.size() < 14) {
            return;
        }

        m_pendingMessageBodySize = ReadUInt64(payload, 4);
        m_pendingMessageType.clear();

        int offset = 12;
        quint16 propertyFlags = ReadUInt16(payload, offset);
        offset += 2;

        if (propertyFlags & 0x8000) {
            ReadShortString(payload, &offset);
        }
        if (propertyFlags & 0x4000) {
            ReadShortString(payload, &offset);
        }
        if (propertyFlags & 0x2000) {
            if (offset + 4 <= payload.size()) {
                const quint32 headerTableLength = ReadUInt32(payload, offset);
                offset += 4 + int(headerTableLength);
            }
        }
        if (propertyFlags & 0x1000) {
            ++offset;
        }
        if (propertyFlags & 0x0800) {
            ++offset;
        }
        if (propertyFlags & 0x0400) {
            ReadShortString(payload, &offset);
        }
        if (propertyFlags & 0x0200) {
            ReadShortString(payload, &offset);
        }
        if (propertyFlags & 0x0100) {
            ReadShortString(payload, &offset);
        }
        if (propertyFlags & 0x0080) {
            ReadShortString(payload, &offset);
        }
        if (propertyFlags & 0x0040) {
            offset += 8;
        }
        if (propertyFlags & 0x0020) {
            m_pendingMessageType = ReadShortString(payload, &offset);
        }
        if (m_pendingMessageBodySize == 0) {
            DispatchMessageBody();
        }
    }

    void ProcessBodyFrame(const QByteArray &payload)
    {
        if (!m_waitingForMessageBody) {
            return;
        }

        m_pendingMessageBody.append(payload);
        if (quint64(m_pendingMessageBody.size()) >= m_pendingMessageBodySize) {
            DispatchMessageBody();
        }
    }

    void DispatchMessageBody()
    {
        QString payload = QString::fromUtf8(m_pendingMessageBody.left(int(m_pendingMessageBodySize)));
        m_waitingForMessageBody = false;
        m_pendingMessageBody.clear();
        m_pendingMessageBodySize = 0;

        if (payload.trimmed().isEmpty()) {
            return;
        }

        if (!m_pendingMessageType.isEmpty()) {
            const QJsonDocument document = QJsonDocument::fromJson(payload.toUtf8());
            if (document.isObject()) {
                QJsonObject object = document.object();
                if (!object.contains(QStringLiteral("OrderType"))
                    && !object.contains(QStringLiteral("orderType"))
                    && !object.contains(QStringLiteral("Type"))
                    && !object.contains(QStringLiteral("type"))) {
                    object.insert(QStringLiteral("Type"), m_pendingMessageType);
                    payload = QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
                }
            }
            AppLogger::WriteLog(QStringLiteral("RabbitMQ AMQP 消息类型：%1").arg(m_pendingMessageType));
        }

        AppLogger::WriteLog(QStringLiteral("RabbitMQ 原始消息：%1").arg(payload.left(500)));
        m_pendingMessageType.clear();

        if (g_messageHandler) {
            g_messageHandler(payload);
        } else {
            AppLogger::WriteLog(QStringLiteral("收到RabbitMQ消息：%1").arg(payload));
        }
    }

    void SendHeartbeat()
    {
        if (m_socket.state() == QAbstractSocket::ConnectedState) {
            m_socket.write(BuildFrame(8, 0, {}));
            AppLogger::WriteLog(QStringLiteral("RabbitMQ AMQP 心跳已发送"));
        }
    }

    void OnDisconnected()
    {
        m_consumerReady = false;
        m_heartbeatTimer.stop();
        if (!m_stopping) {
            ReportError(QStringLiteral("RabbitMQ AMQP 连接已断开，正在重连"));
            m_reconnectTimer.start(2000);
        }
    }

    void OnSocketError()
    {
        FailConnection(QStringLiteral("RabbitMQ AMQP 连接失败：%1").arg(m_socket.errorString()));
    }

    void FailConnection(const QString &message)
    {
        if (m_startupError.isEmpty()) {
            m_startupError = message;
        }
        ReportError(message);
        if (m_socket.state() != QAbstractSocket::UnconnectedState) {
            m_socket.abort();
        }
    }

    void ReportError(const QString &errorMessage)
    {
        if (m_lastError == errorMessage) {
            return;
        }

        m_lastError = errorMessage;
        AppLogger::WriteLog(errorMessage);
    }

    AppConfigValues m_config;
    QTcpSocket m_socket;
    QTimer m_reconnectTimer;
    QTimer m_heartbeatTimer;
    QByteArray m_buffer;
    QByteArray m_pendingMessageBody;
    QString m_pendingMessageType;
    quint64 m_pendingMessageBodySize = 0;
    quint32 m_frameMax = 131072;
    quint16 m_heartbeatSeconds = 0;
    bool m_waitingForMessageBody = false;
    bool m_consumerReady = false;
    bool m_stopping = false;
    QString m_lastError;
    QString m_startupError;
};

QPointer<RabbitMqListener> g_listener;
}

namespace RabbitMqService {
QString ConsumerQueueName(const AppConfigValues &config)
{
    return ResolveQueueName(config);
}

void SetMessageHandler(MessageHandler handler)
{
    g_messageHandler = std::move(handler);
}

bool TestConnection(const AppConfigValues &config, QString *errorMessage)
{
    QNetworkAccessManager manager;
    QNetworkRequest request(QUrl(ManagementUrl(config, QStringLiteral("/api/overview"))));
    request.setRawHeader("Authorization", AuthorizationHeader(config));

    return ExecuteRequest(manager.get(request),
                          errorMessage,
                          QStringLiteral("连接 RabbitMQ 管理接口超时"),
                          QList<int>{200});
}

bool StartListening(const AppConfigValues &config, QString *errorMessage)
{
    AppLogger::WriteLog(QStringLiteral("RabbitMQ Socket 解密内容：%1").arg(ResolveSocketConfigText(config)));
    AppLogger::WriteLog(QStringLiteral("RabbitMQ 当前连接参数：host=%1, amqpPort=%2, managementPort=%3, vhost=%4, user=%5, queue=%6")
                            .arg(ResolvedHost(config))
                            .arg(ResolvedAmqpPort(config))
                            .arg(ResolvedManagementPort(config))
                            .arg(ResolvedVirtualHost(config))
                            .arg(ResolvedUser(config))
                            .arg(ResolveQueueName(config)));

    if (!g_listener) {
        g_listener = new RabbitMqListener(qApp);
    }

    return g_listener->Start(config, errorMessage);
}

bool Publish(const AppConfigValues &config,
             const QString &exchange,
             const QString &messageType,
             const QString &payload,
             QString *errorMessage)
{
    const QString trimmedExchange = exchange.trimmed();
    if (trimmedExchange.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("RabbitMQ exchange 未配置");
        }
        return false;
    }

    return ExecuteAmqpPublish(config, trimmedExchange, messageType, payload, errorMessage);
}

bool UpdateOrder(const AppConfigValues &config, const QString &payload, QString *errorMessage)
{
    return PublishWithKnownRoute(config,
                                 QStringLiteral("TenantServer.Extensions.EasyNetQ.EasyNetQUpdateOrder, TenantServer"),
                                 QStringLiteral("TenantServer.Extensions.EasyNetQ.EasyNetQUpdateOrder, TenantServer"),
                                 payload,
                                 errorMessage);
}

bool UpdateOrderAccount(const AppConfigValues &config, const QString &payload, QString *errorMessage)
{
    return PublishWithKnownRoute(config,
                                 QStringLiteral("TenantServer.Extensions.EasyNetQ.UpdateOrderAccount, TenantServer"),
                                 QStringLiteral("TenantServer.Extensions.EasyNetQ.UpdateOrderAccount, TenantServer"),
                                 payload,
                                 errorMessage);
}

bool UpdateCheck(const AppConfigValues &config, const QString &payload, QString *errorMessage)
{
    return PublishWithKnownRoute(config,
                                 QStringLiteral("TenantServer.Extensions.EasyNetQ.EasyNetQCheckKey, TenantServer"),
                                 QStringLiteral("TenantServer.Extensions.EasyNetQ.EasyNetQCheckKey, TenantServer"),
                                 payload,
                                 errorMessage);
}

bool PaidApply(const AppConfigValues &config, const QString &payload, QString *errorMessage)
{
    return PublishWithKnownRoute(config,
                                 QStringLiteral("Entities.DTO.Manager.Paid.PaidOrderParameters, Entities"),
                                 QStringLiteral("Entities.DTO.Manager.Paid.PaidOrderParameters, Entities"),
                                 payload,
                                 errorMessage);
}

bool YouXiSaomaProcess(const AppConfigValues &config, const QString &payload, QString *errorMessage)
{
    return PublishWithKnownRoute(config,
                                 QStringLiteral("Entities.DTO.Tenant.QrCode.YouXiSaomaDTO, Entities"),
                                 QStringLiteral("Entities.DTO.Tenant.QrCode.YouXiSaomaDTO, Entities"),
                                 payload,
                                 errorMessage);
}

bool WxValidProcess(const AppConfigValues &config, const QString &payload, QString *errorMessage)
{
    return PublishWithKnownRoute(config,
                                 QStringLiteral("Entities.DTO.Tenant.VxValidDto, Entities"),
                                 QStringLiteral("Entities.DTO.Tenant.VxValidDto, Entities"),
                                 payload,
                                 errorMessage);
}

bool OperationLogProcess(const AppConfigValues &config, const QString &payload, QString *errorMessage)
{
    return PublishWithKnownRoute(config,
                                 QStringLiteral("Entities.DTO.Tenant.WeixinOperationLogDto, Entities"),
                                 QStringLiteral("Entities.DTO.Tenant.WeixinOperationLogDto, Entities"),
                                 payload,
                                 errorMessage);
}

bool RanchResult(const AppConfigValues &config, const QString &payload, QString *errorMessage)
{
    return PublishWithKnownRoute(config,
                                 QStringLiteral("Entities.DTO.Tenant.RanchResult, Entities"),
                                 QStringLiteral("Entities.DTO.Tenant.RanchResult, Entities"),
                                 payload,
                                 errorMessage);
}

bool RanchPTResult(const AppConfigValues &config, const QString &payload, QString *errorMessage)
{
    return PublishWithKnownRoute(config,
                                 QStringLiteral("Entities.DTO.Tenant.RanchPTResult, Entities"),
                                 QStringLiteral("Entities.DTO.Tenant.RanchPTResult, Entities"),
                                 payload,
                                 errorMessage);
}

bool OperationTransferProcess(const AppConfigValues &config, const QString &payload, QString *errorMessage)
{
    return PublishWithKnownRoute(config,
                                 QStringLiteral("Entities.DTO.Tenant.WeixinTransferLogDto, Entities"),
                                 QStringLiteral("Entities.DTO.Tenant.WeixinTransferLogDto, Entities"),
                                 payload,
                                 errorMessage);
}

bool VxCodeEventProcess(const AppConfigValues &config, const QString &payload, QString *errorMessage)
{
    return PublishWithKnownRoute(config,
                                 QStringLiteral("TenantServer.Extensions.EasyNetQ.VxCode, TenantServer"),
                                 QStringLiteral("TenantServer.Extensions.EasyNetQ.VxCode, TenantServer"),
                                 payload,
                                 errorMessage);
}

void StopListening()
{
    if (g_listener) {
        g_listener->Stop();
    }
}
}
