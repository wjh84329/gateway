#include "rechargeprocessor.h"

#include "appconfig.h"
#include "applogger.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

namespace {
enum class TemplateType {
    Unknown,
    WebCommunication,
    CS,
    CQ,
    CQ3,
    TY,
    QJ
};

enum class GiveMethod {
    CloseGive = 0,
    AccordingToTheRechargeAmount = 1,
    RechargeAndChannel = 2,
    RechargeAndIncentive = 3,
    RechargeAndIncentiveAndChannel = 4
};

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

QJsonObject ReadObjectField(const QJsonObject &object, const QStringList &names)
{
    for (const auto &name : names) {
        const auto value = object.value(name);
        if (value.isObject()) {
            return value.toObject();
        }
    }
    return {};
}

QJsonArray ReadArrayField(const QJsonObject &object, const QStringList &names)
{
    for (const auto &name : names) {
        const auto value = object.value(name);
        if (value.isArray()) {
            return value.toArray();
        }
    }
    return {};
}

int ReadIntField(const QJsonObject &object, const QStringList &names, int defaultValue = 0)
{
    for (const auto &name : names) {
        const auto value = object.value(name);
        if (value.isDouble()) {
            return value.toInt(defaultValue);
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

double ReadDoubleField(const QJsonObject &object, const QStringList &names, double defaultValue = 0.0)
{
    for (const auto &name : names) {
        const auto value = object.value(name);
        if (value.isDouble()) {
            return value.toDouble(defaultValue);
        }
        if (value.isString()) {
            bool ok = false;
            const double parsed = value.toString().trimmed().toDouble(&ok);
            if (ok) {
                return parsed;
            }
        }
    }
    return defaultValue;
}

bool ReadBoolField(const QJsonObject &object, const QStringList &names, bool defaultValue = false)
{
    for (const auto &name : names) {
        const auto value = object.value(name);
        if (value.isBool()) {
            return value.toBool();
        }
        if (value.isDouble()) {
            return value.toInt() != 0;
        }
        if (value.isString()) {
            const QString text = value.toString().trimmed();
            if (text.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0) {
                return true;
            }
            if (text.compare(QStringLiteral("false"), Qt::CaseInsensitive) == 0) {
                return false;
            }
            bool ok = false;
            const int parsed = text.toInt(&ok);
            if (ok) {
                return parsed != 0;
            }
        }
    }
    return defaultValue;
}

QString FormatAmount(double value, int precision = 1)
{
    return QString::number(value, 'f', precision);
}

QString FormatInteger(double value)
{
    return QString::number(qRound64(value));
}

TemplateType ResolveTemplateType(const QJsonObject &rechargeObject, const QJsonObject &templateObject)
{
    const auto resolve = [](const QJsonValue &value) -> TemplateType {
        if (value.isDouble()) {
            switch (value.toInt()) {
            case 0: return TemplateType::WebCommunication;
            case 1: return TemplateType::CS;
            case 2: return TemplateType::CQ;
            case 3: return TemplateType::CQ3;
            case 4: return TemplateType::TY;
            case 5: return TemplateType::QJ;
            default: return TemplateType::Unknown;
            }
        }
        if (value.isString()) {
            const QString text = value.toString().trimmed();
            if (text.compare(QStringLiteral("WebCommunication"), Qt::CaseInsensitive) == 0) return TemplateType::WebCommunication;
            if (text.compare(QStringLiteral("CS"), Qt::CaseInsensitive) == 0) return TemplateType::CS;
            if (text.compare(QStringLiteral("CQ"), Qt::CaseInsensitive) == 0) return TemplateType::CQ;
            if (text.compare(QStringLiteral("CQ3"), Qt::CaseInsensitive) == 0) return TemplateType::CQ3;
            if (text.compare(QStringLiteral("TY"), Qt::CaseInsensitive) == 0) return TemplateType::TY;
            if (text.compare(QStringLiteral("QJ"), Qt::CaseInsensitive) == 0) return TemplateType::QJ;
        }
        return TemplateType::Unknown;
    };

    const TemplateType type = resolve(rechargeObject.value(QStringLiteral("Type")));
    if (type != TemplateType::Unknown) {
        return type;
    }
    return resolve(templateObject.value(QStringLiteral("Type")));
}

GiveMethod ResolveGiveMethod(const QJsonObject &object, const QStringList &fieldNames)
{
    for (const auto &fieldName : fieldNames) {
        const auto value = object.value(fieldName);
        if (value.isDouble()) {
            return static_cast<GiveMethod>(value.toInt());
        }
        if (value.isString()) {
            const QString text = value.toString().trimmed();
            bool ok = false;
            const int parsed = text.toInt(&ok);
            if (ok) {
                return static_cast<GiveMethod>(parsed);
            }
            if (text.compare(QStringLiteral("CloseGive"), Qt::CaseInsensitive) == 0) return GiveMethod::CloseGive;
            if (text.compare(QStringLiteral("AccordingToTheRechargeAmount"), Qt::CaseInsensitive) == 0) return GiveMethod::AccordingToTheRechargeAmount;
            if (text.compare(QStringLiteral("RechargeAndChannel"), Qt::CaseInsensitive) == 0) return GiveMethod::RechargeAndChannel;
            if (text.compare(QStringLiteral("RechargeAndIncentive"), Qt::CaseInsensitive) == 0) return GiveMethod::RechargeAndIncentive;
            if (text.compare(QStringLiteral("RechargeAndIncentiveAndChannel"), Qt::CaseInsensitive) == 0) return GiveMethod::RechargeAndIncentiveAndChannel;
        }
    }
    return GiveMethod::CloseGive;
}

bool EnsureDirectoryExists(const QString &path, QString *errorMessage)
{
    if (QDir().mkpath(path)) {
        return true;
    }
    if (errorMessage) {
        *errorMessage = QStringLiteral("无法创建目录：%1").arg(path);
    }
    return false;
}

bool SaveTxtFile(const QString &filePath, const QString &content, QString *errorMessage)
{
    const QFileInfo fileInfo(filePath);
    if (!EnsureDirectoryExists(fileInfo.absolutePath(), errorMessage)) {
        return false;
    }

    // 充值 / 补发：只追加一行，不读回、不覆盖、不按账号去重（与老网关 StreamWriter(..., append:true) 一致）
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("无法写入文件：%1").arg(filePath);
        }
        return false;
    }

    file.write(content.toLocal8Bit());
    file.write("\n");
    file.close();
    return true;
}

bool WriteScore(const QString &path, double amount, const QString &account, QString *errorMessage)
{
    return SaveTxtFile(QDir(path).filePath(QStringLiteral("%1.txt").arg(FormatInteger(amount))), account, errorMessage);
}

bool WriteDecimalScore(const QString &path, double amount, const QString &account, QString *errorMessage)
{
    return SaveTxtFile(QDir(path).filePath(QStringLiteral("0%1.txt").arg(FormatInteger(amount * 10.0))), account, errorMessage);
}

bool RechargeToFile(const QString &path, double score, const QString &account, QString *errorMessage)
{
    double amount = score;
    if (amount >= 1000000.0 || amount < 0.0) {
        return true;
    }

    const QList<double> steps = {100000.0, 10000.0, 1000.0, 100.0, 10.0, 1.0};
    for (const double step : steps) {
        if (amount >= step) {
            const int count = int(amount / step);
            if (!WriteScore(path, count * step, account, errorMessage)) {
                return false;
            }
            amount -= count * step;
        }
    }

    amount = qRound(amount * 10.0) / 10.0;
    if (amount > 0.0) {
        return WriteDecimalScore(path, amount, account, errorMessage);
    }
    return true;
}

double ResolveGiveAmount(GiveMethod type,
                         double amount,
                         double channelGiveAmount,
                         double incentiveGiveAmount)
{
    switch (type) {
    case GiveMethod::AccordingToTheRechargeAmount:
        return amount;
    case GiveMethod::RechargeAndChannel:
        return amount + channelGiveAmount;
    case GiveMethod::RechargeAndIncentive:
        return amount + incentiveGiveAmount;
    case GiveMethod::RechargeAndIncentiveAndChannel:
        return amount + incentiveGiveAmount + channelGiveAmount;
    case GiveMethod::CloseGive:
    default:
        return -1.0;
    }
}

bool RechargeEquip(const QJsonArray &infoEquip, double equipAmount, const QString &equipPath, const QString &account, QString *errorMessage)
{
    Q_UNUSED(equipAmount);
    for (const auto &value : infoEquip) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject equipGive = value.toObject();
        const double amount = ReadDoubleField(equipGive, {QStringLiteral("Amount"), QStringLiteral("amount")});
        if (amount > 0.0) {
            if (!SaveTxtFile(QDir(equipPath).filePath(QStringLiteral("%1.txt").arg(FormatInteger(amount))), account, errorMessage)) {
                return false;
            }
        }
    }
    return true;
}

bool EquipRecharge(const QJsonObject &rechargeObject,
                   const QString &rechargePath,
                   double amount,
                   const QString &account,
                   double channelGiveAmount,
                   double incentiveGiveAmount,
                   double extraGiveAmount,
                   QString *errorMessage)
{
    const GiveMethod equipType = ResolveGiveMethod(rechargeObject, {QStringLiteral("EquipType"), QStringLiteral("equipType")});
    if (equipType == GiveMethod::CloseGive) {
        return true;
    }

    if (!ReadBoolField(rechargeObject, {QStringLiteral("IsOnlyYb"), QStringLiteral("isOnlyYb")}) && extraGiveAmount > 0.0) {
        amount += extraGiveAmount;
    }

    const double equipAmount = ResolveGiveAmount(equipType, amount, channelGiveAmount, incentiveGiveAmount);
    if (equipAmount < 0.0) {
        return true;
    }

    return RechargeEquip(ReadArrayField(rechargeObject, {QStringLiteral("InfoEquip"), QStringLiteral("infoEquip")}),
                         equipAmount,
                         QDir(rechargePath).filePath(QStringLiteral("装备赠送")),
                         account,
                         errorMessage);
}

bool AdditionalRecharge(const QJsonObject &rechargeObject,
                        const QString &rechargePath,
                        double amount,
                        const QString &account,
                        double channelGiveAmount,
                        double incentiveGiveAmount,
                        double extraGiveAmount,
                        QString *errorMessage)
{
    const QJsonArray items = ReadArrayField(rechargeObject, {QStringLiteral("InfoAdditional"), QStringLiteral("infoAdditional")});
    if (items.isEmpty()) {
        return true;
    }

    if (!ReadBoolField(rechargeObject, {QStringLiteral("IsOnlyYb"), QStringLiteral("isOnlyYb")}) && extraGiveAmount > 0.0) {
        amount += extraGiveAmount;
    }

    const QString additionalPath = QDir(rechargePath).filePath(QStringLiteral("附加赠送"));
    for (const auto &value : items) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject item = value.toObject();
        const GiveMethod type = ResolveGiveMethod(item, {QStringLiteral("Type"), QStringLiteral("type")});
        if (type == GiveMethod::CloseGive) {
            continue;
        }
        const double temp = ResolveGiveAmount(type, amount, channelGiveAmount, incentiveGiveAmount);
        if (temp < 0.0) {
            continue;
        }
        const QString name = ReadStringField(item, {QStringLiteral("Name"), QStringLiteral("name")});
        if (!RechargeToFile(QDir(additionalPath).filePath(name), temp, account, errorMessage)) {
            return false;
        }
    }
    return true;
}

bool IntegerRecharge(const QJsonObject &rechargeObject,
                     const QString &rechargePath,
                     double amount,
                     const QString &account,
                     double channelGiveAmount,
                     double incentiveGiveAmount,
                     double extraGiveAmount,
                     QString *errorMessage)
{
    const QJsonArray items = ReadArrayField(rechargeObject, {QStringLiteral("InfoIntegral"), QStringLiteral("infoIntegral")});
    if (items.isEmpty()) {
        return true;
    }

    if (!ReadBoolField(rechargeObject, {QStringLiteral("IsOnlyYb"), QStringLiteral("isOnlyYb")}) && extraGiveAmount > 0.0) {
        amount += extraGiveAmount;
    }

    const QString integerPath = QDir(rechargePath).filePath(QStringLiteral("积分充值"));
    for (const auto &value : items) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject item = value.toObject();
        const GiveMethod type = ResolveGiveMethod(item, {QStringLiteral("Type"), QStringLiteral("type")});
        if (type == GiveMethod::CloseGive) {
            continue;
        }
        const double temp = ResolveGiveAmount(type, amount, channelGiveAmount, incentiveGiveAmount);
        if (temp < 0.0) {
            continue;
        }
        const QString name = ReadStringField(item, {QStringLiteral("Name"), QStringLiteral("name")});
        if (!RechargeToFile(QDir(integerPath).filePath(QStringLiteral("%1充值").arg(name)), temp, account, errorMessage)) {
            return false;
        }
    }
    return true;
}

bool RedPackageGive(const QJsonObject &rechargeObject,
                    const QString &rechargePath,
                    const QString &currencyName,
                    const QString &account,
                    QString *errorMessage)
{
    const QString redPackagePath = QDir(rechargePath).filePath(QStringLiteral("红包赠送"));
    const QString redPackageGoldCoinPath = QDir(redPackagePath).filePath(currencyName);
    const int redPackageAmount = int(ReadDoubleField(rechargeObject, {QStringLiteral("RedPacketAmount"), QStringLiteral("redPacketAmount")}));
    if (!RechargeToFile(redPackageGoldCoinPath, redPackageAmount, account, errorMessage)) {
        return false;
    }
    if (ReadBoolField(rechargeObject, {QStringLiteral("RedPacketAdditional"), QStringLiteral("redPacketAdditional")})) {
        if (!AdditionalRecharge(rechargeObject, redPackagePath, redPackageAmount, account, 0.0, 0.0, 0.0, errorMessage)) {
            return false;
        }
    }
    if (ReadBoolField(rechargeObject, {QStringLiteral("RedPacketIntegral"), QStringLiteral("redPacketIntegral")})) {
        if (!IntegerRecharge(rechargeObject, redPackagePath, redPackageAmount, account, 0.0, 0.0, 0.0, errorMessage)) {
            return false;
        }
    }
    if (ReadBoolField(rechargeObject, {QStringLiteral("RedPacketEquip"), QStringLiteral("redPacketEquip")})) {
        if (!EquipRecharge(rechargeObject, redPackagePath, redPackageAmount, account, 0.0, 0.0, 0.0, errorMessage)) {
            return false;
        }
    }
    return true;
}

/// 通区根路径：与 installscriptprocessor::ResolveTongQuScriptRoot 一致。
/// - IsDir != 0：TongQuDir 为完整服根目录（其下再接 Mir200/Envir/...）
/// - 否则：TongQuDir 为相对分区上一级目录的文件夹名
QString ResolveTongQuScriptPath(const QString &scriptPath, const QString &tongQuDir, int isDir)
{
    const QString dir = tongQuDir.trimmed();
    if (dir.isEmpty()) {
        return scriptPath;
    }
    if (isDir != 0) {
        return dir;
    }
    const QFileInfo info(scriptPath);
    QDir parentDir = info.dir();
    parentDir.cdUp();
    return QDir(parentDir.absolutePath()).filePath(dir);
}

bool ProcessLegendRecharge(const QJsonObject &rechargeObject, const QJsonObject &templateObject, QString *errorMessage)
{
    const QString scriptPath = ReadStringField(rechargeObject, {QStringLiteral("ScriptPath"), QStringLiteral("scriptPath")});
    const QString account = ReadStringField(rechargeObject, {QStringLiteral("PlayerAccount"), QStringLiteral("playerAccount")});
    const QString currencyName = ReadStringField(rechargeObject, {QStringLiteral("CurrencyName"), QStringLiteral("currencyName")}).trimmed();
    const QString payDir = ReadStringField(templateObject, {QStringLiteral("PayDir"), QStringLiteral("payDir")});
    const double safetyMoney = ReadDoubleField(templateObject, {QStringLiteral("SafetyMoney"), QStringLiteral("safetyMoney")});
    const double baseAmount = ReadDoubleField(rechargeObject, {QStringLiteral("Amount"), QStringLiteral("amount")}) + safetyMoney;
    const double channelGiveAmount = ReadDoubleField(rechargeObject, {QStringLiteral("ChannelGiveAmount"), QStringLiteral("channelGiveAmount")});
    const double incentiveGiveAmount = ReadDoubleField(rechargeObject, {QStringLiteral("IncentiveGiveAmount"), QStringLiteral("incentiveGiveAmount")});
    const double extraGiveAmount = ReadDoubleField(rechargeObject, {QStringLiteral("ExtraGiveMoney"), QStringLiteral("extraGiveMoney")});
    const double totalAmount = qRound((baseAmount + channelGiveAmount + incentiveGiveAmount + extraGiveAmount) * 10.0) / 10.0;

    auto processPath = [&](const QString &rootScriptPath) -> bool {
        const QString environmentPath = QDir(rootScriptPath).filePath(QStringLiteral("Mir200/Envir"));
        const QString rechargePath = QDir(environmentPath).filePath(QStringLiteral("QuestDiary/%1/充值%2").arg(payDir, currencyName));
        const QString goldCoinPath = QDir(rechargePath).filePath(currencyName);
        if (!RechargeToFile(goldCoinPath, totalAmount, account, errorMessage)) {
            return false;
        }
        if (!ReadBoolField(rechargeObject, {QStringLiteral("IsClose"), QStringLiteral("isClose")})) {
            if (!RechargeToFile(QDir(rechargePath).filePath(QStringLiteral("转区功能")), totalAmount, account, errorMessage)) {
                return false;
            }
        }
        if (!ReadBoolField(rechargeObject, {QStringLiteral("IsJf"), QStringLiteral("isJf")})) {
            if (!IntegerRecharge(rechargeObject, rechargePath, baseAmount, account, channelGiveAmount, incentiveGiveAmount, extraGiveAmount, errorMessage)) {
                return false;
            }
        }
        if (!ReadBoolField(rechargeObject, {QStringLiteral("IsZb"), QStringLiteral("isZb")})) {
            if (!EquipRecharge(rechargeObject, rechargePath, baseAmount, account, channelGiveAmount, incentiveGiveAmount, extraGiveAmount, errorMessage)) {
                return false;
            }
        }
        if (!ReadBoolField(rechargeObject, {QStringLiteral("IsFj"), QStringLiteral("isFj")})) {
            if (!AdditionalRecharge(rechargeObject, rechargePath, baseAmount, account, channelGiveAmount, incentiveGiveAmount, extraGiveAmount, errorMessage)) {
                return false;
            }
        }
        if (ReadBoolField(rechargeObject, {QStringLiteral("RedPacketState"), QStringLiteral("redPacketState")})
            && int(ReadDoubleField(rechargeObject, {QStringLiteral("RedPacketAmount"), QStringLiteral("redPacketAmount")})) > 0) {
            if (!RedPackageGive(rechargeObject, rechargePath, currencyName, account, errorMessage)) {
                return false;
            }
        }
        return true;
    };

    const bool isTongQu = ReadBoolField(rechargeObject, {QStringLiteral("IsTongQu"), QStringLiteral("isTongQu")})
                          || ReadBoolField(templateObject, {QStringLiteral("IsTongQu"), QStringLiteral("isTongQu")});
    if (isTongQu) {
        const QString tongQuDir = ReadStringField(templateObject, {QStringLiteral("TongQuDir"), QStringLiteral("tongQuDir")});
        const int isDir = ReadIntField(templateObject, {QStringLiteral("IsDir"), QStringLiteral("isDir")}, 0);
        const QString tongRoot = ResolveTongQuScriptPath(scriptPath, tongQuDir, isDir);
        if (tongQuDir.trimmed().isEmpty()) {
            AppLogger::WriteLog(
                QStringLiteral("通区充值：模板 TongQuDir 为空，已按分区 ScriptPath 落盘（订单号 %1）")
                    .arg(ReadStringField(rechargeObject, {QStringLiteral("OrderNumber"), QStringLiteral("orderNumber")})));
        }
        if (!processPath(tongRoot)) {
            return false;
        }
    } else {
        if (!processPath(scriptPath)) {
            return false;
        }
    }

    return true;
}

bool ProcessQjRecharge(const QJsonObject &rechargeObject, QString *errorMessage)
{
    const AppConfigValues config = AppConfig::Load();
    if (config.sqlConnectionStr.trimmed().isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("未配置 SqlConnectionStr");
        }
        return false;
    }

    const double totalAmount = ReadDoubleField(rechargeObject, {QStringLiteral("IncentiveGiveAmount"), QStringLiteral("incentiveGiveAmount")})
                               + ReadDoubleField(rechargeObject, {QStringLiteral("Amount"), QStringLiteral("amount")})
                               + ReadDoubleField(rechargeObject, {QStringLiteral("ChannelGiveAmount"), QStringLiteral("channelGiveAmount")})
                               + ReadDoubleField(rechargeObject, {QStringLiteral("RedPacketAmount"), QStringLiteral("redPacketAmount")})
                               + ReadDoubleField(rechargeObject, {QStringLiteral("ExtraGiveMoney"), QStringLiteral("extraGiveMoney")});
    const double ratio = ReadDoubleField(rechargeObject, {QStringLiteral("Ratio"), QStringLiteral("ratio")}, 1.0);
    const qint64 money = qRound64(totalAmount * ratio);
    const QString account = ReadStringField(rechargeObject, {QStringLiteral("PlayerAccount"), QStringLiteral("playerAccount")});

    const QString connectionName = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QODBC"), connectionName);
    db.setDatabaseName(config.sqlConnectionStr);
    if (!db.open()) {
        if (errorMessage) {
            *errorMessage = db.lastError().text();
        }
        QSqlDatabase::removeDatabase(connectionName);
        return false;
    }

    QSqlQuery query(db);
    query.prepare(QStringLiteral("update MEMB_INFO set myCash=myCash+:money where memb___id=:account"));
    query.bindValue(QStringLiteral(":money"), money);
    query.bindValue(QStringLiteral(":account"), account);
    const bool ok = query.exec() && query.numRowsAffected() > 0;
    if (!ok && errorMessage) {
        *errorMessage = query.lastError().text().isEmpty()
                            ? QStringLiteral("未找到需要充值的账号")
                            : query.lastError().text();
    }
    db.close();
    QSqlDatabase::removeDatabase(connectionName);
    return ok;
}

void WriteRechargeSummaryLog(const QJsonObject &rechargeObject, const QString &operation)
{
    const QJsonObject templateObject = ReadObjectField(rechargeObject, {QStringLiteral("template"), QStringLiteral("Template")});
    const TemplateType templateType = ResolveTemplateType(rechargeObject, templateObject);
    const double safetyMoney = ReadDoubleField(templateObject, {QStringLiteral("SafetyMoney"), QStringLiteral("safetyMoney")});

    const QString partitionName = ReadStringField(rechargeObject, {QStringLiteral("PartitionName"), QStringLiteral("partitionName")});
    const QString orderNumber = ReadStringField(rechargeObject, {QStringLiteral("OrderNumber"), QStringLiteral("orderNumber")});
    const QString playerAccount = ReadStringField(rechargeObject, {QStringLiteral("PlayerAccount"), QStringLiteral("playerAccount")});
    const QString currencyName = ReadStringField(rechargeObject, {QStringLiteral("CurrencyName"), QStringLiteral("currencyName")});
    const double amount = ReadDoubleField(rechargeObject, {QStringLiteral("Amount"), QStringLiteral("amount")});
    const double channelGiveAmount = ReadDoubleField(rechargeObject, {QStringLiteral("ChannelGiveAmount"), QStringLiteral("channelGiveAmount")});
    const double incentiveGiveAmount = ReadDoubleField(rechargeObject, {QStringLiteral("IncentiveGiveAmount"), QStringLiteral("incentiveGiveAmount")});
    const double redPacketAmount = ReadDoubleField(rechargeObject, {QStringLiteral("RedPacketAmount"), QStringLiteral("redPacketAmount")});
    const double extraGiveAmount = ReadDoubleField(rechargeObject, {QStringLiteral("ExtraGiveMoney"), QStringLiteral("extraGiveMoney")});
    const double zstotalAmount = incentiveGiveAmount + channelGiveAmount + redPacketAmount + extraGiveAmount;
    const double totalAmount = incentiveGiveAmount + amount + channelGiveAmount + redPacketAmount + extraGiveAmount;
    const double ratio = ReadDoubleField(rechargeObject, {QStringLiteral("Ratio"), QStringLiteral("ratio")}, 1.0);

    QString safetyPart;
    double yuanbaoBaseAmount = totalAmount;
    if (safetyMoney > 0.0
        && (templateType == TemplateType::CS || templateType == TemplateType::CQ || templateType == TemplateType::CQ3)) {
        safetyPart = QStringLiteral(" 风控金赠送%1元").arg(FormatAmount(safetyMoney));
        yuanbaoBaseAmount += safetyMoney;
    }

    AppLogger::WriteLog(QStringLiteral("%1=>【%2】%3 账号%4 充值%5元 送%6元%7 获得%8%9")
                            .arg(operation,
                                 partitionName,
                                 orderNumber,
                                 playerAccount,
                                 FormatAmount(amount),
                                 QString::number(zstotalAmount, 'f', 1),
                                 safetyPart,
                                 FormatInteger(yuanbaoBaseAmount * ratio),
                                 currencyName));
}
}

bool RechargeProcessor::Process(const QJsonObject &rechargeObject,
                                const QString &operation,
                                QString *errorMessage)
{
    const QJsonObject templateObject = ReadObjectField(rechargeObject, {QStringLiteral("template"), QStringLiteral("Template")});
    const TemplateType type = ResolveTemplateType(rechargeObject, templateObject);

    bool ok = false;
    switch (type) {
    case TemplateType::CS:
    case TemplateType::CQ:
    case TemplateType::CQ3:
        ok = ProcessLegendRecharge(rechargeObject, templateObject, errorMessage);
        break;
    case TemplateType::QJ:
        ok = ProcessQjRecharge(rechargeObject, errorMessage);
        break;
    case TemplateType::WebCommunication:
    case TemplateType::TY:
    case TemplateType::Unknown:
    default:
        if (errorMessage) {
            *errorMessage = QStringLiteral("暂不支持的充值模板类型");
        }
        return false;
    }

    if (ok) {
        WriteRechargeSummaryLog(rechargeObject, operation);
    }
    return ok;
}
