#include "rabbitmqdispatcher.h"

#include "appconfig.h"
#include "applogger.h"
#include "filemonitorservice.h"
#include "installscriptprocessor.h"
#include "legacycryptoutil.h"
#include "rechargeprocessor.h"
#include "partitionpathcache.h"
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

int ReadIntField(const QJsonObject &object, const QStringList &names, int defaultValue = 0)
{
    for (const auto &name : names) {
        const QJsonValue value = object.value(name);
        if (value.isDouble()) {
            return int(value.toDouble());
        }
        if (value.isBool()) {
            return value.toBool() ? 1 : 0;
        }
        if (value.isString()) {
            bool ok = false;
            const int parsed = value.toString().trimmed().toInt(&ok);
            if (ok) {
                return parsed;
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
    const QString text = DecryptWithSecretKey(value.toString(), &ok);
    if (!ok || text.trimmed().isEmpty()) {
        if (decodedText) {
            decodedText->clear();
        }
        return {};
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
    const QString text = DecryptWithSecretKey(value.toString(), &ok);
    if (!ok || text.trimmed().isEmpty()) {
        if (decodedText) {
            decodedText->clear();
        }
        return {};
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

/// 与旧版 Gateway.cs ManualReissue 分支一致：仅接受解密后的 JSON 对象，解密失败或为空时不回退到明文 Data。
QJsonObject ParseManualReissuePayload(const QJsonValue &value, QString *decodedText, bool *decryptFailedOut)
{
    if (decryptFailedOut) {
        *decryptFailedOut = false;
    }
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

    bool decryptOk = false;
    const QString text = DecryptWithSecretKey(value.toString(), &decryptOk);
    if (!decryptOk) {
        if (decryptFailedOut) {
            *decryptFailedOut = true;
        }
        if (decodedText) {
            decodedText->clear();
        }
        return {};
    }
    if (text.trimmed().isEmpty()) {
        if (decodedText) {
            decodedText->clear();
        }
        return {};
    }
    if (decodedText) {
        *decodedText = text;
    }
    const QJsonDocument document = QJsonDocument::fromJson(text.toUtf8());
    return document.isObject() ? document.object() : QJsonObject{};
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

/// JSON 标量转字符串：兼容数值/布尔（与 ReadStringField 仅识别字符串不同；Tenant 下发的 PartitionId、GameId 多为数字）
QString ReadJsonScalarAsQString(const QJsonObject &object, const QStringList &names)
{
    for (const auto &name : names) {
        if (!object.contains(name)) {
            continue;
        }
        const QJsonValue v = object.value(name);
        if (v.isNull() || v.isUndefined()) {
            continue;
        }
        const QString s = v.toVariant().toString().trimmed();
        if (!s.isEmpty()) {
            return s;
        }
    }
    return {};
}

bool PublishManualReissueResult(const QJsonObject &dataObject, bool reissueOk)
{
    const auto config = AppConfig::Load();
    const QString orderNumber = ReadStringField(dataObject, {QStringLiteral("OrderNumber"), QStringLiteral("orderNumber")});
    const QString randomCode = ReadStringField(dataObject, {QStringLiteral("RandomCode"), QStringLiteral("randomCode")});

    QJsonObject responseObject;
    responseObject.insert(QStringLiteral("OrderNumber"), orderNumber);
    responseObject.insert(QStringLiteral("RandomCode"), randomCode);
    responseObject.insert(QStringLiteral("SecretKey"), config.secretKey);
    responseObject.insert(QStringLiteral("Status"), reissueOk ? QStringLiteral("success") : QStringLiteral("failed"));

    QString notifyErrorMessage;
    if (!RabbitMqService::PublishReissueResult(config,
                                               QString::fromUtf8(QJsonDocument(responseObject).toJson(QJsonDocument::Compact)),
                                               &notifyErrorMessage)) {
        return false;
    }

    return true;
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
    const QString partitionId = SanitizeFileNamePart(ReadJsonScalarAsQString(dataObject,
                                                                             {QStringLiteral("PartionId"),
                                                                              QStringLiteral("partionId"),
                                                                              QStringLiteral("PartitionId"),
                                                                              QStringLiteral("partitionId"),
                                                                              QStringLiteral("GameId"),
                                                                              QStringLiteral("gameId")}));
    const QString playerAccount = SanitizeFileNamePart(ReadJsonScalarAsQString(dataObject,
                                                                               {QStringLiteral("PlayerAccount"),
                                                                                QStringLiteral("playerAccount"),
                                                                                QStringLiteral("UserId"),
                                                                                QStringLiteral("userId")}));
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
        {15, QStringLiteral("UpdateOrderName")} // Common.RabbitMQ 中为 Account，Gateway 枚举为 UpdateOrderName
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
        if (typeName.compare(QStringLiteral("Account"), Qt::CaseInsensitive) == 0) {
            return QStringLiteral("UpdateOrderName");
        }
        return typeName;
    };

    const QJsonValue orderTypeValue = object.value(QStringLiteral("OrderType"));
    if (!orderTypeValue.isUndefined() && !orderTypeValue.isNull()) {
        if (orderTypeValue.isString()) {
            const QString orderTypeText = orderTypeValue.toString().trimmed();
            bool ok = false;
            const int numericOrderType = orderTypeText.toInt(&ok);
            if (ok) {
                return knownTypes.value(numericOrderType, orderTypeText);
            }
            if (orderTypeText.compare(QStringLiteral("Account"), Qt::CaseInsensitive) == 0) {
                return QStringLiteral("UpdateOrderName");
            }
            return normalizeTypeName(orderTypeText);
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
        if (ok) {
            return knownTypes.value(numericOrderType, orderTypeText);
        }
        if (orderTypeText.compare(QStringLiteral("Account"), Qt::CaseInsensitive) == 0) {
            return QStringLiteral("UpdateOrderName");
        }
        return normalizeTypeName(orderTypeText);
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
            || dataObject.contains(QStringLiteral("gameId"))
            || dataObject.contains(QStringLiteral("PartitionId"))
            || dataObject.contains(QStringLiteral("partitionId"))) {
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

bool HandleCheckPartitionMessage(const QJsonObject &object)
{
    const QJsonValue dataValue = ExtractDataValue(object);
    const QJsonObject dataObject = ParseEmbeddedObject(dataValue);
    const QString checkKey = ReadStringField(dataObject, {QStringLiteral("CheckKey"), QStringLiteral("checkKey")});
    if (checkKey.isEmpty()) {
        AppLogger::WriteLog(QStringLiteral("【ERROR】【CheckPartition】缺少 CheckKey，已忽略"));
        return false;
    }

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

    return true;
}

bool HandleInfoMessage(const QJsonObject &object)
{
    const QJsonValue dataValue = ExtractDataValue(object);
    const QString dataText = ParseEmbeddedText(dataValue);
    AppLogger::WriteLog(dataText.isEmpty()
                            ? QStringLiteral("【ERROR】【Info】收到Info消息（对齐老网关 Logger.Error）")
                            : QStringLiteral("【ERROR】【Info】%1").arg(dataText));
    return true;
}

bool HandleOrderUpdateSuccessMessage(const QJsonObject &object)
{
    const QJsonValue dataValue = ExtractDataValue(object);
    const QString dataText = ParseEmbeddedText(dataValue);
    if (dataText.isEmpty()) {
        AppLogger::WriteLog(QStringLiteral("【ERROR】【OrderUpdateSuccess】消息内容为空"));
    }
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
        if (!ok) {
            AppLogger::WriteLog(QStringLiteral("创建分区 解密失败"));
            decryptedText.clear();
        } else if (decryptedText.isEmpty()) {
            AppLogger::WriteLog(QStringLiteral("创建分区 解密后内容为空"));
        } else {
            const auto document = QJsonDocument::fromJson(decryptedText.toUtf8());
            if (document.isObject()) {
                dataObject = document.object();
            }
        }
    }

    if (dataObject.isEmpty()) {
        AppLogger::WriteLog(decryptedText.isEmpty() ? QStringLiteral("创建分区失败：无法解析分区数据")
                                                    : QStringLiteral("创建分区失败：无法解析分区数据 %1").arg(decryptedText.left(300)));
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
    const QString randomCode = ReadStringField(dataObject, {QStringLiteral("RandomCode"), QStringLiteral("randomCode")});

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

    QString decodedText;
    const QJsonArray dataArray = ParseSecretKeyArray(ExtractDataValue(object), &decodedText);
    if (dataArray.isEmpty() && !decodedText.trimmed().startsWith(QLatin1Char('['))) {
        AppLogger::WriteLog(decodedText.isEmpty()
                                ? QStringLiteral("整区补发处理失败：无法解析补发数组消息")
                                : QStringLiteral("整区补发处理失败：无法解析补发数组消息 %1").arg(decodedText.left(300)));
        return false;
    }

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

    const QJsonValue dataValue = ExtractDataValue(object);
    if (dataValue.isUndefined() || dataValue.isNull()) {
        AppLogger::WriteLog(QStringLiteral("手动补发跳过：Data 为空"));
        return true;
    }
    if (!dataValue.isObject() && !dataValue.isString()) {
        AppLogger::WriteLog(QStringLiteral("手动补发处理失败：Data 须为加密字符串或 JSON 对象"));
        return true;
    }

    QString decodedText;
    bool decryptFailed = false;
    const QJsonObject dataObject = ParseManualReissuePayload(dataValue, &decodedText, &decryptFailed);
    if (decryptFailed) {
        AppLogger::WriteLog(QStringLiteral("手动补发 解密失败"));
        return true;
    }
    if (dataObject.isEmpty()) {
        if (decodedText.trimmed().isEmpty()) {
            AppLogger::WriteLog(QStringLiteral("手动补发 解密后内容为空，跳过处理"));
        } else {
            AppLogger::WriteLog(QStringLiteral("手动补发处理失败：无法解析补发消息 %1").arg(decodedText.left(300)));
        }
        return true;
    }

    const bool reissueOk = HandleRechargeDataObject(dataObject, QStringLiteral("补发"), false);
    const bool notifyOk = PublishManualReissueResult(dataObject, reissueOk);
    return reissueOk && notifyOk;
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
    }

    if (!replaced && QDir(payFolder).exists()) {
        QDirIterator iterator(payFolder, {QStringLiteral("*.txt")}, QDir::Files, QDirIterator::Subdirectories);
        while (iterator.hasNext()) {
            const QString filePath = iterator.next();
            if (ReplaceAccountInFile(filePath, playerAccount, newAccount, &fileErrorMessage)) {
                replaced = true;
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

    return true;
}

bool HandleUpdateOrderRoleNameMessage(const QJsonObject &object)
{
    const QJsonObject dataObject = ParseEmbeddedObject(ExtractDataValue(object));
    const QString newRoleName = ReadStringField(dataObject,
                                                {QStringLiteral("newRoleName"),
                                                 QStringLiteral("NewRoleName"),
                                                 QStringLiteral("newPlayerRoleName"),
                                                 QStringLiteral("NewPlayerRoleName")});
    const QJsonObject partitionObject = dataObject.value(QStringLiteral("Partition")).toObject();
    const QJsonObject templateObject = dataObject.value(QStringLiteral("Template")).toObject();
    const QJsonObject orderObject = dataObject.value(QStringLiteral("Order")).toObject();
    const QString orderNumber = ReadStringField(orderObject, {QStringLiteral("OrderNumber"), QStringLiteral("orderNumber")});
    const QString oldRoleName = ReadStringField(orderObject, {QStringLiteral("PlayerRoleName"), QStringLiteral("playerRoleName")});

    const QString scriptPath = ReadStringField(partitionObject, {QStringLiteral("ScriptPath"), QStringLiteral("scriptPath")});
    const QString payDir = ReadStringField(templateObject, {QStringLiteral("PayDir"), QStringLiteral("payDir")});
    const QString currencyName = ReadStringField(templateObject, {QStringLiteral("CurrencyName"), QStringLiteral("currencyName")});
    const QString payFolder = QDir(scriptPath).filePath(QStringLiteral("Mir200/Envir/QuestDiary/%1/充值%2/%2").arg(payDir, currencyName));
    const double amount = ResolveOrderUpdateRemainderAmount(orderObject);
    const QString targetFile = QDir(payFolder).filePath(QStringLiteral("%1.txt").arg(FormatOrderAmountFileName(amount)));

    bool replaced = false;
    QString fileErrorMessage;
    if (!oldRoleName.trimmed().isEmpty() && !newRoleName.trimmed().isEmpty() && QFileInfo::exists(targetFile)) {
        replaced = ReplaceAccountInFile(targetFile, oldRoleName, newRoleName, &fileErrorMessage);
    }

    if (!replaced && QDir(payFolder).exists()) {
        QDirIterator iterator(payFolder, {QStringLiteral("*.txt")}, QDir::Files, QDirIterator::Subdirectories);
        while (iterator.hasNext()) {
            const QString filePath = iterator.next();
            if (ReplaceAccountInFile(filePath, oldRoleName, newRoleName, &fileErrorMessage)) {
                replaced = true;
                break;
            }
        }
    }

    if (!replaced) {
        AppLogger::WriteLog(QStringLiteral("订单[%1] 修改角色名未找到匹配行：%2，在目录：%3")
                                .arg(orderNumber,
                                     oldRoleName.isEmpty() ? QStringLiteral("<empty>") : oldRoleName,
                                     QDir::toNativeSeparators(payFolder)));
    }

    if (orderNumber.trimmed().isEmpty() || newRoleName.trimmed().isEmpty()) {
        return false;
    }

    const auto config = AppConfig::Load();
    QJsonObject notifyObject;
    notifyObject.insert(QStringLiteral("OrderNumber"), orderNumber);
    notifyObject.insert(QStringLiteral("NewPlayerRoleName"), newRoleName);

    QString notifyError;
    if (!RabbitMqService::UpdateOrderAccount(config,
                                             QString::fromUtf8(QJsonDocument(notifyObject).toJson(QJsonDocument::Compact)),
                                             &notifyError)) {
        AppLogger::WriteLog(QStringLiteral("发送订单角色名更新(UpdateOrderAccount)失败：%1").arg(notifyError));
        return false;
    }
    (void) replaced;
    return true;
}

static bool JsonBoolLoose(const QJsonObject &o, const QStringList &keys)
{
    for (const QString &k : keys) {
        if (!o.contains(k))
            continue;
        const QJsonValue v = o.value(k);
        if (v.isBool())
            return v.toBool();
        if (v.isDouble())
            return v.toInt() != 0;
        if (v.isString()) {
            const QString s = v.toString().trimmed();
            if (s.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0 || s == QLatin1String("1"))
                return true;
        }
    }
    return false;
}

/// 分分通等：嵌入 JSON 的 codeQrUrl 为「二维码源」；优先提取 codeQrUrl，再以 form action 兜底（整页 HTML 不能直接进 Mir 编码）。
static QString ExtractQrPayloadFromHtmlOrPaymentResponse(const QString &raw)
{
    const QString t = raw.trimmed();
    if (t.isEmpty())
        return t;

    if (t.startsWith(QLatin1String("http://"), Qt::CaseInsensitive)
        || t.startsWith(QLatin1String("https://"), Qt::CaseInsensitive)) {
        if (!t.contains(QLatin1Char('<')) && t.size() < 2048)
            return t;
    }

    if (t.startsWith(QLatin1Char('{')) && t.contains(QLatin1String("codeQrUrl"))) {
        const QJsonObject o = QJsonDocument::fromJson(t.toUtf8()).object();
        const QString u = o.value(QLatin1String("codeQrUrl")).toString().trimmed();
        if (!u.isEmpty())
            return u;
    }

    const bool looksLikeHtml = t.contains(QLatin1String("<html"), Qt::CaseInsensitive)
        || t.contains(QLatin1String("<form"), Qt::CaseInsensitive);
    if (!looksLikeHtml)
        return t;

    QString decoded = t;
    decoded.replace(QLatin1String("&quot;"), QLatin1String("\""));
    static const QRegularExpression reCodeQr(QStringLiteral(R"re("codeQrUrl"\s*:\s*"([^"]+)")re"),
                                             QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch mJson = reCodeQr.match(decoded);
    if (mJson.hasMatch()) {
        QString url = mJson.captured(1).trimmed();
        url.replace(QLatin1String("\\/"), QLatin1String("/"));
        if (url.startsWith(QLatin1String("http://"), Qt::CaseInsensitive)
            || url.startsWith(QLatin1String("https://"), Qt::CaseInsensitive)) {
            return url;
        }
    }

    static const QRegularExpression reFormAction(
        QStringLiteral(R"re(<form\b[^>]*\baction\s*=\s*"([^"]+)")re"),
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    const QRegularExpressionMatch mForm = reFormAction.match(t);
    if (mForm.hasMatch()) {
        const QString url = mForm.captured(1).trimmed();
        if (url.startsWith(QLatin1String("http://"), Qt::CaseInsensitive)
            || url.startsWith(QLatin1String("https://"), Qt::CaseInsensitive)) {
            return url;
        }
    }

    static const QRegularExpression reFormActionSq(
        QStringLiteral(R"(<form\b[^>]*\baction\s*=\s*'([^']+)')"),
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    const QRegularExpressionMatch mFormSq = reFormActionSq.match(t);
    if (mFormSq.hasMatch()) {
        const QString url = mFormSq.captured(1).trimmed();
        if (url.startsWith(QLatin1String("http://"), Qt::CaseInsensitive)
            || url.startsWith(QLatin1String("https://"), Qt::CaseInsensitive)) {
            return url;
        }
    }

    return t;
}

/// 充值图片文件名约定：分区ID_账号.txt（与 filemonitorservice 写入 RabbitMQ Remarks 一致）
int TryParsePartitionIdFromQrImageFilePath(const QString &normalizedRelativePath)
{
    if (!normalizedRelativePath.contains(QStringLiteral("充值图片"), Qt::CaseInsensitive)) {
        return 0;
    }
    const QFileInfo fi(normalizedRelativePath);
    const QString base = fi.completeBaseName();
    const int usc = base.indexOf(QLatin1Char('_'));
    if (usc <= 0) {
        return 0;
    }
    bool ok = false;
    const int id = base.left(usc).toInt(&ok);
    return (ok && id > 0) ? id : 0;
}

static int ResolvePartitionIdForQrPath(const QJsonObject &dataObject, const QString &pathNorm, const QString &originalPathHint)
{
    int partitionId = dataObject.value(QStringLiteral("PartitionId")).toInt(0);
    if (partitionId <= 0) {
        partitionId = dataObject.value(QStringLiteral("partitionId")).toInt(0);
    }
    if (partitionId <= 0) {
        partitionId = TryParsePartitionIdFromQrImageFilePath(pathNorm);
    }
    if (partitionId <= 0) {
        partitionId = TryParsePartitionIdFromQrImageFilePath(QDir::fromNativeSeparators(originalPathHint.trimmed()));
    }
    return partitionId;
}

/// 服务端若下发带盘符的绝对路径但盘符错误（如落到 C:），按分区脚本安装目录所在卷替换卷根。
QString RemapPlatformVerifyPathToPartitionVolume(const QString &absolutePath,
                                                 const QJsonObject &dataObject,
                                                 const QString &originalPathHint)
{
    if (absolutePath.trimmed().isEmpty()) {
        return {};
    }
    const QString norm = QDir::fromNativeSeparators(QFileInfo(absolutePath).absoluteFilePath());
    if (!norm.contains(QStringLiteral("平台验证"), Qt::CaseInsensitive)) {
        return norm;
    }

    const int partitionId = ResolvePartitionIdForQrPath(dataObject, norm, originalPathHint);
    const QString wantRoot = PartitionPathCache::RootPathForPartition(partitionId);
    if (wantRoot.isEmpty()) {
        return norm;
    }

    const QString curRoot = QDir::fromNativeSeparators(QDir(norm).rootPath());
    const QString w = QDir::fromNativeSeparators(wantRoot);
    if (QString::compare(curRoot, w, Qt::CaseInsensitive) == 0) {
        return norm;
    }

    QString tail = norm.mid(curRoot.length());
    while (tail.startsWith(QLatin1Char('/'))) {
        tail = tail.mid(1);
    }
    const QString remapped = QFileInfo(QDir(w).filePath(tail)).absoluteFilePath();
    return remapped;
}

/// 支付 HTML/链接里待编码进 Mir 的载荷常带「C:\\平台验证…」；游戏按该串落盘，与网关写入的 Path 文件不是一回事。仅替换盘符为当前分区脚本安装卷（与 RemapPlatformVerifyPathToPartitionVolume 同源）。
QString RemapPlatformVerifyDriveInQrPayload(const QString &text, const QJsonObject &dataObject, const QString &pathHint)
{
    if (text.isEmpty()) {
        return text;
    }
    const int partitionId = ResolvePartitionIdForQrPath(dataObject, QString(), pathHint);
    const QString wantRoot = PartitionPathCache::RootPathForPartition(partitionId);
    if (wantRoot.size() < 2 || wantRoot.at(1) != QLatin1Char(':')) {
        return text;
    }
    const QChar wantDrive = wantRoot.at(0).toUpper();
    static const QRegularExpression re(QStringLiteral(R"(([A-Za-z]:)((?:\\|/)平台验证))"),
                                       QRegularExpression::CaseInsensitiveOption);

    QString result;
    int last = 0;
    for (auto it = re.globalMatch(text); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        result += text.mid(last, m.capturedStart() - last);
        const QChar curDrive = m.captured(1).at(0).toUpper();
        if (curDrive != wantDrive) {
            result += wantDrive;
            result += QLatin1Char(':');
            result += m.captured(2);
        } else {
            result += m.captured(0);
        }
        last = m.capturedEnd();
    }
    result += text.mid(last);
    return result;
}

QStringList QrSplitPipeConfiguredPaths(const QString &value)
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

QStringList QrResolveVolumeRootsFromConfigField(const QString &configuredValue)
{
    QStringList roots;
    for (const QString &path : QrSplitPipeConfiguredPaths(configuredValue)) {
        const QString absolutePath = QFileInfo(path).absoluteFilePath();
        QString root;
        if (absolutePath.size() >= 3 && absolutePath.at(1) == QLatin1Char(':')
            && (absolutePath.at(2) == QLatin1Char('/') || absolutePath.at(2) == QLatin1Char('\\'))) {
            root = absolutePath.left(3);
        } else {
            root = QDir(absolutePath).absolutePath();
        }
        root = QDir::fromNativeSeparators(root);
        if (!root.isEmpty() && !roots.contains(root, Qt::CaseInsensitive)) {
            roots.append(root);
        }
    }
    return roots;
}

QStringList QrMergedYxsmWxVolumeRoots(const AppConfigValues &cfg)
{
    QStringList m = QrResolveVolumeRootsFromConfigField(cfg.yxsmDir);
    for (const QString &r : QrResolveVolumeRootsFromConfigField(cfg.wxValidPath)) {
        if (!r.isEmpty() && !m.contains(r, Qt::CaseInsensitive)) {
            m.append(r);
        }
    }
    return m;
}

static const QStringList kJsonDiskPathKeys = {QStringLiteral("DiskPath"),
                                              QStringLiteral("diskPath"),
                                              QStringLiteral("DiskRoot"),
                                              QStringLiteral("diskRoot"),
                                              QStringLiteral("Disk"),
                                              QStringLiteral("disk")};

void AppendUniqueVolumeRootForPlatformVerify(QStringList *out, const QString &anyPath)
{
    if (!out || anyPath.trimmed().isEmpty()) {
        return;
    }
    const QString vol = QDir::fromNativeSeparators(QDir(anyPath.trimmed()).rootPath()).trimmed();
    if (vol.isEmpty()) {
        return;
    }
    for (const QString &existing : std::as_const(*out)) {
        if (existing.compare(vol, Qt::CaseInsensitive) == 0) {
            return;
        }
    }
    out->append(vol);
}

/// WxValidPath 常填 C:\ 而游戏在 D:\；若按配置顺序先尝试 C:，会先成功写到错误盘符。将 C: 卷根排到队尾。
void PrioritizeNonSystemDriveCRoots(QStringList *roots)
{
#ifdef Q_OS_WIN
    if (!roots || roots->size() < 2) {
        return;
    }
    QStringList nonC;
    QStringList cOnly;
    for (const QString &r : std::as_const(*roots)) {
        const QString norm = QDir::fromNativeSeparators(r.trimmed());
        const bool isC = norm.size() >= 2 && norm[0].toUpper() == QLatin1Char('C') && norm[1] == QLatin1Char(':');
        if (isC) {
            cOnly.append(r);
        } else {
            nonC.append(r);
        }
    }
    if (!nonC.isEmpty()) {
        *roots = nonC + cOnly;
    }
#endif
}

/// 平台验证/微信转区写入：消息 DiskPath → 分区脚本缓存 → 安装起始路径 → 已缓存各分区卷 → yxsm|wxValid（与 QrMerged 一致，扫码盘优先）
void AppendPlatformVerifyVolumeRootsFromContext(QStringList *out,
                                                const QJsonObject &dataObject,
                                                const AppConfigValues &config,
                                                const QString &partitionIdString)
{
    AppendUniqueVolumeRootForPlatformVerify(out, ReadJsonScalarAsQString(dataObject, kJsonDiskPathKeys));

    bool pidOk = false;
    const int pid = partitionIdString.toInt(&pidOk);
    if (pidOk && pid > 0) {
        AppendUniqueVolumeRootForPlatformVerify(out, PartitionPathCache::RootPathForPartition(pid));
    }

    if (!config.installStartPath.trimmed().isEmpty()) {
        AppendUniqueVolumeRootForPlatformVerify(out, config.installStartPath.trimmed());
    }

    for (const QString &r : PartitionPathCache::AllScriptVolumeRoots()) {
        AppendUniqueVolumeRootForPlatformVerify(out, r);
    }

    for (const QString &r : QrMergedYxsmWxVolumeRoots(config)) {
        AppendUniqueVolumeRootForPlatformVerify(out, r);
    }

    PrioritizeNonSystemDriveCRoots(out);
}

/// 分区脚本常在 C:，服务端 Path 纠正后仍落在 C:；游戏与文件监控实际在 YxsmDir/WxValidPath/InstallStartPath 所在卷。仅当路径仍在 C: 时，按配置换卷根（与「扫码根路径」一致）。
QString RemapPlatformVerifyPathPreferGatewayConfigRoots(const QString &absolutePath)
{
    if (absolutePath.trimmed().isEmpty()) {
        return absolutePath;
    }
    if (!absolutePath.contains(QStringLiteral("平台验证"), Qt::CaseInsensitive)) {
        return absolutePath;
    }
#ifdef Q_OS_WIN
    const QString norm = QDir::fromNativeSeparators(QFileInfo(absolutePath).absoluteFilePath());
    const QString curRoot = QDir::fromNativeSeparators(QDir(norm).rootPath());
    const bool onDriveC = curRoot.length() >= 2 && curRoot.at(0).toUpper() == QLatin1Char('C') && curRoot.at(1) == QLatin1Char(':');
    if (!onDriveC) {
        return absolutePath;
    }

    const AppConfigValues cfg = AppConfig::Load();
    QString targetRoot;
    const QString installTrim = cfg.installStartPath.trimmed();
    if (!installTrim.isEmpty()) {
        const QString ir = QDir::fromNativeSeparators(QDir(QFileInfo(installTrim).absoluteFilePath()).rootPath());
        if (QString::compare(ir, curRoot, Qt::CaseInsensitive) != 0) {
            targetRoot = ir;
        }
    }
    if (targetRoot.isEmpty()) {
        for (const QString &r : QrMergedYxsmWxVolumeRoots(cfg)) {
            if (QString::compare(r, curRoot, Qt::CaseInsensitive) != 0) {
                targetRoot = r;
                break;
            }
        }
    }
    if (targetRoot.isEmpty() || QString::compare(curRoot, targetRoot, Qt::CaseInsensitive) == 0) {
        return absolutePath;
    }

    QString tail = norm.mid(curRoot.length());
    while (tail.startsWith(QLatin1Char('/'))) {
        tail = tail.mid(1);
    }
    const QString remapped = QDir::fromNativeSeparators(QFileInfo(QDir(targetRoot).filePath(tail)).absoluteFilePath());
    return remapped;
#else
    return absolutePath;
#endif
}

/// 与 HandleQrCodeMessage 一致：含「平台验证」的路径在写入前经分区脚本卷与 WxValid/Yxsm/InstallStartPath 纠正盘符（租户 Path 常先落在 C:）。
QString ApplyPlatformVerifyWritePathRemap(const QString &pathBuiltFromVolumeRoot,
                                          const QJsonObject &dataObject,
                                          const QString &partitionResolveHint)
{
    if (pathBuiltFromVolumeRoot.trimmed().isEmpty()) {
        return pathBuiltFromVolumeRoot;
    }
    QString p = QDir::fromNativeSeparators(QFileInfo(pathBuiltFromVolumeRoot).absoluteFilePath());
    p = RemapPlatformVerifyPathToPartitionVolume(p, dataObject, partitionResolveHint);
    p = RemapPlatformVerifyPathPreferGatewayConfigRoots(p);
    return p;
}

/// 服务端 Path 若为相对路径，不能相对网关进程工作目录解析（易落到 C:）。顺序：DiskPath → 分区脚本安装路径所在卷（与文件监控一致，不读 YxsmDir 配置）。
QString ResolveQrCodeWriteAbsolutePath(const QString &pathFromMessage, const QJsonObject &dataObject)
{
    const QString trimmed = pathFromMessage.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }
    const QString normalized = QDir::fromNativeSeparators(trimmed);
    QFileInfo fi(normalized);
    if (fi.isAbsolute()) {
        return fi.absoluteFilePath();
    }

    const QString diskHint = ReadStringField(dataObject, {QStringLiteral("DiskPath"), QStringLiteral("diskPath")});
    if (!diskHint.trimmed().isEmpty()) {
        const QString root = QDir::fromNativeSeparators(diskHint.trimmed());
        return QFileInfo(QDir(root).filePath(normalized)).absoluteFilePath();
    }

    const int partitionId = ResolvePartitionIdForQrPath(dataObject, normalized, trimmed);
    const QString installVolRoot = PartitionPathCache::RootPathForPartition(partitionId);
    if (!installVolRoot.isEmpty()) {
        const QString absolute = QFileInfo(QDir(installVolRoot).filePath(normalized)).absoluteFilePath();
        return absolute;
    }

    AppLogger::WriteLog(QStringLiteral(
        "二维码 Path 为相对路径，但无法从分区脚本安装目录解析盘符（请确认平台分区已填写安装路径且网关已拉取分区列表）"));
    return fi.absoluteFilePath();
}

/// 解析 Rabbit 消息里的扫码模板：兼容 `scan` / `Scan`，以及个别序列化把嵌套对象打成 JSON 字符串的情况。
static QJsonObject ResolveScanObject(const QJsonObject &dataObject)
{
    static const QStringList keys = {
        QStringLiteral("scan"),
        QStringLiteral("Scan"),
    };
    for (const QString &key : keys) {
        const QJsonValue raw = dataObject.value(key);
        if (raw.isObject()) {
            const QJsonObject o = raw.toObject();
            if (!o.isEmpty()) {
                return o;
            }
        }
        if (raw.isString()) {
            const QJsonDocument inner = QJsonDocument::fromJson(raw.toString().trimmed().toUtf8());
            if (inner.isObject() && !inner.object().isEmpty()) {
                return inner.object();
            }
        }
    }
    return {};
}

bool HandleQrCodeMessage(const QJsonObject &object)
{
    const QJsonObject dataObject = ParseEmbeddedObject(ExtractDataValue(object));
    const QString path = ReadStringField(dataObject, {QStringLiteral("Path"), QStringLiteral("path")});

    const QString qrCodeText = ReadStringField(dataObject, {QStringLiteral("QrCode"), QStringLiteral("qrCode")});
    const QJsonObject scanObject = ResolveScanObject(dataObject);
    QString resourceCode = ReadStringField(scanObject, {QStringLiteral("ResourceCode"), QStringLiteral("resourceCode")});
    if (resourceCode.isEmpty()) {
        resourceCode = ReadStringField(dataObject, {QStringLiteral("ResourceCode"), QStringLiteral("resourceCode")});
    }
    QString imageCode = ReadStringField(scanObject, {QStringLiteral("ImageCode"), QStringLiteral("imageCode")});
    if (imageCode.isEmpty()) {
        imageCode = ReadStringField(dataObject, {QStringLiteral("ImageCode"), QStringLiteral("imageCode")});
    }
    // Serial 即 QR 版本(3–6)，经 qrcodeencoder 裁切后格子数为 (4*version+17-8)^2。优先嵌套 scan，其次根级 Serial（Tenant 与 DTO 冗余字段对齐老王管）。
    const int serial = ReadIntField(scanObject,
                                    {QStringLiteral("Serial"), QStringLiteral("serial")},
                                    ReadIntField(dataObject,
                                                 {QStringLiteral("Serial"),
                                                  QStringLiteral("serial"),
                                                  QStringLiteral("QrSerial"),
                                                  QStringLiteral("qrSerial")},
                                                 0));
    const int xOffset = ReadIntField(scanObject,
                                     {QStringLiteral("XOffset"), QStringLiteral("xOffset")},
                                     ReadIntField(dataObject,
                                                  {QStringLiteral("XOffset"), QStringLiteral("xOffset")},
                                                  10));
    const int yOffset = ReadIntField(scanObject,
                                     {QStringLiteral("YOffset"), QStringLiteral("yOffset")},
                                     ReadIntField(dataObject,
                                                  {QStringLiteral("YOffset"), QStringLiteral("yOffset")},
                                                  10));

    QString absolutePath = ResolveQrCodeWriteAbsolutePath(path, dataObject);
    if (absolutePath.isEmpty()) {
        AppLogger::WriteLog(QStringLiteral("二维码处理失败：Path 为空"));
        return false;
    }
    absolutePath = RemapPlatformVerifyPathToPartitionVolume(absolutePath, dataObject, path);
    absolutePath = RemapPlatformVerifyPathPreferGatewayConfigRoots(absolutePath);

    QString driveRoot = QDir(absolutePath).rootPath();
    if (driveRoot.isEmpty()) {
        driveRoot = QDir::rootPath();
    }
    driveRoot = QDir::fromNativeSeparators(driveRoot);

    const bool useStaticQrImage = JsonBoolLoose(scanObject,
                                                {QStringLiteral("useStaticQrImage"), QStringLiteral("UseStaticQrImage")})
        || JsonBoolLoose(dataObject, {QStringLiteral("useStaticQrImage"), QStringLiteral("UseStaticQrImage")});
    const bool isWxmbOfficialAccountFile = absolutePath.contains(QStringLiteral("微信密保"), Qt::CaseInsensitive)
        && absolutePath.contains(QStringLiteral("本服公众号.txt"), Qt::CaseInsensitive);

    if (useStaticQrImage && isWxmbOfficialAccountFile) {
        const QStringList wxmbStaticClearPaths = {absolutePath,
                                                  QDir(driveRoot).filePath(QStringLiteral("平台验证/微信转区/本服公众号.txt"))};
        for (const auto &outputPath : wxmbStaticClearPaths) {
            if (!QDir().mkpath(QFileInfo(outputPath).absolutePath())
                || !WriteGb2312TextFile(outputPath, QString())) {
                AppLogger::WriteLog(QStringLiteral("微信密保静态图：清空「本服公众号.txt」失败 %1").arg(outputPath));
                return false;
            }
        }
        AppLogger::WriteLog(QStringLiteral("微信密保静态图：已清空「微信密保/本服公众号.txt」及转区目录镜像"));
        return true;
    }

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
        QString payload = ExtractQrPayloadFromHtmlOrPaymentResponse(qrCodeText.trimmed());
        payload = RemapPlatformVerifyDriveInQrPayload(payload, dataObject, path);
        QString qrErrorMessage;
        qrString = QrCodeEncoder::GenerateLegacyMirText(payload,
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

    {
        const int effVer = serial > 0 ? serial : 6;
        const int side = (4 * effVer + 17) - 8;
        const int nEmpty = qrString.count(QStringLiteral("< >"));
        const int nImg = qrString.count(QStringLiteral("<Img:"));
        const int partId = dataObject.value(QStringLiteral("PartitionId")).toInt(
            dataObject.value(QStringLiteral("partitionId")).toInt(0));
        AppLogger::WriteLog(QStringLiteral(
            "二维码已生成：消息Serial=%1→有效QR版本=%2，理论Mir边长=%3（%4格）；"
            "本串空格=%5+图块=%6=%7格；分区Id=%8；Path=%9")
                                .arg(serial)
                                .arg(effVer)
                                .arg(side)
                                .arg(side * side)
                                .arg(nEmpty)
                                .arg(nImg)
                                .arg(nEmpty + nImg)
                                .arg(partId)
                                .arg(path));
    }

    QStringList outputPaths;
    outputPaths.append(absolutePath);
    outputPaths.append(QDir(driveRoot).filePath(QStringLiteral("平台验证/微信转区/本服公众号.txt")));

    for (const auto &outputPath : outputPaths) {
        if (!QDir().mkpath(QFileInfo(outputPath).absolutePath()) || !WriteGb2312TextFile(outputPath, qrString)) {
            AppLogger::WriteLog(QStringLiteral("二维码处理失败：写入文件失败 %1").arg(outputPath));
            return false;
        }
    }

    return true;
}

bool HandleWeChatCodeMessage(const QJsonObject &object)
{
    return HandleQrCodeMessage(object);
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
    const QString partitionId = ReadJsonScalarAsQString(dataObject,
                                                        {QStringLiteral("PartionId"),
                                                         QStringLiteral("partionId"),
                                                         QStringLiteral("PartitionId"),
                                                         QStringLiteral("partitionId"),
                                                         QStringLiteral("GameId"),
                                                         QStringLiteral("gameId")});
    const QString serverName = ReadStringField(dataObject, {QStringLiteral("ServerName"), QStringLiteral("serverName")});
    const QString userName = ReadStringField(dataObject, {QStringLiteral("UserName"), QStringLiteral("userName")});
    const QString extractPoints = ReadJsonScalarAsQString(dataObject, {QStringLiteral("ExtractPoints"), QStringLiteral("extractPoints")});
    const QString balanceAfter = ReadJsonScalarAsQString(dataObject, {QStringLiteral("BalanceAfter"), QStringLiteral("balanceAfter")});

    const auto config = AppConfig::Load();
    QStringList volumeRootsToTry;
    AppendPlatformVerifyVolumeRootsFromContext(&volumeRootsToTry, dataObject, config, partitionId);

    if (volumeRootsToTry.isEmpty()) {
        AppLogger::WriteLog(QStringLiteral("微信转区处理失败：无可用卷根（请配置 WxValidPath/YxsmDir 或让租户消息带 DiskPath）；PartitionId=%1")
                                .arg(partitionId));
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

    for (const auto &volumeRoot : volumeRootsToTry) {
        QString targetDir = QDir(volumeRoot).filePath(QStringLiteral("平台验证/微信转区/%1").arg(subFolder));
        targetDir = ApplyPlatformVerifyWritePathRemap(targetDir, dataObject, QStringLiteral("平台验证/微信转区"));
        if (!QDir().mkpath(targetDir)) {
            continue;
        }

        const QString targetFilePath = QDir(targetDir).filePath(fileName);
        if (WriteGb2312TextFile(targetFilePath, fileContent)) {
            return true;
        }
    }

    AppLogger::WriteLog(QStringLiteral("微信转区处理失败：写入转区文件失败（已尝试卷根：%1）").arg(volumeRootsToTry.join(QStringLiteral(", "))));
    return false;
}

bool HandleWxValidBindRecordFiles(const QJsonObject &dataObject)
{
    const QString partitionId = ReadJsonScalarAsQString(dataObject,
                                                      {QStringLiteral("GameId"),
                                                       QStringLiteral("gameId"),
                                                       QStringLiteral("PartitionId"),
                                                       QStringLiteral("partitionId"),
                                                       QStringLiteral("PartionId"),
                                                       QStringLiteral("partionId")});
    const QString playerName = SanitizeFileNamePart(ReadStringField(dataObject, {QStringLiteral("RoleName"), QStringLiteral("roleName"), QStringLiteral("PlayerRoleName"), QStringLiteral("playerRoleName"), QStringLiteral("UserName"), QStringLiteral("userName")}));
    const QString openKey = ReadStringField(dataObject, {QStringLiteral("openKey"), QStringLiteral("OpenKey")});

    const auto config = AppConfig::Load();

    if (partitionId.isEmpty() || playerName.isEmpty() || openKey.trimmed().isEmpty()) {
        AppLogger::WriteLog(QStringLiteral("微信验证(OpenKey/绑定记录)失败：partitionId=%1 playerName=%2 openKeyLen=%3")
                                .arg(partitionId.isEmpty() ? QStringLiteral("<empty>") : partitionId)
                                .arg(playerName.isEmpty() ? QStringLiteral("<empty>") : playerName)
                                .arg(openKey.trimmed().size()));
        return false;
    }

    const QString serverId = ReadStringField(dataObject,
                                             {QStringLiteral("ServerId"),
                                              QStringLiteral("serverId"),
                                              QStringLiteral("ServerID"),
                                              QStringLiteral("serverID")});
    const QString serverName = ReadStringField(dataObject, {QStringLiteral("ServerName"), QStringLiteral("serverName")});

    /// 「平台验证」挂在卷根；候选盘符由消息、分区缓存、安装路径与 yxsm|wx 配置合并，且非 C: 优先（避免 WxValidPath 先填 C: 时写错盘）
    QStringList volumeRootsToTry;
    AppendPlatformVerifyVolumeRootsFromContext(&volumeRootsToTry, dataObject, config, partitionId);

    const QString diskPath = ReadJsonScalarAsQString(dataObject, kJsonDiskPathKeys);

    if (volumeRootsToTry.isEmpty()) {
        AppLogger::WriteLog(QStringLiteral(
            "微信验证处理失败：无可用卷根写入认证数据（请配置 WxValidPath 或 YxsmDir，或让平台/监控在消息中带 DiskPath；当前 partitionId=%1）")
                                .arg(partitionId));
        return false;
    }

    QStringList triedDirs;
    for (const QString &volumeRoot : volumeRootsToTry) {
        QString authDirPath = QDir(volumeRoot).filePath(QStringLiteral("平台验证/微信密保/认证数据"));
        authDirPath = ApplyPlatformVerifyWritePathRemap(authDirPath, dataObject, QStringLiteral("平台验证/微信密保/认证数据"));
        triedDirs.append(QDir::toNativeSeparators(authDirPath));
        if (!QDir().mkpath(authDirPath)) {
            continue;
        }

        const QString openKeyFilePath = QDir(authDirPath).filePath(QStringLiteral("%1_%2_WXID.txt").arg(partitionId, playerName));
        const QString gameIdFilePath = QDir(authDirPath).filePath(QStringLiteral("%1_%2_WXNC.txt").arg(partitionId, playerName));

        FileMonitorService::Instance().SuppressNextChange(openKeyFilePath);
        FileMonitorService::Instance().SuppressNextChange(gameIdFilePath);

        if (WriteGb2312TextFile(openKeyFilePath, openKey) && WriteGb2312TextFile(gameIdFilePath, partitionId)) {
            AppLogger::WriteLog(QStringLiteral(
                "微信验证(OpenKey/绑定)已写入认证文件（候选卷根=%1，实际目录经与二维码相同盘符纠正）：%2 / %3（WXNC 内容为分区 Id：%4）；"
                "ServerId=%5 ServerName=%6 DiskPath=%7")
                                    .arg(QDir::toNativeSeparators(volumeRoot),
                                         QDir::toNativeSeparators(openKeyFilePath),
                                         QDir::toNativeSeparators(gameIdFilePath),
                                         partitionId,
                                         serverId.isEmpty() ? QStringLiteral("<empty>") : serverId,
                                         serverName.isEmpty() ? QStringLiteral("<empty>") : serverName,
                                         diskPath.isEmpty() ? QStringLiteral("<empty>") : QDir::toNativeSeparators(diskPath)));
            return true;
        }
    }

    AppLogger::WriteLog(QStringLiteral(
        "微信验证处理失败：写入认证文件失败（已尝试目录：%1）；wxValidPath=%2 yxsmDir=%3 DiskPath=%4 partitionId=%5")
                            .arg(triedDirs.join(QStringLiteral(" | ")),
                                 QDir::toNativeSeparators(config.wxValidPath.trimmed()),
                                 QDir::toNativeSeparators(config.yxsmDir.trimmed()),
                                 diskPath.isEmpty() ? QStringLiteral("<empty>") : QDir::toNativeSeparators(diskPath),
                                 partitionId));
    return false;
}

bool HandleWxValidVxValidDtoReturnFiles(const QJsonObject &dataObject)
{
    const QString playerRoleName = ReadStringField(dataObject,
                                                   {QStringLiteral("PlayerRoleName"),
                                                    QStringLiteral("playerRoleName"),
                                                    QStringLiteral("PlayerName"),
                                                    QStringLiteral("playerName")});
    const QString openId = ReadStringField(dataObject,
                                           {QStringLiteral("OpenId"),
                                            QStringLiteral("openId"),
                                            QStringLiteral("OpenID"),
                                            QStringLiteral("Openid")});
    const QString nickname = ReadStringField(dataObject,
                                             {QStringLiteral("Nickname"),
                                              QStringLiteral("nickname"),
                                              QStringLiteral("NickName"),
                                              QStringLiteral("nickName")});
    const QString validCode = ReadJsonScalarAsQString(dataObject, {QStringLiteral("ValidCode"), QStringLiteral("validCode")});
    const QString dataType = ReadStringField(dataObject, {QStringLiteral("DataType"), QStringLiteral("dataType")});

    if (playerRoleName.trimmed().isEmpty()) {
        return false;
    }

    const auto config = AppConfig::Load();
    const QString partitionIdForVol = ReadJsonScalarAsQString(dataObject,
                                                             {QStringLiteral("PartitionId"),
                                                              QStringLiteral("partitionId"),
                                                              QStringLiteral("GameId"),
                                                              QStringLiteral("gameId"),
                                                              QStringLiteral("PartionId"),
                                                              QStringLiteral("partionId")});
    QStringList volumeRootsToTry;
    AppendPlatformVerifyVolumeRootsFromContext(&volumeRootsToTry, dataObject, config, partitionIdForVol);

    if (volumeRootsToTry.isEmpty()) {
        AppLogger::WriteLog(QStringLiteral(
            "VxValidDto 写回失败：无可用卷根（请配置 WxValidPath/YxsmDir/InstallStartPath 或在 payload 中提供 DiskPath）"));
        return false;
    }

    const QString nickFile = QStringLiteral("%1_WXNC.txt").arg(playerRoleName.trimmed());
    const QString openIdFile = QStringLiteral("%1_WXID.txt").arg(playerRoleName.trimmed());
    const QString codeFile = QStringLiteral("%1_CODE.txt").arg(playerRoleName.trimmed());

    bool wroteAny = false;
    for (const QString &volumeRoot : volumeRootsToTry) {
        QString dirWx = QDir(volumeRoot).filePath(QStringLiteral("WX验证/返回数据"));
        QString dirAuth = QDir(volumeRoot).filePath(QStringLiteral("平台验证/微信密保/认证数据"));
        dirWx = ApplyPlatformVerifyWritePathRemap(dirWx, dataObject, QStringLiteral("WX验证/返回数据"));
        dirAuth = ApplyPlatformVerifyWritePathRemap(dirAuth, dataObject, QStringLiteral("平台验证/微信密保/认证数据"));
        const QStringList dirs = {dirWx, dirAuth};
        for (const QString &dir : dirs) {
            if (!QDir().mkpath(dir)) {
                AppLogger::WriteLog(QStringLiteral("VxValidDto 写回失败：无法创建目录 %1").arg(dir));
                continue;
            }

            if (!nickname.trimmed().isEmpty()) {
                const QString p1 = QDir(dir).filePath(nickFile);
                FileMonitorService::Instance().SuppressNextChange(p1);
                if (WriteGb2312TextFile(p1, nickname)) {
                    wroteAny = true;
                }
            }
            if (!openId.trimmed().isEmpty()) {
                const QString p2 = QDir(dir).filePath(openIdFile);
                FileMonitorService::Instance().SuppressNextChange(p2);
                if (WriteGb2312TextFile(p2, openId)) {
                    wroteAny = true;
                }
            }
        }

        // 与 GatewayNew 一致：验证码文件仅写入「WX验证/返回数据」
        if (!validCode.trimmed().isEmpty()) {
            if (QDir().mkpath(dirWx)) {
                const QString p3 = QDir(dirWx).filePath(codeFile);
                FileMonitorService::Instance().SuppressNextChange(p3);
                if (WriteGb2312TextFile(p3, validCode)) {
                    wroteAny = true;
                }
            }
        }
    }

    if (!wroteAny) {
        AppLogger::WriteLog(QStringLiteral("VxValidDto 写回：未写入任何文件（OpenId、Nickname、ValidCode 均为空或写入失败）"));
        return false;
    }

    return true;
}

bool HandleWxValidMessage(const QJsonObject &object)
{
    const QJsonValue dataValue = ExtractDataValue(object);
    QJsonObject dataObject;
    if (dataValue.isObject()) {
        dataObject = dataValue.toObject();
    } else if (dataValue.isString()) {
        bool ok = false;
        const QString decryptedText = DecryptWithSecretKey(dataValue.toString(), &ok);
        if (!ok) {
            AppLogger::WriteLog(QStringLiteral("微信验证处理失败：解密失败"));
            return false;
        }
        if (decryptedText.trimmed().isEmpty()) {
            AppLogger::WriteLog(QStringLiteral("微信验证处理失败：解密后为空"));
            return false;
        }
        const auto document = QJsonDocument::fromJson(decryptedText.toUtf8());
        if (!document.isObject()) {
            AppLogger::WriteLog(QStringLiteral("微信验证处理失败：解密后不是有效 JSON 对象"));
            return false;
        }
        dataObject = document.object();
    } else {
        AppLogger::WriteLog(QStringLiteral("微信验证处理失败：Data 既不是对象也不是字符串"));
        return false;
    }

    const QString openKey = ReadStringField(dataObject, {QStringLiteral("openKey"), QStringLiteral("OpenKey")});
    if (!openKey.trimmed().isEmpty()) {
        return HandleWxValidBindRecordFiles(dataObject);
    }

    return HandleWxValidVxValidDtoReturnFiles(dataObject);
}
}

namespace RabbitMqDispatcher {
bool HandleMessage(const QString &payload)
{
    const auto document = QJsonDocument::fromJson(payload.toUtf8());
    if (!document.isObject()) {
        AppLogger::WriteLog(QStringLiteral("RabbitMQ 消息无法解析为 JSON 对象"));
        return true;
    }

    const QJsonObject object = document.object();
    const QString orderType = NormalizeOrderType(object);
    if (!orderType.isEmpty()) {
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
        if (orderType == QLatin1String("UpdateOrderName")) {
            return HandleUpdateOrderRoleNameMessage(object);
        }
        if (orderType == QLatin1String("QrCode")) {
            return HandleQrCodeMessage(object);
        }
        if (orderType == QLatin1String("WeChatCode")) {
            return HandleWeChatCodeMessage(object);
        }
        if (orderType == QLatin1String("WeixinTransfer")) {
            return HandleWeixinTransferMessage(object);
        }
        if (orderType == QLatin1String("WxValid")) {
            return HandleWxValidMessage(object);
        }
    }

    return true;
}
}
