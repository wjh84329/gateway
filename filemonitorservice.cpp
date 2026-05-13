#include "filemonitorservice.h"

#include "applogger.h"
#include "gatewayapiclient.h"
#include "machinecode.h"
#include "rabbitmqservice.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>

#include <algorithm>
#include <cmath>

namespace {
enum class WatchKind {
    PaidFile,
    GameScanDirectory,
    WxValidSubmitDirectory,
    WxValidAuthDirectory,
    TransferSubmitDirectory,
    TransferQueryDirectory,
    TransferDeductDirectory,
    RechargeResultFile,
    PTRechargeResultFile
};

struct MonitorState {
    QFileSystemWatcher watcher;
    QHash<QString, WatchKind> directoryKinds;
    QHash<QString, WatchKind> fileKinds;
    QHash<QString, QString> lastProcessedLine;
    QHash<QString, QDateTime> suppressedUntil;
    QString cachedIp;
    QString machineCode;
    bool initialized = false;
};

MonitorState &State()
{
    static MonitorState state;
    return state;
}

QString WatchKindName(WatchKind kind)
{
    switch (kind) {
    case WatchKind::PaidFile:
        return QStringLiteral("代付文件");
    case WatchKind::GameScanDirectory:
        return QStringLiteral("游戏扫码提交");
    case WatchKind::WxValidSubmitDirectory:
        return QStringLiteral("微信密保提交数据");
    case WatchKind::WxValidAuthDirectory:
        return QStringLiteral("微信密保认证数据");
    case WatchKind::TransferSubmitDirectory:
        return QStringLiteral("微信转区提交数据");
    case WatchKind::TransferQueryDirectory:
        return QStringLiteral("微信转区点查询");
    case WatchKind::TransferDeductDirectory:
        return QStringLiteral("微信转区点扣除");
    case WatchKind::RechargeResultFile:
        return QStringLiteral("充值记录结果");
    case WatchKind::PTRechargeResultFile:
        return QStringLiteral("普通充值结果");
    }
    return QStringLiteral("未知监控类型");
}

QStringList SplitConfiguredPaths(const QString &value)
{
    QStringList paths;
    for (const auto &part : value.split(QLatin1Char('|'), Qt::SkipEmptyParts)) {
        const QString trimmed = QDir::fromNativeSeparators(part.trimmed());
        if (!trimmed.isEmpty() && !paths.contains(trimmed, Qt::CaseInsensitive)) {
            paths.append(trimmed);
        }
    }
    return paths;
}

QStringList ResolveRootPaths(const QString &configuredValue)
{
    QStringList roots;
    for (const auto &path : SplitConfiguredPaths(configuredValue)) {
        const QString absolutePath = QFileInfo(path).absoluteFilePath();
        QString root;
        if (absolutePath.size() >= 3 && absolutePath.at(1) == QLatin1Char(':')
            && (absolutePath.at(2) == QLatin1Char('/') || absolutePath.at(2) == QLatin1Char('\\'))) {
            root = absolutePath.left(3);
        } else {
            root = QDir(absolutePath).absolutePath();
        }
        if (!root.isEmpty() && !roots.contains(root, Qt::CaseInsensitive)) {
            roots.append(root);
        }
    }
    return roots;
}

QStringList MergeRootPaths(QStringList primaryRoots, const QStringList &secondaryRoots)
{
    for (const auto &root : secondaryRoots) {
        if (!root.isEmpty() && !primaryRoots.contains(root, Qt::CaseInsensitive)) {
            primaryRoots.append(root);
        }
    }
    return primaryRoots;
}

bool IsValidUtf8(const QByteArray &data)
{
    int expectedContinuationBytes = 0;
    for (const unsigned char byte : data) {
        if (expectedContinuationBytes == 0) {
            if ((byte & 0x80) == 0x00) {
                continue;
            }
            if ((byte & 0xE0) == 0xC0) {
                expectedContinuationBytes = 1;
                continue;
            }
            if ((byte & 0xF0) == 0xE0) {
                expectedContinuationBytes = 2;
                continue;
            }
            if ((byte & 0xF8) == 0xF0) {
                expectedContinuationBytes = 3;
                continue;
            }
            return false;
        }

        if ((byte & 0xC0) != 0x80) {
            return false;
        }
        --expectedContinuationBytes;
    }

    return expectedContinuationBytes == 0;
}

QString DecodeTextFile(const QByteArray &data)
{
    if (data.startsWith(QByteArray::fromHex("EFBBBF"))) {
        return QString::fromUtf8(data.mid(3));
    }
    if (data.startsWith(QByteArray::fromHex("FFFE"))) {
        return QString::fromUtf16(reinterpret_cast<const char16_t *>(data.constData() + 2), (data.size() - 2) / 2);
    }
    if (data.startsWith(QByteArray::fromHex("FEFF"))) {
        const QByteArray swapped = QByteArray(data.constData() + 2, data.size() - 2);
        QString result;
        result.reserve(swapped.size() / 2);
        for (int i = 0; i + 1 < swapped.size(); i += 2) {
            const ushort codeUnit = (ushort(quint8(swapped.at(i))) << 8) | ushort(quint8(swapped.at(i + 1)));
            result.append(QChar(codeUnit));
        }
        return result;
    }
    if (IsValidUtf8(data)) {
        return QString::fromUtf8(data);
    }
    return QString::fromLocal8Bit(data);
}

QStringList NonEmptyTrimmedLines(const QString &text)
{
    QStringList lines = text.split(QRegularExpression(QStringLiteral("\r\n|\n|\r")), Qt::SkipEmptyParts);
    for (QString &line : lines) {
        line = line.trimmed();
        if (!line.isEmpty() && line.front().unicode() == 0xFEFF) {
            line.remove(0, 1);
        }
    }
    lines.erase(std::remove_if(lines.begin(), lines.end(), [](const QString &line) { return line.trimmed().isEmpty(); }), lines.end());
    return lines;
}

QString ReadInterestingLine(const QString &filePath, bool useLastLine)
{
    QFile file(filePath);
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        return {};
    }

    const QStringList lines = NonEmptyTrimmedLines(DecodeTextFile(file.readAll()));
    file.close();
    if (lines.isEmpty()) {
        return {};
    }

    return useLastLine ? lines.constLast() : lines.constFirst();
}

bool RemoveLastNonEmptyLineFromTextFile(const QString &filePath)
{
    QFile file(filePath);
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QByteArray data = file.readAll();
    file.close();

    QStringList lines = NonEmptyTrimmedLines(DecodeTextFile(data));
    if (lines.isEmpty()) {
        return false;
    }
    lines.removeLast();

    QString newContent = lines.join(QStringLiteral("\r\n"));
    if (!lines.isEmpty()) {
        newContent.append(QStringLiteral("\r\n"));
    }

    QByteArray out;
    if (data.startsWith(QByteArray::fromHex("EFBBBF"))) {
        out.append(QByteArray::fromHex("EFBBBF"));
    }
    out.append(newContent.toUtf8());

    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    const qint64 written = file.write(out);
    file.close();
    return written == out.size();
}

bool UseLastLine(WatchKind kind)
{
    return kind == WatchKind::GameScanDirectory
           || kind == WatchKind::RechargeResultFile
           || kind == WatchKind::PTRechargeResultFile;
}

bool EnsureDirectoryExists(const QString &path)
{
    return !path.isEmpty() && QDir().mkpath(path);
}

bool EnsureFileExists(const QString &filePath)
{
    const QFileInfo fileInfo(filePath);
    if (!EnsureDirectoryExists(fileInfo.absolutePath())) {
        return false;
    }

    if (QFile::exists(filePath)) {
        return true;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    file.close();
    return true;
}

QString CurrentTimestamp()
{
    return QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

QString ResolveClientIp(const AppConfigValues &config)
{
    auto &state = State();
    if (!state.cachedIp.trimmed().isEmpty()) {
        return state.cachedIp;
    }

    GatewayApiClient client;
    QString errorMessage;
    const QString ip = client.UpdateClientIp(&errorMessage).trimmed();
    if (!ip.isEmpty()) {
        state.cachedIp = ip;
        return state.cachedIp;
    }

    if (!errorMessage.isEmpty()) {
        AppLogger::WriteLog(QStringLiteral("文件监控更新客户端 IP 失败：%1").arg(errorMessage));
    }
    Q_UNUSED(config);
    return {};
}

bool PublishJson(bool (*publishFunc)(const AppConfigValues &, const QString &, QString *),
                 const QJsonObject &payload,
                 const QString &successMessage,
                 const QString &failurePrefix)
{
    const auto config = AppConfig::Load();
    QString errorMessage;
    const QString body = QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact));
    if (!publishFunc(config, body, &errorMessage)) {
        AppLogger::WriteLog(QStringLiteral("%1：%2").arg(failurePrefix, errorMessage));
        return false;
    }

    (void)successMessage;
    return true;
}

QMap<QString, QString> ParseKeyValuePairs(const QString &text)
{
    QMap<QString, QString> values;
    const QStringList pairs = text.split(QLatin1Char('|'), Qt::SkipEmptyParts);
    for (const auto &pair : pairs) {
        const int separatorIndex = pair.indexOf(QLatin1Char(':'));
        if (separatorIndex <= 0) {
            continue;
        }
        values.insert(pair.left(separatorIndex).trimmed(), pair.mid(separatorIndex + 1).trimmed());
    }
    return values;
}

int ReadFlexibleInt(QString text)
{
    text = text.trimmed();
    text.remove(QRegularExpression(QStringLiteral(R"([^\d-])")));
    bool ok = false;
    const int value = text.toInt(&ok);
    return ok ? value : 0;
}

double ReadFlexibleDouble(QString text)
{
    text = text.trimmed();
    text.replace(QStringLiteral("，"), QStringLiteral(","));
    text.replace(QStringLiteral("。"), QStringLiteral("."));
    text.remove(QRegularExpression(QStringLiteral(R"([^\d\.,-])")));
    bool ok = false;
    double value = text.toDouble(&ok);
    if (ok) {
        return value;
    }
    text.remove(QLatin1Char(','));
    value = text.toDouble(&ok);
    return ok ? value : 0.0;
}

void ProcessPath(const QString &filePath, WatchKind kind);

void TrackFile(const QString &filePath, WatchKind kind, bool initializeBaselineOnly)
{
    auto &state = State();
    const QString normalizedPath = QDir::fromNativeSeparators(filePath);
    state.fileKinds.insert(normalizedPath, kind);
    if (!state.watcher.files().contains(normalizedPath)) {
        state.watcher.addPath(normalizedPath);
    }

    if (initializeBaselineOnly) {
        state.lastProcessedLine.insert(normalizedPath, ReadInterestingLine(normalizedPath, UseLastLine(kind)));
        return;
    }

    ProcessPath(normalizedPath, kind);
}

void ScanDirectory(const QString &directoryPath, WatchKind kind, bool initializeBaselineOnly)
{
    QDir directory(directoryPath);
    const QFileInfoList files = directory.entryInfoList({QStringLiteral("*.txt")}, QDir::Files | QDir::NoSymLinks | QDir::Readable, QDir::Name);
    for (const auto &fileInfo : files) {
        const QString filePath = QDir::fromNativeSeparators(fileInfo.absoluteFilePath());
        const bool known = State().fileKinds.contains(filePath);
        if (known) {
            if (initializeBaselineOnly) {
                State().lastProcessedLine.insert(filePath, ReadInterestingLine(filePath, UseLastLine(kind)));
            }
            continue;
        }

        TrackFile(filePath, kind, initializeBaselineOnly);
    }
}

void RegisterDirectory(const QString &directoryPath, WatchKind kind)
{
    const QString normalizedPath = QDir::fromNativeSeparators(directoryPath);
    if (!EnsureDirectoryExists(normalizedPath)) {
        AppLogger::WriteLog(QStringLiteral("文件监控目录创建失败：%1").arg(QDir::toNativeSeparators(normalizedPath)));
        return;
    }

    auto &state = State();
    state.directoryKinds.insert(normalizedPath, kind);
    if (!state.watcher.directories().contains(normalizedPath)) {
        state.watcher.addPath(normalizedPath);
    }
    ScanDirectory(normalizedPath, kind, true);
}

void RegisterFixedFile(const QString &filePath, WatchKind kind)
{
    const QString normalizedPath = QDir::fromNativeSeparators(filePath);
    if (!EnsureFileExists(normalizedPath)) {
        AppLogger::WriteLog(QStringLiteral("文件监控文件创建失败：%1").arg(QDir::toNativeSeparators(normalizedPath)));
        return;
    }
    TrackFile(normalizedPath, kind, true);
}

bool DeduplicateLine(const QString &filePath, const QString &line)
{
    auto &state = State();
    const QString normalizedPath = QDir::fromNativeSeparators(filePath);
    if (state.lastProcessedLine.value(normalizedPath) == line) {
        return false;
    }
    state.lastProcessedLine.insert(normalizedPath, line);
    return true;
}

bool IsSuppressedPath(const QString &filePath)
{
    auto &state = State();
    const QString normalizedPath = QDir::fromNativeSeparators(filePath);
    const auto it = state.suppressedUntil.constFind(normalizedPath);
    if (it == state.suppressedUntil.constEnd()) {
        return false;
    }

    if (QDateTime::currentDateTime() <= it.value()) {
        return true;
    }

    state.suppressedUntil.remove(normalizedPath);
    return false;
}

bool IsTemplatePlaceholderLine(const QString &text)
{
    const QString trimmed = text.trimmed();
    return trimmed.contains(QStringLiteral("<$"))
           || trimmed.contains(QStringLiteral("@InPut"), Qt::CaseInsensitive)
           || trimmed.contains(QStringLiteral("#IF"), Qt::CaseInsensitive)
           || trimmed.contains(QStringLiteral("#ACT"), Qt::CaseInsensitive);
}

void HandlePaidFile(const QString &filePath)
{
    const QString text = ReadInterestingLine(filePath, false);
    if (text.isEmpty() || !DeduplicateLine(filePath, text)) {
        return;
    }

    const QStringList values = text.split(QLatin1Char('|'));
    if (values.size() != 11) {
        AppLogger::WriteLog(QStringLiteral("代付信息格式不正确：%1").arg(text));
        return;
    }

    const auto config = AppConfig::Load();
    QJsonObject payload;
    payload.insert(QStringLiteral("PartitionName"), values.value(2).trimmed());
    payload.insert(QStringLiteral("PlayerRoleName"), values.value(3).trimmed());
    payload.insert(QStringLiteral("ApplyAmount"), ReadFlexibleDouble(values.value(4)));
    payload.insert(QStringLiteral("BeneficiaryAccountName"), values.value(5).trimmed());
    payload.insert(QStringLiteral("BeneficiaryAccountNumber"), values.value(6).trimmed());
    payload.insert(QStringLiteral("Contact"), values.value(7).trimmed());
    payload.insert(QStringLiteral("Remarks"), values.value(8).trimmed());
    payload.insert(QStringLiteral("PlayerAccount"), values.value(9).trimmed());
    payload.insert(QStringLiteral("Ip"), ResolveClientIp(config));
    payload.insert(QStringLiteral("SecretKey"), config.secretKey);
    payload.insert(QStringLiteral("MachineCode"), State().machineCode);

    PublishJson(&RabbitMqService::PaidApply,
                payload,
                QStringLiteral("收到代付请求信息：%1 已发送").arg(text),
                QStringLiteral("发送代付请求失败"));
}

void HandleGameScanFile(const QString &filePath)
{
    const QString text = ReadInterestingLine(filePath, true);
    if (text.isEmpty() || !DeduplicateLine(filePath, text)) {
        return;
    }

    const QMap<QString, QString> values = ParseKeyValuePairs(text);
    const QString partitionIdText = values.value(QStringLiteral("分区ID")).trimmed();
    const QString account = values.value(QStringLiteral("充值账号")).trimmed();
    if (partitionIdText.isEmpty() || account.isEmpty()) {
        AppLogger::WriteLog(QStringLiteral("游戏扫码缺少关键字段：%1").arg(text));
        return;
    }

    const QString rootPath = QDir(QDir(filePath).rootPath()).path();
    const QString backPath = QDir(rootPath).filePath(QStringLiteral("平台验证/充值二维码/充值图片/%1_%2.txt").arg(partitionIdText, account));

    const auto config = AppConfig::Load();
    double amount = ReadFlexibleDouble(values.value(QStringLiteral("金额")));
    if (!std::isfinite(amount)) {
        amount = 0;
    }
    QJsonObject payload;
    payload.insert(QStringLiteral("PlayerRoleName"), values.value(QStringLiteral("角色名")).trimmed());
    payload.insert(QStringLiteral("PartitionName"), values.value(QStringLiteral("区名")).trimmed());
    payload.insert(QStringLiteral("PlayerAccount"), account);
    payload.insert(QStringLiteral("Amount"), amount);
    payload.insert(QStringLiteral("PlayerIp"), values.value(QStringLiteral("支付IP")).trimmed());
    payload.insert(QStringLiteral("SecretKey"), config.secretKey);
    payload.insert(QStringLiteral("MachineCode"), State().machineCode);
    // 与老网关 FileMonitor / Tenant 热血传奇(CQ) 约定一致：TemplatesType.Rxcq == 1
    payload.insert(QStringLiteral("TemplatesType"), 1);
    payload.insert(QStringLiteral("Remarks"), backPath);
    payload.insert(QStringLiteral("PartitionId"), ReadFlexibleInt(partitionIdText));
    payload.insert(QStringLiteral("ProductId"), ReadFlexibleInt(values.value(QStringLiteral("支付方式"))));
    payload.insert(QStringLiteral("CzType"), values.value(QStringLiteral("充值类型")).trimmed());
    payload.insert(QStringLiteral("IsMobile"), true);

    PublishJson(&RabbitMqService::YouXiSaomaProcess,
                payload,
                QStringLiteral("收到游戏扫码请求信息：%1 已提交").arg(text),
                QStringLiteral("游戏扫码RabbitMQ发送失败"));
}

void HandleWxValidSubmitFile(const QString &filePath)
{
    const QString text = ReadInterestingLine(filePath, false);
    if (text.isEmpty() || !DeduplicateLine(filePath, text)) {
        return;
    }

    if (IsTemplatePlaceholderLine(text)) {
        return;
    }

    const QStringList tokens = text.split(QLatin1Char('|'));
    if (tokens.size() < 5) {
        AppLogger::WriteLog(QStringLiteral("微信密保提交数据格式不正确：%1").arg(text));
        return;
    }

    const QString fileName = QFileInfo(filePath).completeBaseName();
    const int separatorIndex = fileName.indexOf(QLatin1Char('_'));
    if (separatorIndex <= 0) {
        AppLogger::WriteLog(QStringLiteral("微信密保提交数据文件名不符合要求：%1").arg(filePath));
        return;
    }

    const QString rootPath = QDir(QDir(filePath).rootPath()).path();
    const QString operation = tokens.value(0).trimmed();
    QJsonObject payload;
    if (operation == QStringLiteral("绑定")) {
        payload.insert(QStringLiteral("DataType"), QStringLiteral("FSBDWX"));
    } else if (operation == QStringLiteral("解绑")) {
        payload.insert(QStringLiteral("DataType"), QStringLiteral("FSJCWX"));
        payload.insert(QStringLiteral("OpenId"), tokens.value(5).trimmed());
    } else if (operation == QStringLiteral("验证")) {
        payload.insert(QStringLiteral("DataType"), QStringLiteral("FSYZWX"));
        payload.insert(QStringLiteral("OpenId"), tokens.value(5).trimmed());
    } else {
        AppLogger::WriteLog(QStringLiteral("微信密保提交数据首字段错误：%1").arg(text));
        return;
    }

    payload.insert(QStringLiteral("PartitionName"), tokens.value(1).trimmed());
    payload.insert(QStringLiteral("PlayerRoleName"), tokens.value(2).trimmed());
    payload.insert(QStringLiteral("ValidCode"), tokens.value(3).trimmed());
    payload.insert(QStringLiteral("TimeOut"), QStringLiteral("120"));
    payload.insert(QStringLiteral("Remarks"), rootPath);
    payload.insert(QStringLiteral("GameMachineCode"), tokens.value(4).trimmed());
    payload.insert(QStringLiteral("SecretKey"), AppConfig::Load().secretKey);
    payload.insert(QStringLiteral("MachineCode"), State().machineCode);
    payload.insert(QStringLiteral("DiskPath"), rootPath);
    payload.insert(QStringLiteral("Account"), QString());
    payload.insert(QStringLiteral("PartitionId"), ReadFlexibleInt(fileName.left(separatorIndex)));

    PublishJson(&RabbitMqService::WxValidProcess,
                payload,
                QStringLiteral("发送微信密保数据：%1").arg(text),
                QStringLiteral("发送微信密保数据失败"));
}

void HandleTransferSubmitFile(const QString &filePath)
{
    const QString text = ReadInterestingLine(filePath, false);
    if (text.isEmpty() || !DeduplicateLine(filePath, text)) {
        return;
    }

    if (IsTemplatePlaceholderLine(text)) {
        return;
    }

    const QStringList tokens = text.split(QLatin1Char('|'));
    if (tokens.size() < 6) {
        AppLogger::WriteLog(QStringLiteral("微信转区提交数据格式不正确：%1").arg(text));
        return;
    }

    const QString fileName = QFileInfo(filePath).completeBaseName();
    const int separatorIndex = fileName.indexOf(QLatin1Char('_'));
    if (separatorIndex <= 0) {
        AppLogger::WriteLog(QStringLiteral("微信转区提交数据文件名不符合要求：%1").arg(filePath));
        return;
    }

    const QString rootPath = QDir(QDir(filePath).rootPath()).path();
    const QString operation = tokens.value(0).trimmed();
    QJsonObject payload;
    payload.insert(QStringLiteral("DataType"), QStringLiteral("转区点") + operation);
    payload.insert(QStringLiteral("PartitionName"), tokens.value(1).trimmed());
    payload.insert(QStringLiteral("PlayerRoleName"), tokens.value(3).trimmed());
    payload.insert(QStringLiteral("TimeOut"), QStringLiteral("120"));
    payload.insert(QStringLiteral("Remarks"), rootPath);
    payload.insert(QStringLiteral("GameMachineCode"), State().machineCode);
    payload.insert(QStringLiteral("SecretKey"), AppConfig::Load().secretKey);
    payload.insert(QStringLiteral("MachineCode"), State().machineCode);
    payload.insert(QStringLiteral("DiskPath"), rootPath);
    payload.insert(QStringLiteral("Account"), tokens.value(2).trimmed());
    payload.insert(QStringLiteral("OpenId"), tokens.value(4).trimmed());
    payload.insert(QStringLiteral("PartitionId"), ReadFlexibleInt(fileName.left(separatorIndex)));
    if (operation == QStringLiteral("查询") || operation == QStringLiteral("申请")) {
        payload.insert(QStringLiteral("ValidCode"), tokens.value(5).trimmed());
        payload.insert(QStringLiteral("CurrentPointNum"), tokens.value(5).trimmed());
    } else if (operation == QStringLiteral("扣除")) {
        payload.insert(QStringLiteral("ValidCode"), tokens.value(5).trimmed());
        payload.insert(QStringLiteral("TargetPointNum"), tokens.value(6).trimmed());
    } else if (operation == QStringLiteral("转区")) {
        payload.insert(QStringLiteral("TargetPointNum"), tokens.value(5).trimmed());
    }

    PublishJson(&RabbitMqService::WxValidProcess,
                payload,
                QStringLiteral("发送微信转区提交数据：%1").arg(text),
                QStringLiteral("发送微信转区提交数据失败"));
}

void HandleRechargeResultFile(const QString &filePath, bool ptResult)
{
    const QString text = ReadInterestingLine(filePath, true);
    if (text.isEmpty() || !DeduplicateLine(filePath, text)) {
        return;
    }

    static const QRegularExpression regex(QStringLiteral(R"(OrderTime:(?<t>[^|]+)\|SERVERNAME:(?<server>[^|]+)\|USERNAME:(?<username>[^|]+)\|USERID:(?<userid>[^|]+)\|Ip:(?<ip>[^|]+)\|Amount:(?<amount>[^|]+)(?:\|PartitionId:(?<partitionId>.+))?$)"),
                                          QRegularExpression::CaseInsensitiveOption);
    const auto match = regex.match(text.trimmed());
    if (!match.hasMatch()) {
        AppLogger::WriteLog(QStringLiteral("充值结果格式不正确：%1").arg(text));
        return;
    }

    QJsonObject payload;
    payload.insert(QStringLiteral("OrderTime"), match.captured(QStringLiteral("t")).trimmed());
    payload.insert(QStringLiteral("ServerName"), match.captured(QStringLiteral("server")).trimmed());
    payload.insert(QStringLiteral("UserName"), match.captured(QStringLiteral("username")).trimmed());
    payload.insert(QStringLiteral("UserId"), match.captured(QStringLiteral("userid")).trimmed());
    payload.insert(QStringLiteral("Ip"), match.captured(QStringLiteral("ip")).trimmed());
    payload.insert(QStringLiteral("Amount"), ReadFlexibleDouble(match.captured(QStringLiteral("amount")).trimmed()));
    payload.insert(QStringLiteral("PartitionId"), ReadFlexibleInt(match.captured(QStringLiteral("partitionId")).trimmed()));
    payload.insert(QStringLiteral("Raw"), text.trimmed());
    payload.insert(QStringLiteral("MachineCode"), State().machineCode);
    payload.insert(QStringLiteral("SecretKey"), AppConfig::Load().secretKey);
    payload.insert(QStringLiteral("CreateTime"), CurrentTimestamp());

    const QString normalizedPath = QDir::fromNativeSeparators(filePath);
    if (!PublishJson(ptResult ? &RabbitMqService::RanchPTResult : &RabbitMqService::RanchResult,
                     payload,
                     QStringLiteral("已发送充值结果：%1").arg(text),
                     QStringLiteral("发送充值结果失败"))) {
        return;
    }

    if (!RemoveLastNonEmptyLineFromTextFile(normalizedPath)) {
        AppLogger::WriteLog(QStringLiteral("充值结果已发出但未能从文件删除已发送行（可能被占用）：%1").arg(QDir::toNativeSeparators(normalizedPath)));
    }
    State().lastProcessedLine.insert(normalizedPath, ReadInterestingLine(normalizedPath, true));
}

bool ParseAuthFileInfo(const QString &filePath,
                       QString *partitionId,
                       QString *playerName,
                       QString *suffix)
{
    const QString baseName = QFileInfo(filePath).completeBaseName();
    if (baseName.endsWith(QStringLiteral("_WXID"), Qt::CaseInsensitive)) {
        if (suffix) {
            *suffix = QStringLiteral("WXID");
        }
    } else if (baseName.endsWith(QStringLiteral("_WXNC"), Qt::CaseInsensitive)) {
        if (suffix) {
            *suffix = QStringLiteral("WXNC");
        }
    } else {
        return false;
    }

    const QString prefix = baseName.left(baseName.size() - 5);
    const int separatorIndex = prefix.indexOf(QLatin1Char('_'));
    if (separatorIndex <= 0) {
        return false;
    }

    if (partitionId) {
        *partitionId = prefix.left(separatorIndex).trimmed();
    }
    if (playerName) {
        *playerName = prefix.mid(separatorIndex + 1).trimmed();
    }
    return true;
}

void HandleWxValidAuthFile(const QString &filePath)
{
    const QString text = ReadInterestingLine(filePath, false);
    if (text.isEmpty() || !DeduplicateLine(filePath, text)) {
        return;
    }

    QString partitionId;
    QString playerName;
    QString suffix;
    if (!ParseAuthFileInfo(filePath, &partitionId, &playerName, &suffix)) {
        AppLogger::WriteLog(QStringLiteral("微信密保认证数据文件名不符合要求：%1").arg(QDir::toNativeSeparators(filePath)));
        return;
    }

    const QString directoryPath = QFileInfo(filePath).absolutePath();
    const QString openIdPath = QDir(directoryPath).filePath(QStringLiteral("%1_%2_WXID.txt").arg(partitionId, playerName));
    const QString nickNamePath = QDir(directoryPath).filePath(QStringLiteral("%1_%2_WXNC.txt").arg(partitionId, playerName));
    AppLogger::WriteLog(QStringLiteral(
        "微信密保认证数据：触发文件=%1 解析 partitionId=%2 playerName=%3 suffix=%4 配对 WXID=%5 WXNC=%6")
                            .arg(QDir::toNativeSeparators(filePath),
                                 partitionId,
                                 playerName,
                                 suffix,
                                 QDir::toNativeSeparators(openIdPath),
                                 QDir::toNativeSeparators(nickNamePath)));
    const QString openId = ReadInterestingLine(openIdPath, false).trimmed();
    const QString nickName = ReadInterestingLine(nickNamePath, false).trimmed();
    if (openId.isEmpty() || nickName.isEmpty()) {
        AppLogger::WriteLog(QStringLiteral(
            "微信密保认证数据：配对文件内容未就绪，跳过上报 openIdEmpty=%1 nickEmpty=%2（WXID=%3 WXNC=%4）")
                                .arg(openId.isEmpty() ? QStringLiteral("1") : QStringLiteral("0"))
                                .arg(nickName.isEmpty() ? QStringLiteral("1") : QStringLiteral("0"))
                                .arg(QDir::toNativeSeparators(openIdPath))
                                .arg(QDir::toNativeSeparators(nickNamePath)));
        return;
    }

    if (nickName == partitionId) {
        AppLogger::WriteLog(QStringLiteral(
            "微信密保认证数据：WXNC 内容与分区 Id 相同，视为未绑定昵称，跳过上报 partitionId=%1 playerName=%2")
                                .arg(partitionId, playerName));
        return;
    }

    const auto config = AppConfig::Load();
    QJsonObject payload;
    payload.insert(QStringLiteral("PartitionId"), ReadFlexibleInt(partitionId));
    payload.insert(QStringLiteral("GameId"), ReadFlexibleInt(partitionId));
    payload.insert(QStringLiteral("PlayerRoleName"), playerName);
    payload.insert(QStringLiteral("RoleName"), playerName);
    payload.insert(QStringLiteral("OpenId"), openId);
    payload.insert(QStringLiteral("OpenKey"), openId);
    payload.insert(QStringLiteral("openKey"), openId);
    payload.insert(QStringLiteral("NickName"), nickName);
    payload.insert(QStringLiteral("WxName"), nickName);
    payload.insert(QStringLiteral("SecretKey"), config.secretKey);
    payload.insert(QStringLiteral("MachineCode"), State().machineCode);
    payload.insert(QStringLiteral("DiskPath"), QDir(QDir(filePath).rootPath()).path());

    PublishJson(&RabbitMqService::VxCodeEventProcess,
                payload,
                QStringLiteral("已发送微信密保认证数据：%1_%2").arg(partitionId, playerName),
                QStringLiteral("发送微信密保认证数据失败"));
}

void HandleTransferResultFile(const QString &filePath, WatchKind kind)
{
    const QString text = ReadInterestingLine(filePath, false);
    if (text.isEmpty() || !DeduplicateLine(filePath, text)) {
        return;
    }

    if (IsTemplatePlaceholderLine(text)) {
        return;
    }

    const QStringList tokens = text.split(QLatin1Char('|'));
    if (tokens.size() < 6) {
        AppLogger::WriteLog(QStringLiteral("%1格式不正确：%2").arg(WatchKindName(kind), text));
        return;
    }

    const QString baseName = QFileInfo(filePath).completeBaseName();
    const QStringList fileParts = baseName.split(QLatin1Char('_'));
    const QString playerAccount = fileParts.size() >= 3 ? fileParts.constLast().trimmed() : QString();

    const auto config = AppConfig::Load();
    QJsonObject payload;
    payload.insert(QStringLiteral("OperationType"), tokens.value(0).trimmed());
    payload.insert(QStringLiteral("PartionId"), tokens.value(1).trimmed());
    payload.insert(QStringLiteral("PartitionId"), ReadFlexibleInt(tokens.value(1)));
    payload.insert(QStringLiteral("ServerName"), tokens.value(2).trimmed());
    payload.insert(QStringLiteral("UserName"), tokens.value(3).trimmed());
    payload.insert(QStringLiteral("OpenId"), tokens.value(4).trimmed());
    payload.insert(QStringLiteral("PlayerAccount"), playerAccount);
    payload.insert(QStringLiteral("MachineCode"), State().machineCode);
    payload.insert(QStringLiteral("SecretKey"), config.secretKey);

    if (tokens.size() >= 7) {
        payload.insert(QStringLiteral("ExtractPoints"), ReadFlexibleDouble(tokens.value(5)));
        payload.insert(QStringLiteral("BalanceAfter"), ReadFlexibleDouble(tokens.value(6)));
    } else {
        payload.insert(QStringLiteral("BalanceAfter"), ReadFlexibleDouble(tokens.value(5)));
    }

    PublishJson(&RabbitMqService::OperationTransferProcess,
                payload,
                QStringLiteral("已发送%1：%2").arg(WatchKindName(kind), text),
                QStringLiteral("发送%1失败").arg(WatchKindName(kind)));
}

void ProcessPath(const QString &filePath, WatchKind kind)
{
    if (IsSuppressedPath(filePath)) {
        return;
    }

    switch (kind) {
    case WatchKind::PaidFile:
        HandlePaidFile(filePath);
        break;
    case WatchKind::GameScanDirectory:
        HandleGameScanFile(filePath);
        break;
    case WatchKind::WxValidSubmitDirectory:
        HandleWxValidSubmitFile(filePath);
        break;
    case WatchKind::WxValidAuthDirectory:
        HandleWxValidAuthFile(filePath);
        break;
    case WatchKind::TransferSubmitDirectory:
        HandleTransferSubmitFile(filePath);
        break;
    case WatchKind::TransferQueryDirectory:
    case WatchKind::TransferDeductDirectory:
        HandleTransferResultFile(filePath, kind);
        break;
    case WatchKind::RechargeResultFile:
        HandleRechargeResultFile(filePath, false);
        break;
    case WatchKind::PTRechargeResultFile:
        HandleRechargeResultFile(filePath, true);
        break;
    }
}

void InitializeConnections()
{
    static bool connected = false;
    if (connected) {
        return;
    }
    connected = true;

    auto &state = State();
    QObject::connect(&state.watcher, &QFileSystemWatcher::directoryChanged, &state.watcher, [](const QString &path) {
        const auto kind = State().directoryKinds.value(QDir::fromNativeSeparators(path), WatchKind::GameScanDirectory);
        ScanDirectory(path, kind, false);
    });
    QObject::connect(&state.watcher, &QFileSystemWatcher::fileChanged, &state.watcher, [](const QString &path) {
        const QString normalizedPath = QDir::fromNativeSeparators(path);
        const auto kind = State().fileKinds.value(normalizedPath, WatchKind::PaidFile);
        if (QFileInfo::exists(normalizedPath) && !State().watcher.files().contains(normalizedPath)) {
            State().watcher.addPath(normalizedPath);
        }
        ProcessPath(normalizedPath, kind);
    });
}
}

FileMonitorService &FileMonitorService::Instance()
{
    static FileMonitorService instance;
    return instance;
}

FileMonitorService::FileMonitorService()
{
    InitializeConnections();
    State().machineCode = MachineCode().GetRNum().trimmed();
}

void FileMonitorService::Initialize(const AppConfigValues &config)
{
    Stop();
    State().machineCode = MachineCode().GetRNum().trimmed();

    const QStringList scanRoots = ResolveRootPaths(config.yxsmDir);
    const QStringList wxRoots = ResolveRootPaths(config.wxValidPath);
    const QStringList effectiveRechargeRoots = MergeRootPaths(scanRoots, wxRoots);

    auto registerScanRoots = [&](const QStringList &roots) {
        for (const auto &root : roots) {
            const QString normalizedRoot = QDir::fromNativeSeparators(root);
            RegisterDirectory(QDir(normalizedRoot).filePath(QStringLiteral("平台验证/充值二维码/充值提交")), WatchKind::GameScanDirectory);
        }
    };

    auto registerRechargeResultRoots = [&](const QStringList &roots) {
        for (const auto &root : roots) {
            const QString normalizedRoot = QDir::fromNativeSeparators(root);
            RegisterFixedFile(QDir(normalizedRoot).filePath(QStringLiteral("平台验证/充值二维码/充值记录/充值记录.txt")), WatchKind::RechargeResultFile);
        }
    };

    auto registerPtRechargeResultRoots = [&](const QStringList &roots) {
        for (const auto &root : roots) {
            const QString normalizedRoot = QDir::fromNativeSeparators(root);
            RegisterFixedFile(QDir(normalizedRoot).filePath(QStringLiteral("平台验证/充值二维码/普通充值/普通充值.txt")), WatchKind::PTRechargeResultFile);
        }
    };

    auto registerWxRoots = [&](const QStringList &roots) {
        for (const auto &root : roots) {
            const QString normalizedRoot = QDir::fromNativeSeparators(root);
            RegisterDirectory(QDir(normalizedRoot).filePath(QStringLiteral("平台验证/微信密保/提交数据")), WatchKind::WxValidSubmitDirectory);
            RegisterDirectory(QDir(normalizedRoot).filePath(QStringLiteral("平台验证/微信密保/认证数据")), WatchKind::WxValidAuthDirectory);
        }
    };

    auto registerTransferRoots = [&](const QStringList &roots) {
        for (const auto &root : roots) {
            const QString normalizedRoot = QDir::fromNativeSeparators(root);
            RegisterDirectory(QDir(normalizedRoot).filePath(QStringLiteral("平台验证/微信转区/提交转区数据")), WatchKind::TransferSubmitDirectory);
            RegisterDirectory(QDir(normalizedRoot).filePath(QStringLiteral("平台验证/微信转区/转区点查询")), WatchKind::TransferQueryDirectory);
            RegisterDirectory(QDir(normalizedRoot).filePath(QStringLiteral("平台验证/微信转区/转区点扣除")), WatchKind::TransferDeductDirectory);
        }
    };

    if (config.isSm) {
        registerScanRoots(effectiveRechargeRoots);
    }

    registerRechargeResultRoots(effectiveRechargeRoots);
    registerPtRechargeResultRoots(wxRoots);

    if (config.isWeixinMb) {
        registerWxRoots(wxRoots);
    }

    if (config.isWeixinZq) {
        registerTransferRoots(wxRoots);
    }

    for (const auto &paidFile : SplitConfiguredPaths(config.paidDir)) {
        RegisterFixedFile(paidFile, WatchKind::PaidFile);
    }

    State().initialized = true;
}

void FileMonitorService::Stop()
{
    auto &state = State();
    if (!state.watcher.files().isEmpty()) {
        state.watcher.removePaths(state.watcher.files());
    }
    if (!state.watcher.directories().isEmpty()) {
        state.watcher.removePaths(state.watcher.directories());
    }
    state.directoryKinds.clear();
    state.fileKinds.clear();
    state.lastProcessedLine.clear();
    state.suppressedUntil.clear();
    state.initialized = false;
}

void FileMonitorService::SuppressNextChange(const QString &path)
{
    const QString normalizedPath = QDir::fromNativeSeparators(path);
    State().suppressedUntil.insert(normalizedPath, QDateTime::currentDateTime().addSecs(3));
}
