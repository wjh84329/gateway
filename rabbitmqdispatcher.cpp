#include "rabbitmqdispatcher.h"

#include "appconfig.h"
#include "applogger.h"
#include "filemonitorservice.h"
#include "installscriptprocessor.h"
#include "legacycryptoutil.h"
#include "rechargeprocessor.h"
#include "qrcodeencoder.h"
#include "rabbitmqservice.h"

#include <QBuffer>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QRegularExpression>

namespace {
bool HandleRechargeDataObject(const QJsonObject &dataObject, const QString &operationName, bool publishUpdate);

QString SanitizeFileNamePart(QString value)
{
    value.replace(QRegularExpression(QStringLiteral(R"([\\/:*?"<>|])")), QStringLiteral("_"));
    return value.trimmed();
}

bool WriteTextFile(const QString &filePath, const QString &content)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return false;
    }

    file.write(content.toUtf8());
    file.close();
    return true;
}

QString DecodeDataText(const QString &text)
{
    if (text.trimmed().isEmpty()) {
        return {};
    }

    const auto config = AppConfig::Load();
    const QStringList passPhrases = {
        config.secretKey.trimmed(),
        config.signKey.trimmed()
    };

    for (const auto &passPhrase : passPhrases) {
        if (passPhrase.isEmpty()) {
            continue;
        }

        bool ok = false;
        const QString decrypted = LegacyCryptoUtil::DecryptRijndaelBase64(text, passPhrase, &ok);
        if (ok && !decrypted.trimmed().isEmpty()) {
            return decrypted;
        }
    }

    return text;
}

double ReadDoubleField(const QJsonObject &object, const QStringList &names, double defaultValue = 0.0)
{
    for (const auto &name : names) {
        const auto value = object.value(name);
        if (value.isDouble()) {
            return value.toDouble();
        }
        if (value.isString()) {
            bool ok = false;
            const double parsedValue = value.toString().trimmed().toDouble(&ok);
            if (ok) {
                return parsedValue;
            }
        }
    }

    return defaultValue;
}

QString FormatOrderAmountFileName(double amount)
{
    QString text = QString::number(amount, 'f', 1);
    while (text.contains(QLatin1Char('.')) && text.endsWith(QLatin1Char('0'))) {
        text.chop(1);
    }
    if (text.endsWith(QLatin1Char('.'))) {
        text.chop(1);
    }
    return text;
}

double ResolveOrderUpdateRemainderAmount(const QJsonObject &orderObject)
{
    double amount = ReadDoubleField(orderObject, {QStringLiteral("Amount"), QStringLiteral("amount")})
                    + ReadDoubleField(orderObject, {QStringLiteral("IncentiveGiveAmount"), QStringLiteral("incentiveGiveAmount")})
                    + ReadDoubleField(orderObject, {QStringLiteral("ChannelGiveAmount"), QStringLiteral("channelGiveAmount")})
                    + ReadDoubleField(orderObject, {QStringLiteral("RedPacketAmount"), QStringLiteral("redPacketAmount")})
                    + ReadDoubleField(orderObject, {QStringLiteral("TemplateAmount"), QStringLiteral("templateAmount")});

    if (amount >= 1000000.0 || amount < 0.0) {
        return 0.0;
    }

    const QList<double> steps = {100000.0, 10000.0, 1000.0, 100.0, 10.0, 1.0};
    for (const double step : steps) {
        if (amount >= step) {
            const int count = int(amount / step);
            amount -= count * step;
        }
    }

    return amount;
}

bool ReplaceAccountInFile(const QString &filePath,
                          const QString &oldAccount,
                          const QString &newAccount,
                          QString *errorMessage = nullptr)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("无法读取文件：%1").arg(filePath);
        }
        return false;
    }

    const QString text = QString::fromLocal8Bit(file.readAll());
    file.close();

    QStringList lines = text.split(QRegularExpression(QStringLiteral("\\r\\n|\\n|\\r")));
    bool updated = false;
    for (QString &line : lines) {
        if (line.trimmed() == oldAccount.trimmed()) {
            line = newAccount;
            updated = true;
        }
    }

    if (!updated) {
        return false;
    }

    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("无法写入文件：%1").arg(filePath);
        }
        return false;
    }

    file.write(lines.join(QStringLiteral("\r\n")).toLocal8Bit());
    file.close();
    return true;
}

QString DecryptWithSecretKey(const QString &text, bool *ok = nullptr);

QJsonObject ParseSecretKeyObject(const QJsonValue &value, QString *decodedText = nullptr)
{
    if (value.isObject()) {
        if (decodedText) {
            *decodedText = QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
        }
        return value.toObject();
    }

    if (!value.isString()) {
        if (decodedText) {
            decodedText->clear();
        }
        return {};
    }

    bool ok = false;
    QString text = DecryptWithSecretKey(value.toString(), &ok);
    if (!ok || text.trimmed().isEmpty()) {
        text = value.toString().trimmed();
    }
    if (decodedText) {
        *decodedText = text;
    }

    const QJsonDocument document = QJsonDocument::fromJson(text.toUtf8());
    return document.isObject() ? document.object() : QJsonObject{};
}

QJsonArray ParseSecretKeyArray(const QJsonValue &value, QString *decodedText = nullptr)
{
    if (value.isArray()) {
        if (decodedText) {
            *decodedText = QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact));
        }
        return value.toArray();
    }

    if (!value.isString()) {
        if (decodedText) {
            decodedText->clear();
        }
        return {};
    }

    bool ok = false;
    QString text = DecryptWithSecretKey(value.toString(), &ok);
    if (!ok || text.trimmed().isEmpty()) {
        text = value.toString().trimmed();
    }
    if (decodedText) {
        *decodedText = text;
    }

    const QJsonDocument document = QJsonDocument::fromJson(text.toUtf8());
    return document.isArray() ? document.array() : QJsonArray{};
}

QString DecryptWithSecretKey(const QString &text, bool *ok)
{
    const auto config = AppConfig::Load();
    bool localOk = false;
    const QString decrypted = LegacyCryptoUtil::DecryptRijndaelBase64(text, config.secretKey.trimmed(), &localOk).trimmed();
    if (ok) {
        *ok = localOk;
    }
    return decrypted;
}

bool IsLikelyBase64Image(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.startsWith(QStringLiteral("data:image"), Qt::CaseInsensitive)) {
        return true;
    }
    if (trimmed.size() < 100 || trimmed.contains(QStringLiteral("://"))) {
        return false;
    }

    for (const QChar ch : trimmed) {
        if (!(ch.isLetterOrNumber() || ch == QLatin1Char('+') || ch == QLatin1Char('/') || ch == QLatin1Char('=')
              || ch == QLatin1Char('\r') || ch == QLatin1Char('\n'))) {
            return false;
        }
    }
    return true;
}

QByteArray DecodeImagePayload(const QString &text)
{
    QString payload = text.trimmed();
    const int commaIndex = payload.indexOf(QLatin1Char(','));
    if (payload.startsWith(QStringLiteral("data:image"), Qt::CaseInsensitive) && commaIndex >= 0) {
        payload = payload.mid(commaIndex + 1);
    }
    return QByteArray::fromBase64(payload.toLatin1());
}

double GetBlockBlackRatio(const QImage &image, int x, int y, int blockSize)
{
    int blackPixels = 0;
    const int totalPixels = blockSize * blockSize;
    for (int i = x; i < x + blockSize; ++i) {
        for (int j = y; j < y + blockSize; ++j) {
            const QColor pixelColor = image.pixelColor(i, j);
            if (pixelColor.red() < 128 && pixelColor.green() < 128 && pixelColor.blue() < 128) {
                ++blackPixels;
            }
        }
    }
    return totalPixels == 0 ? 0.0 : double(blackPixels) / double(totalPixels);
}

QString GenerateQrCodeStringFromImage(const QImage &image,
                                      const QString &resourceCode,
                                      const QString &imageCode,
                                      int serial,
                                      int xOffset,
                                      int yOffset)
{
    if (image.isNull()) {
        return {};
    }

    QImage croppedImage = image;
    const int minSide = qMin(croppedImage.width(), croppedImage.height());
    if (minSide >= 37) {
        const int blockSize = qMax(1, minSide / 37);
        const int cropWidth = blockSize * 37;
        const int cropHeight = blockSize * 37;
        const int cropX = (croppedImage.width() - cropWidth) / 2;
        const int cropY = (croppedImage.height() - cropHeight) / 2;
        if (cropX >= 0 && cropY >= 0 && cropWidth > 0 && cropHeight > 0) {
            croppedImage = croppedImage.copy(cropX, cropY, cropWidth, cropHeight);
        }
    }

    const int rows = 37;
    const int cols = 37;
    const int blockSizeFinal = qMax(1, qMin(croppedImage.width(), croppedImage.height()) / 37);

    QVector<QVector<int>> bits(rows, QVector<int>(cols, 0));
    for (int i = 0; i < rows; ++i) {
        for (int j = 0; j < cols; ++j) {
            const int startX = j * blockSizeFinal;
            const int startY = i * blockSizeFinal;
            if (startX + blockSizeFinal > croppedImage.width() || startY + blockSizeFinal > croppedImage.height()) {
                bits[i][j] = 0;
                continue;
            }

            const double ratio = GetBlockBlackRatio(croppedImage, startX, startY, blockSizeFinal);
            bits[i][j] = ratio >= 0.5 ? 1 : 0;
        }
    }

    int imgCodeInt = 46;
    if (serial == 3) {
        imgCodeInt = 43;
    } else if (serial == 4) {
        imgCodeInt = 44;
    } else if (serial == 5) {
        imgCodeInt = 45;
    }

    const int smallStep = imgCodeInt - 46;
    const int rowStep = qMax(1, imgCodeInt - 40);
    const int rowJump = cols * rowStep;

    QVector<QVector<int>> nVals(rows, QVector<int>(cols, 0));
    QVector<QVector<int>> groupOffsets(rows, QVector<int>(cols, 0));
    int prevRowEnd = 0;
    bool firstBlackFound = false;

    for (int i = 0; i < rows; ++i) {
        int nRow = i == 0 ? 0 : prevRowEnd - rowJump;
        int lastNInRow = nRow;
        for (int j = 0; j < cols; ++j) {
            const bool bit = bits[i][j] == 1;
            if (!bit) {
                nRow += smallStep;
            }

            int currentN = nRow;
            if (bit && !firstBlackFound) {
                currentN = 0;
                nRow = 0;
                firstBlackFound = true;
            }

            lastNInRow = currentN;
            const int groupOffset = i * rowStep;
            nVals[i][j] = currentN;
            groupOffsets[i][j] = groupOffset;
        }
        prevRowEnd = lastNInRow;
    }

    bool found = false;
    int baseX = 0;
    int baseY = 0;
    for (int i = 0; i < rows && !found; ++i) {
        for (int j = 0; j < cols; ++j) {
            if (bits[i][j] == 1) {
                baseX = nVals[i][j];
                baseY = groupOffsets[i][j];
                found = true;
                break;
            }
        }
    }

    const int shiftX = found ? -baseX : 0;
    const int shiftY = found ? -baseY : 0;
    QString result;
    result.reserve(rows * cols * 24);
    for (int i = 0; i < rows; ++i) {
        for (int j = 0; j < cols; ++j) {
            if (bits[i][j] == 1) {
                const int outX = nVals[i][j] + shiftX + xOffset;
                const int outY = groupOffsets[i][j] + shiftY + yOffset;
                result += QStringLiteral("<Img:%1:%2:%3:%4>").arg(imageCode, resourceCode).arg(outX).arg(outY);
            } else {
                result += QStringLiteral("< >");
            }
        }
    }
    return result;
}

bool WriteGb2312TextFile(const QString &filePath, const QString &content)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return false;
    }

    file.write(content.toLocal8Bit());
    file.close();
    return true;
}

QJsonValue ExtractDataValue(const QJsonObject &object)
{
    return object.contains(QStringLiteral("Data"))
               ? object.value(QStringLiteral("Data"))
               : object.value(QStringLiteral("data"));
}

QJsonObject ParseEmbeddedObject(const QJsonValue &value)
{
    if (value.isObject()) {
        return value.toObject();
    }

    if (value.isString()) {
        const auto document = QJsonDocument::fromJson(DecodeDataText(value.toString()).toUtf8());
        if (document.isObject()) {
            return document.object();
        }
    }

    return {};
}

QJsonArray ParseEmbeddedArray(const QJsonValue &value)
{
    if (value.isArray()) {
        return value.toArray();
    }

    if (value.isString()) {
        const auto document = QJsonDocument::fromJson(DecodeDataText(value.toString()).toUtf8());
        if (document.isArray()) {
            return document.array();
        }
    }

    return {};
}

QString ReadStringField(const QJsonObject &object, const QStringList &names)
{
    for (const auto &name : names) {
        const QString value = object.value(name).toString();
        if (!value.isEmpty()) {
            return value;
        }
    }

    return {};
}

QStringList ResolveConfiguredRootPaths(const QString &configuredPath)
{
    QStringList roots;
    const QStringList paths = configuredPath.split(QLatin1Char('|'), Qt::SkipEmptyParts);
    for (const auto &path : paths) {
        const QString rootPath = QDir(path.trimmed()).rootPath();
        if (!rootPath.isEmpty() && !roots.contains(rootPath, Qt::CaseInsensitive)) {
            roots.append(rootPath);
        }
    }
    return roots;
}

QString BuildSafeTransferFileName(const QJsonObject &dataObject)
{
    const QString openId = SanitizeFileNamePart(ReadStringField(dataObject, {QStringLiteral("OpenId"), QStringLiteral("openId")}));
    const QString partitionId = SanitizeFileNamePart(ReadStringField(dataObject, {QStringLiteral("PartionId"), QStringLiteral("partionId"), QStringLiteral("GameId"), QStringLiteral("gameId")}));
    const QString playerAccount = SanitizeFileNamePart(ReadStringField(dataObject, {QStringLiteral("PlayerAccount"), QStringLiteral("playerAccount"), QStringLiteral("UserId"), QStringLiteral("userId")}));
    return QStringLiteral("%1_%2_%3.txt")
        .arg(openId.isEmpty() ? QStringLiteral("unknown") : openId,
             partitionId.isEmpty() ? QStringLiteral("unknown") : partitionId,
             playerAccount.isEmpty() ? QStringLiteral("unknown") : playerAccount);
}

QString ParseEmbeddedText(const QJsonValue &value)
{
    if (value.isString()) {
        return DecodeDataText(value.toString());
    }

    if (value.isObject()) {
        return QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
    }

    return {};
}

QString NormalizeOrderType(const QJsonObject &object)
{
    static const QMap<int, QString> knownTypes = {
        {1, QStringLiteral("InstallScript")},
        {2, QStringLiteral("Recharge")},
        {3, QStringLiteral("ManualReissue")},
        {4, QStringLiteral("AllPartitionReissue")},
        {5, QStringLiteral("CheckPartition")},
        {6, QStringLiteral("OrderUpdate")},
        {7, QStringLiteral("OrderUpdateSuccess")},
        {10, QStringLiteral("WxValid")},
        {11, QStringLiteral("Info")},
        {12, QStringLiteral("QrCode")},
        {13, QStringLiteral("WeChatCode")},
        {14, QStringLiteral("WeixinTransfer")},
        {15, QStringLiteral("UpdateOrderName")}
    };

    const auto normalizeTypeName = [](QString typeName) {
        typeName = typeName.trimmed();
        if (typeName.contains(QStringLiteral("CheckPartition"), Qt::CaseInsensitive)) {
            return QStringLiteral("CheckPartition");
        }
        if (typeName.contains(QStringLiteral("VxValid"), Qt::CaseInsensitive)
            || typeName.contains(QStringLiteral("WxValid"), Qt::CaseInsensitive)) {
            return QStringLiteral("WxValid");
        }
        if (typeName.contains(QStringLiteral("EasyNetQUpdateOrder"), Qt::CaseInsensitive)) {
            return QStringLiteral("OrderUpdate");
        }
        if (typeName.contains(QStringLiteral("UpdateOrderAccount"), Qt::CaseInsensitive)) {
            return QStringLiteral("OrderUpdate");
        }
        if (typeName.contains(QStringLiteral("YouXiSaoma"), Qt::CaseInsensitive)) {
            return QStringLiteral("QrCode");
        }
        if (typeName.contains(QStringLiteral("WeixinTransfer"), Qt::CaseInsensitive)) {
            return QStringLiteral("WeixinTransfer");
        }
        return typeName;
    };

    const QJsonValue orderTypeValue = object.value(QStringLiteral("OrderType"));
    if (!orderTypeValue.isUndefined() && !orderTypeValue.isNull()) {
        if (orderTypeValue.isString()) {
            const QString orderTypeText = orderTypeValue.toString().trimmed();
            bool ok = false;
            const int numericOrderType = orderTypeText.toInt(&ok);
            return ok ? knownTypes.value(numericOrderType, orderTypeText) : orderTypeText;
        }
        if (orderTypeValue.isDouble()) {
            return knownTypes.value(orderTypeValue.toInt(), QString::number(orderTypeValue.toInt()));
        }
    }

    const QJsonValue lowerOrderTypeValue = object.value(QStringLiteral("orderType"));
    if (lowerOrderTypeValue.isString()) {
        const QString orderTypeText = lowerOrderTypeValue.toString().trimmed();
        bool ok = false;
        const int numericOrderType = orderTypeText.toInt(&ok);
        return ok ? knownTypes.value(numericOrderType, orderTypeText) : orderTypeText;
    }
    if (lowerOrderTypeValue.isDouble()) {
        return knownTypes.value(lowerOrderTypeValue.toInt(), QString::number(lowerOrderTypeValue.toInt()));
    }

    const QJsonObject dataObject = ParseEmbeddedObject(ExtractDataValue(object));
    if (!dataObject.isEmpty()) {
        if (dataObject.contains(QStringLiteral("CheckKey")) || dataObject.contains(QStringLiteral("checkKey"))) {
            return QStringLiteral("CheckPartition");
        }
        if (dataObject.contains(QStringLiteral("openKey"))
            || dataObject.contains(QStringLiteral("OpenKey"))
            || dataObject.contains(QStringLiteral("GameId"))
            || dataObject.contains(QStringLiteral("gameId"))) {
            return QStringLiteral("WxValid");
        }
    }

    for (const QString &fieldName : {QStringLiteral("Type"), QStringLiteral("type"), QStringLiteral("MessageType"), QStringLiteral("messageType")}) {
        const QJsonValue typeValue = object.value(fieldName);
        if (typeValue.isString()) {
            const QString normalizedType = normalizeTypeName(typeValue.toString());
            if (!normalizedType.isEmpty()) {
                return normalizedType;
            }
        }
    }

    return {};
}

void LogKnownOrderType(const QString &orderType)
{
    if (orderType == QLatin1String("InstallScript")) {
        AppLogger::WriteLog(QStringLiteral("收到RabbitMQ消息，类型：InstallScript，待处理创建分区请求"));
    } else if (orderType == QLatin1String("Recharge")) {
        AppLogger::WriteLog(QStringLiteral("收到RabbitMQ消息，类型：Recharge，待处理充值请求"));
    } else if (orderType == QLatin1String("ManualReissue")) {
        AppLogger::WriteLog(QStringLiteral("收到RabbitMQ消息，类型：ManualReissue，待处理手动补发请求"));
    } else if (orderType == QLatin1String("AllPartitionReissue")) {
        AppLogger::WriteLog(QStringLiteral("收到RabbitMQ消息，类型：AllPartitionReissue，待处理整区补发请求"));
    } else if (orderType == QLatin1String("CheckPartition")) {
        AppLogger::WriteLog(QStringLiteral("收到RabbitMQ消息，类型：CheckPartition，待处理分区检测请求"));
    } else if (orderType == QLatin1String("WxValid")) {
        AppLogger::WriteLog(QStringLiteral("收到RabbitMQ消息，类型：WxValid，待处理微信验证请求"));
    } else if (orderType == QLatin1String("Info")) {
        AppLogger::WriteLog(QStringLiteral("收到RabbitMQ消息，类型：Info"));
    } else if (orderType == QLatin1String("OrderUpdate")) {
        AppLogger::WriteLog(QStringLiteral("收到RabbitMQ消息，类型：OrderUpdate，待处理订单更新请求"));
    } else if (orderType == QLatin1String("OrderUpdateSuccess")) {
        AppLogger::WriteLog(QStringLiteral("收到RabbitMQ消息，类型：OrderUpdateSuccess"));
    } else if (orderType == QLatin1String("QrCode")) {
        AppLogger::WriteLog(QStringLiteral("收到RabbitMQ消息，类型：QrCode，待处理二维码请求"));
    } else if (orderType == QLatin1String("WeixinTransfer")) {
        AppLogger::WriteLog(QStringLiteral("收到RabbitMQ消息，类型：WeixinTransfer，待处理微信转区请求"));
    } else {
        AppLogger::WriteLog(QStringLiteral("收到RabbitMQ消息，类型：%1").arg(orderType));
    }
}

bool HandleCheckPartitionMessage(const QJsonObject &object)
{
    const QJsonValue dataValue = ExtractDataValue(object);
    const QJsonObject dataObject = ParseEmbeddedObject(dataValue);
    const QString checkKey = ReadStringField(dataObject, {QStringLiteral("CheckKey"), QStringLiteral("checkKey")});

    AppLogger::WriteLog(QStringLiteral("收到分区检测请求,检测Key：%1").arg(checkKey.isEmpty() ? QStringLiteral("<empty>") : checkKey));

    QJsonObject responseObject;
    responseObject.insert(QStringLiteral("CheckKey"), checkKey);
    responseObject.insert(QStringLiteral("CheckResult"), QStringLiteral("Success"));

    QString errorMessage;
    if (!RabbitMqService::UpdateCheck(AppConfig::Load(),
                                      QString::fromUtf8(QJsonDocument(responseObject).toJson(QJsonDocument::Compact)),
                                      &errorMessage)) {
        AppLogger::WriteLog(QStringLiteral("发送检测成功请求失败：%1").arg(errorMessage));
        return false;
    }

    AppLogger::WriteLog(QStringLiteral("发送检测成功请求"));
    return true;
}

bool HandleInfoMessage(const QJsonObject &object)
{
    const QJsonValue dataValue = ExtractDataValue(object);
    const QString dataText = ParseEmbeddedText(dataValue);
    AppLogger::WriteLog(dataText.isEmpty()
                            ? QStringLiteral("收到Info消息")
                            : QStringLiteral("Info消息：%1").arg(dataText));
    return true;
}

bool HandleOrderUpdateSuccessMessage(const QJsonObject &object)
{
    const QJsonValue dataValue = ExtractDataValue(object);
    const QString dataText = ParseEmbeddedText(dataValue);
    AppLogger::WriteLog(dataText.isEmpty()
                            ? QStringLiteral("收到OrderUpdateSuccess消息")
                            : QStringLiteral("OrderUpdateSuccess：%1").arg(dataText));
    return true;
}

bool HandleInstallScriptMessage(const QJsonObject &object)
{
    const QJsonValue dataValue = ExtractDataValue(object);
    QJsonObject dataObject;
    QString decryptedText;

    if (dataValue.isObject()) {
        dataObject = dataValue.toObject();
        decryptedText = QString::fromUtf8(QJsonDocument(dataObject).toJson(QJsonDocument::Compact));
    } else if (dataValue.isString()) {
        const auto config = AppConfig::Load();
        bool ok = false;
        decryptedText = LegacyCryptoUtil::DecryptRijndaelBase64(dataValue.toString(), config.secretKey.trimmed(), &ok).trimmed();
        if (!ok || decryptedText.isEmpty()) {
            decryptedText = dataValue.toString().trimmed();
        }

        const auto document = QJsonDocument::fromJson(decryptedText.toUtf8());
        if (document.isObject()) {
            dataObject = document.object();
        }
    }

    if (dataObject.isEmpty()) {
        AppLogger::WriteLog(decryptedText.isEmpty()
                                ? QStringLiteral("收到创建分区请求")
                                : QStringLiteral("收到创建分区请求：%1").arg(decryptedText));
        return false;
    }

    QString errorMessage;
    const bool installOk = InstallScriptProcessor::Process(dataObject, &errorMessage);

    const QJsonObject partitionObject = dataObject.value(QStringLiteral("Partition")).toObject();
    const QString partitionName = ReadStringField(partitionObject, {QStringLiteral("Name"), QStringLiteral("name")});
    const QString scriptPath = ReadStringField(partitionObject, {QStringLiteral("ScriptPath"), QStringLiteral("scriptPath")});
    AppLogger::WriteLog(QStringLiteral("创建分区 【%1->%2】 %3")
                            .arg(partitionName.isEmpty() ? QStringLiteral("<empty>") : partitionName,
                                 scriptPath.isEmpty() ? QStringLiteral("<empty>") : scriptPath,
                                 installOk ? QStringLiteral("成功") : QStringLiteral("失败")));

    if (!installOk) {
        if (!errorMessage.isEmpty()) {
            AppLogger::WriteLog(QStringLiteral("创建分区请求处理失败：%1").arg(errorMessage));
        }
        return false;
    }

    return true;
}

bool HandleRechargeMessage(const QJsonObject &object, const QString &operationName, bool publishUpdate)
{
    QString decodedText;
    const QJsonObject dataObject = ParseSecretKeyObject(ExtractDataValue(object), &decodedText);
    if (dataObject.isEmpty()) {
        AppLogger::WriteLog(decodedText.isEmpty()
                                ? QStringLiteral("%1处理失败：无法解析充值消息").arg(operationName)
                                : QStringLiteral("%1处理失败：无法解析充值消息 %2").arg(operationName, decodedText.left(300)));
    }
    return HandleRechargeDataObject(dataObject, operationName, publishUpdate);
}

bool HandleRechargeDataObject(const QJsonObject &dataObject, const QString &operationName, bool publishUpdate)
{
    if (dataObject.isEmpty()) {
        AppLogger::WriteLog(QStringLiteral("%1处理失败：消息内容为空").arg(operationName));
        return false;
    }

    const QString orderNumber = ReadStringField(dataObject, {QStringLiteral("OrderNumber"), QStringLiteral("orderNumber")});
    const QString playerAccount = ReadStringField(dataObject, {QStringLiteral("PlayerAccount"), QStringLiteral("playerAccount")});
    const QString partitionName = ReadStringField(dataObject, {QStringLiteral("PartitionName"), QStringLiteral("partitionName")});
    const QString randomCode = ReadStringField(dataObject, {QStringLiteral("RandomCode"), QStringLiteral("randomCode")});

    AppLogger::WriteLog(QStringLiteral("收到%1请求：订单=%2，账号=%3，分区=%4")
                            .arg(operationName,
                                 orderNumber.isEmpty() ? QStringLiteral("<empty>") : orderNumber,
                                 playerAccount.isEmpty() ? QStringLiteral("<empty>") : playerAccount,
                                 partitionName.isEmpty() ? QStringLiteral("<empty>") : partitionName));

    QString errorMessage;
    if (!RechargeProcessor::Process(dataObject, operationName, &errorMessage)) {
        AppLogger::WriteLog(QStringLiteral("%1处理失败：%2").arg(operationName, errorMessage.isEmpty() ? QStringLiteral("未知错误") : errorMessage));
        return false;
    }

    if (publishUpdate) {
        const auto config = AppConfig::Load();
        QJsonObject responseObject;
        responseObject.insert(QStringLiteral("OrderNumber"), orderNumber);
        responseObject.insert(QStringLiteral("RandomCode"), randomCode);
        responseObject.insert(QStringLiteral("SecretKey"), config.secretKey);

        QString updateErrorMessage;
        if (!RabbitMqService::UpdateOrder(config,
                                          QString::fromUtf8(QJsonDocument(responseObject).toJson(QJsonDocument::Compact)),
                                          &updateErrorMessage)) {
            AppLogger::WriteLog(QStringLiteral("发送订单状态更新失败：%1").arg(updateErrorMessage));
            return false;
        }

        AppLogger::WriteLog(QStringLiteral("发送订单状态更新成功：订单=%1").arg(orderNumber.isEmpty() ? QStringLiteral("<empty>") : orderNumber));
    }

    return true;
}

bool HandleAllPartitionReissueMessage(const QJsonObject &object)
{
    const auto config = AppConfig::Load();
    if (!config.isOpenOrderReissue) {
        AppLogger::WriteLog(QStringLiteral("有整区补发订单请求，但网关设置为不可补发"));
        return true;
    }

    AppLogger::WriteLog(QStringLiteral("收到整区补发订单"));
    QString decodedText;
    const QJsonArray dataArray = ParseSecretKeyArray(ExtractDataValue(object), &decodedText);
    if (dataArray.isEmpty() && !decodedText.trimmed().startsWith(QLatin1Char('['))) {
        AppLogger::WriteLog(decodedText.isEmpty()
                                ? QStringLiteral("整区补发处理失败：无法解析补发数组消息")
                                : QStringLiteral("整区补发处理失败：无法解析补发数组消息 %1").arg(decodedText.left(300)));
        return false;
    }
    AppLogger::WriteLog(QStringLiteral("收到整区补发请求，共%1条").arg(static_cast<int>(dataArray.size())));

    bool allSucceeded = true;
    for (const auto &item : dataArray) {
        if (!item.isObject()) {
            allSucceeded = false;
            continue;
        }

        if (!HandleRechargeDataObject(item.toObject(), QStringLiteral("补发"), false)) {
            allSucceeded = false;
        }
    }

    return allSucceeded;
}

bool HandleManualReissueMessage(const QJsonObject &object)
{
    const auto config = AppConfig::Load();
    if (!config.isOpenOrderReissue) {
        AppLogger::WriteLog(QStringLiteral("有手动补发订单请求，但网关设置为不可补发"));
        return true;
    }

    AppLogger::WriteLog(QStringLiteral("收到手动补发订单"));
    QString decodedText;
    const QJsonObject dataObject = ParseSecretKeyObject(ExtractDataValue(object), &decodedText);
    if (dataObject.isEmpty()) {
        AppLogger::WriteLog(decodedText.isEmpty()
                                ? QStringLiteral("手动补发处理失败：无法解析补发消息")
                                : QStringLiteral("手动补发处理失败：无法解析补发消息 %1").arg(decodedText.left(300)));
    }
    return HandleRechargeDataObject(dataObject, QStringLiteral("补发"), false);
}

bool HandleOrderUpdateMessage(const QJsonObject &object)
{
    const QJsonObject dataObject = ParseEmbeddedObject(ExtractDataValue(object));
    const QString newAccount = ReadStringField(dataObject, {QStringLiteral("newAccount"), QStringLiteral("NewAccount")});
    const QJsonObject partitionObject = dataObject.value(QStringLiteral("Partition")).toObject();
    const QJsonObject templateObject = dataObject.value(QStringLiteral("Template")).toObject();
    const QJsonObject orderObject = dataObject.value(QStringLiteral("Order")).toObject();
    const QString orderNumber = ReadStringField(orderObject, {QStringLiteral("OrderNumber"), QStringLiteral("orderNumber")});
    const QString playerAccount = ReadStringField(orderObject, {QStringLiteral("PlayerAccount"), QStringLiteral("playerAccount")});

    AppLogger::WriteLog(QStringLiteral("收到订单更新请求：订单=%1，旧账号=%2，新账号=%3")
                            .arg(orderNumber.isEmpty() ? QStringLiteral("<empty>") : orderNumber,
                                 playerAccount.isEmpty() ? QStringLiteral("<empty>") : playerAccount,
                                 newAccount.isEmpty() ? QStringLiteral("<empty>") : newAccount));

    const QString scriptPath = ReadStringField(partitionObject, {QStringLiteral("ScriptPath"), QStringLiteral("scriptPath")});
    const QString payDir = ReadStringField(templateObject, {QStringLiteral("PayDir"), QStringLiteral("payDir")});
    const QString currencyName = ReadStringField(templateObject, {QStringLiteral("CurrencyName"), QStringLiteral("currencyName")});
    const QString payFolder = QDir(scriptPath).filePath(QStringLiteral("Mir200/Envir/QuestDiary/%1/充值%2/%2").arg(payDir, currencyName));
    const double amount = ResolveOrderUpdateRemainderAmount(orderObject);
    const QString targetFile = QDir(payFolder).filePath(QStringLiteral("%1.txt").arg(FormatOrderAmountFileName(amount)));

    bool replaced = false;
    QString fileErrorMessage;
    if (!playerAccount.trimmed().isEmpty() && !newAccount.trimmed().isEmpty() && QFileInfo::exists(targetFile)) {
        replaced = ReplaceAccountInFile(targetFile, playerAccount, newAccount, &fileErrorMessage);
        if (replaced) {
            AppLogger::WriteLog(QStringLiteral("订单[%1] 账号替换成功：%2 -> %3 文件：%4")
                                    .arg(orderNumber, playerAccount, newAccount, QDir::toNativeSeparators(targetFile)));
        }
    }

    if (!replaced && QDir(payFolder).exists()) {
        QDirIterator iterator(payFolder, {QStringLiteral("*.txt")}, QDir::Files, QDirIterator::Subdirectories);
        while (iterator.hasNext()) {
            const QString filePath = iterator.next();
            if (ReplaceAccountInFile(filePath, playerAccount, newAccount, &fileErrorMessage)) {
                replaced = true;
                AppLogger::WriteLog(QStringLiteral("订单[%1] 账号替换成功：%2 -> %3 文件：%4")
                                        .arg(orderNumber, playerAccount, newAccount, QDir::toNativeSeparators(filePath)));
                break;
            }
        }
    }

    if (!replaced) {
        AppLogger::WriteLog(QStringLiteral("订单[%1] 修改账号未找到匹配行：%2，在目录：%3")
                                .arg(orderNumber,
                                     playerAccount.isEmpty() ? QStringLiteral("<empty>") : playerAccount,
                                     QDir::toNativeSeparators(payFolder)));
    }

    const auto config = AppConfig::Load();
    QJsonObject responseObject;
    responseObject.insert(QStringLiteral("OrderNumber"), orderNumber);
    responseObject.insert(QStringLiteral("NewAccount"), newAccount);

    QString errorMessage;
    if (!RabbitMqService::UpdateOrderAccount(config,
                                             QString::fromUtf8(QJsonDocument(responseObject).toJson(QJsonDocument::Compact)),
                                             &errorMessage)) {
        AppLogger::WriteLog(QStringLiteral("发送订单账号更新结果失败：%1").arg(errorMessage));
        return false;
    }

    AppLogger::WriteLog(QStringLiteral("发送订单账号更新结果成功：订单=%1").arg(orderNumber.isEmpty() ? QStringLiteral("<empty>") : orderNumber));
    return true;
}

bool HandleQrCodeMessage(const QJsonObject &object)
{
    const QJsonObject dataObject = ParseEmbeddedObject(ExtractDataValue(object));
    const QString path = ReadStringField(dataObject, {QStringLiteral("Path"), QStringLiteral("path")});
    AppLogger::WriteLog(path.isEmpty()
                            ? QStringLiteral("收到二维码请求")
                            : QStringLiteral("收到二维码请求，输出路径：%1").arg(path));

    const QString qrCodeText = ReadStringField(dataObject, {QStringLiteral("QrCode"), QStringLiteral("qrCode")});
    const QJsonObject scanObject = dataObject.value(QStringLiteral("scan")).toObject();
    QString resourceCode = ReadStringField(scanObject, {QStringLiteral("ResourceCode"), QStringLiteral("resourceCode")});
    if (resourceCode.isEmpty()) {
        resourceCode = ReadStringField(dataObject, {QStringLiteral("ResourceCode"), QStringLiteral("resourceCode")});
    }
    QString imageCode = ReadStringField(scanObject, {QStringLiteral("ImageCode"), QStringLiteral("imageCode")});
    if (imageCode.isEmpty()) {
        imageCode = ReadStringField(dataObject, {QStringLiteral("ImageCode"), QStringLiteral("imageCode")});
    }
    const int serial = scanObject.value(QStringLiteral("Serial")).toInt(dataObject.value(QStringLiteral("Serial")).toInt(0));
    const int xOffset = scanObject.value(QStringLiteral("XOffset")).toInt(dataObject.value(QStringLiteral("XOffset")).toInt(10));
    const int yOffset = scanObject.value(QStringLiteral("YOffset")).toInt(dataObject.value(QStringLiteral("YOffset")).toInt(10));

    QString qrString;
    if (IsLikelyBase64Image(qrCodeText)) {
        QImage image;
        image.loadFromData(DecodeImagePayload(qrCodeText));
        qrString = GenerateQrCodeStringFromImage(image,
                                                 resourceCode.isEmpty() ? QStringLiteral("46") : resourceCode,
                                                 imageCode.isEmpty() ? QStringLiteral("0") : imageCode,
                                                 serial,
                                                 xOffset,
                                                 yOffset);
    } else if (!qrCodeText.trimmed().isEmpty()) {
        QString qrErrorMessage;
        qrString = QrCodeEncoder::GenerateLegacyMirText(qrCodeText.trimmed(),
                                                        resourceCode.isEmpty() ? QStringLiteral("46") : resourceCode,
                                                        imageCode.isEmpty() ? QStringLiteral("0") : imageCode,
                                                        serial,
                                                        xOffset,
                                                        yOffset,
                                                        &qrErrorMessage);
        if (qrString.isEmpty()) {
            AppLogger::WriteLog(QStringLiteral("二维码纯 C++ 生成失败：%1").arg(qrErrorMessage));
        }
    }

    if (qrString.isEmpty()) {
        AppLogger::WriteLog(QStringLiteral("二维码处理失败：无法生成二维码字符串"));
        return false;
    }

    const QString normalizedPath = QDir::fromNativeSeparators(path);
    const QString absolutePath = QFileInfo(normalizedPath).absoluteFilePath();
    QString driveRoot;
    if (absolutePath.size() >= 3 && absolutePath.at(1) == QLatin1Char(':') && absolutePath.at(2) == QLatin1Char('/')) {
        driveRoot = absolutePath.left(3);
    } else {
        driveRoot = QDir::rootPath();
    }

    const QStringList outputPaths = {
        absolutePath,
        QDir(driveRoot).filePath(QStringLiteral("平台验证/微信转区/本服公众号.txt"))
    };

    for (const auto &outputPath : outputPaths) {
        if (!QDir().mkpath(QFileInfo(outputPath).absolutePath()) || !WriteGb2312TextFile(outputPath, qrString)) {
            AppLogger::WriteLog(QStringLiteral("二维码处理失败：写入文件失败 %1").arg(outputPath));
            return false;
        }
    }

    AppLogger::WriteLog(QStringLiteral("二维码文件已写入：%1 和 %2").arg(outputPaths.at(0), outputPaths.at(1)));
    return true;
}

bool HandleWeixinTransferMessage(const QJsonObject &object)
{
    QString decodedText;
    const QJsonObject dataObject = ParseSecretKeyObject(ExtractDataValue(object), &decodedText);
    if (dataObject.isEmpty()) {
        AppLogger::WriteLog(decodedText.isEmpty()
                                ? QStringLiteral("微信转区处理失败：无法解析消息")
                                : QStringLiteral("微信转区处理失败：无法解析消息 %1").arg(decodedText.left(300)));
        return false;
    }

    const QString operationType = ReadStringField(dataObject, {QStringLiteral("OperationType"), QStringLiteral("operationType")});
    const QString openId = ReadStringField(dataObject, {QStringLiteral("OpenId"), QStringLiteral("openId")});
    const QString partitionId = ReadStringField(dataObject, {QStringLiteral("PartionId"), QStringLiteral("partionId")});
    const QString serverName = ReadStringField(dataObject, {QStringLiteral("ServerName"), QStringLiteral("serverName")});
    const QString userName = ReadStringField(dataObject, {QStringLiteral("UserName"), QStringLiteral("userName")});
    const QString extractPoints = ReadStringField(dataObject, {QStringLiteral("ExtractPoints"), QStringLiteral("extractPoints")});
    const QString balanceAfter = ReadStringField(dataObject, {QStringLiteral("BalanceAfter"), QStringLiteral("balanceAfter")});

    AppLogger::WriteLog(QStringLiteral("收到微信转区请求：操作=%1，分区=%2，OpenId=%3")
                            .arg(operationType.isEmpty() ? QStringLiteral("<empty>") : operationType,
                                 partitionId.isEmpty() ? QStringLiteral("<empty>") : partitionId,
                                 openId.isEmpty() ? QStringLiteral("<empty>") : openId));

    const auto config = AppConfig::Load();
    const QStringList rootPaths = ResolveConfiguredRootPaths(config.wxValidPath);
    if (rootPaths.isEmpty()) {
        AppLogger::WriteLog(QStringLiteral("微信转区处理失败：WxValidPath 未配置"));
        return false;
    }

    const bool isQueryOperation = operationType.contains(QStringLiteral("查询"), Qt::CaseInsensitive);
    const QString subFolder = isQueryOperation ? QStringLiteral("转区点查询") : QStringLiteral("转区点扣除");
    const QString fileContent = isQueryOperation
                                    ? QStringLiteral("%1|%2|%3|%4|%5|%6")
                                          .arg(operationType, partitionId, serverName, userName, openId, balanceAfter)
                                    : QStringLiteral("%1|%2|%3|%4|%5|%6|%7")
                                          .arg(operationType, partitionId, serverName, userName, openId, extractPoints, balanceAfter);
    const QString fileName = BuildSafeTransferFileName(dataObject);

    for (const auto &rootPath : rootPaths) {
        const QString targetDir = QDir(rootPath).filePath(QStringLiteral("平台验证/微信转区/%1").arg(subFolder));
        if (!QDir().mkpath(targetDir)) {
            continue;
        }

        const QString targetFilePath = QDir(targetDir).filePath(fileName);
        if (WriteGb2312TextFile(targetFilePath, fileContent)) {
            AppLogger::WriteLog(QStringLiteral("已写入转区文件到 %1 内容：%2")
                                    .arg(QDir::toNativeSeparators(targetFilePath), fileContent));
            return true;
        }
    }

    AppLogger::WriteLog(QStringLiteral("微信转区处理失败：写入转区文件失败"));
    return false;
}

bool HandleWxValidMessage(const QJsonObject &object)
{
    const QJsonValue dataValue = ExtractDataValue(object);
    QString decryptedText;
    QJsonObject dataObject;
    if (dataValue.isObject()) {
        dataObject = dataValue.toObject();
        decryptedText = QString::fromUtf8(QJsonDocument(dataObject).toJson(QJsonDocument::Compact));
    } else if (dataValue.isString()) {
        bool ok = false;
        decryptedText = DecryptWithSecretKey(dataValue.toString(), &ok);
        if (!ok || decryptedText.isEmpty()) {
            AppLogger::WriteLog(QStringLiteral("微信验证处理失败：解密失败"));
            return false;
        }
        const auto document = QJsonDocument::fromJson(decryptedText.toUtf8());
        if (!document.isObject()) {
            AppLogger::WriteLog(QStringLiteral("微信验证处理失败：解密后不是有效 JSON"));
            return false;
        }
        dataObject = document.object();
    }

    const QString partitionId = ReadStringField(dataObject, {QStringLiteral("GameId"), QStringLiteral("gameId"), QStringLiteral("PartionId"), QStringLiteral("partionId")});
    const QString playerName = SanitizeFileNamePart(ReadStringField(dataObject, {QStringLiteral("RoleName"), QStringLiteral("roleName"), QStringLiteral("PlayerRoleName"), QStringLiteral("playerRoleName"), QStringLiteral("UserName"), QStringLiteral("userName")}));
    const QString openKey = ReadStringField(dataObject, {QStringLiteral("openKey"), QStringLiteral("OpenKey")});

    AppLogger::WriteLog(QStringLiteral("收到微信验证请求：分区=%1，角色=%2")
                            .arg(partitionId.isEmpty() ? QStringLiteral("<empty>") : partitionId,
                                 playerName.isEmpty() ? QStringLiteral("<empty>") : playerName));

    const auto config = AppConfig::Load();
    if (config.wxValidPath.trimmed().isEmpty()) {
        AppLogger::WriteLog(QStringLiteral("微信验证处理失败：WxValidPath 未配置"));
        return false;
    }

    if (partitionId.isEmpty() || playerName.isEmpty()) {
        AppLogger::WriteLog(QStringLiteral("微信验证处理失败：缺少分区或角色信息"));
        return false;
    }

    const QString rootPath = QDir(config.wxValidPath).rootPath();
    const QString authDirPath = QDir(rootPath).filePath(QStringLiteral("平台验证/微信密保/认证数据"));
    if (!QDir().mkpath(authDirPath)) {
        AppLogger::WriteLog(QStringLiteral("微信验证处理失败：无法创建认证目录 %1").arg(authDirPath));
        return false;
    }

    const QString openKeyFilePath = QDir(authDirPath).filePath(QStringLiteral("%1_%2_WXID.txt").arg(partitionId, playerName));
    const QString gameIdFilePath = QDir(authDirPath).filePath(QStringLiteral("%1_%2_WXNC.txt").arg(partitionId, playerName));

    FileMonitorService::Instance().SuppressNextChange(openKeyFilePath);
    FileMonitorService::Instance().SuppressNextChange(gameIdFilePath);

    if (!WriteGb2312TextFile(openKeyFilePath, openKey) || !WriteGb2312TextFile(gameIdFilePath, partitionId)) {
        AppLogger::WriteLog(QStringLiteral("微信验证处理失败：写入认证文件失败"));
        return false;
    }

    AppLogger::WriteLog(QStringLiteral("微信验证认证文件已写入：%1 和 %2").arg(openKeyFilePath, gameIdFilePath));
    return true;
}
}

namespace RabbitMqDispatcher {
bool HandleMessage(const QString &payload)
{
    const auto document = QJsonDocument::fromJson(payload.toUtf8());
    if (!document.isObject()) {
        AppLogger::WriteLog(QStringLiteral("收到RabbitMQ消息：%1").arg(payload));
        return true;
    }

    const QJsonObject object = document.object();
    const QString orderType = NormalizeOrderType(object);
    if (!orderType.isEmpty()) {
        LogKnownOrderType(orderType);
        if (orderType == QLatin1String("CheckPartition")) {
            return HandleCheckPartitionMessage(object);
        }
        if (orderType == QLatin1String("Info")) {
            return HandleInfoMessage(object);
        }
        if (orderType == QLatin1String("OrderUpdateSuccess")) {
            return HandleOrderUpdateSuccessMessage(object);
        }
        if (orderType == QLatin1String("InstallScript")) {
            return HandleInstallScriptMessage(object);
        }
        if (orderType == QLatin1String("Recharge")) {
            return HandleRechargeMessage(object, QStringLiteral("充值"), true);
        }
        if (orderType == QLatin1String("ManualReissue")) {
            return HandleManualReissueMessage(object);
        }
        if (orderType == QLatin1String("AllPartitionReissue")) {
            return HandleAllPartitionReissueMessage(object);
        }
        if (orderType == QLatin1String("OrderUpdate")) {
            return HandleOrderUpdateMessage(object);
        }
        if (orderType == QLatin1String("QrCode")) {
            return HandleQrCodeMessage(object);
        }
        if (orderType == QLatin1String("WeixinTransfer")) {
            return HandleWeixinTransferMessage(object);
        }
        if (orderType == QLatin1String("WxValid")) {
            return HandleWxValidMessage(object);
        }
    } else {
        AppLogger::WriteLog(QStringLiteral("收到RabbitMQ消息：%1").arg(payload));
    }

    return true;
}
}
