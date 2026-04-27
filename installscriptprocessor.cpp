#include "installscriptprocessor.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QVector>

#include "appconfig.h"
#include "filemonitorservice.h"

#include <climits>

namespace {
struct InstallContext
{
    bool isTongQu = false;
    int isDir = 0;
    QString tongQuDir;
    int buildType = 0;
};

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

QString ReadStringField(const QJsonObject &object, const QStringList &names)
{
    for (const auto &name : names) {
        const QString value = object.value(name).toString().trimmed();
        if (!value.isEmpty()) {
            return value;
        }
    }

    return {};
}

int ReadIntField(const QJsonObject &object, const QStringList &names, int defaultValue = 0)
{
    for (const auto &name : names) {
        const auto value = object.value(name);
        if (value.isDouble()) {
            return value.toInt();
        }
        if (value.isString()) {
            bool ok = false;
            const int parsedValue = value.toString().toInt(&ok);
            if (ok) {
                return parsedValue;
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
            const QString text = value.toString().trimmed().toLower();
            if (text == QLatin1String("true") || text == QLatin1String("1")) {
                return true;
            }
            if (text == QLatin1String("false") || text == QLatin1String("0")) {
                return false;
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
            return value.toDouble();
        }
        if (value.isString()) {
            bool ok = false;
            const double parsedValue = value.toString().toDouble(&ok);
            if (ok) {
                return parsedValue;
            }
        }
    }

    return defaultValue;
}

InstallContext ResolveInstallContext(const QJsonObject &dataObject,
                                     const QJsonObject &partitionObject,
                                     const QJsonObject &templateObject)
{
    InstallContext context;

    const auto readInt = [&](const QStringList &names, int defaultValue = 0) -> int {
        int value = ReadIntField(dataObject, names, INT_MIN);
        if (value != INT_MIN) {
            return value;
        }
        value = ReadIntField(partitionObject, names, INT_MIN);
        if (value != INT_MIN) {
            return value;
        }
        return ReadIntField(templateObject, names, defaultValue);
    };

    const auto readString = [&](const QStringList &names) -> QString {
        QString value = ReadStringField(dataObject, names);
        if (!value.isEmpty()) {
            return value;
        }
        value = ReadStringField(partitionObject, names);
        if (!value.isEmpty()) {
            return value;
        }
        return ReadStringField(templateObject, names);
    };

    context.isTongQu = readInt({QStringLiteral("IsTongQu"), QStringLiteral("isTongQu")}, 0) != 0;
    context.isDir = readInt({QStringLiteral("IsDir"), QStringLiteral("isDir")}, 0);
    context.tongQuDir = readString({QStringLiteral("TongQuDir"), QStringLiteral("tongQuDir")});
    context.buildType = readInt({QStringLiteral("BuildType"), QStringLiteral("buildType")}, 0);
    if (context.buildType == 0 && context.isTongQu) {
        context.buildType = 2;
    }

    return context;
}

QString ResolveTongQuScriptRoot(const QString &scriptPath, const InstallContext &context)
{
    if (!context.isTongQu || context.tongQuDir.trimmed().isEmpty()) {
        return scriptPath;
    }

    if (context.isDir != 0) {
        return context.tongQuDir;
    }

    QDir parentDir = QFileInfo(scriptPath).dir();
    parentDir.cdUp();
    return QDir(parentDir.absolutePath()).filePath(context.tongQuDir);
}

bool EnsureDirectoryExists(const QString &path, QString *errorMessage)
{
    if (path.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("目录路径为空");
        }
        return false;
    }

    if (QDir().mkpath(path)) {
        return true;
    }

    if (errorMessage) {
        *errorMessage = QStringLiteral("无法创建目录：%1").arg(path);
    }
    return false;
}

bool EnsureEmptyFileExists(const QString &filePath, QString *errorMessage)
{
    QFileInfo fileInfo(filePath);
    if (!EnsureDirectoryExists(fileInfo.absolutePath(), errorMessage)) {
        return false;
    }

    if (QFile::exists(filePath)) {
        return true;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("无法创建文件：%1").arg(filePath);
        }
        return false;
    }

    file.close();
    return true;
}

QStringList BuildAmountFileNames()
{
    QStringList fileNames;
    for (int amount = 1; amount < 10; ++amount) {
        fileNames.append(QStringLiteral("%1.txt").arg(amount, 2, 10, QLatin1Char('0')));
    }

    for (int amount = 1; amount <= 1000000; ++amount) {
        if (amount <= 10
            || (amount <= 100 && amount % 10 == 0)
            || (amount <= 1000 && amount % 100 == 0)
            || (amount <= 10000 && amount % 1000 == 0)
            || (amount <= 100000 && amount % 10000 == 0)
            || (amount > 100000 && amount % 100000 == 0)) {
            fileNames.append(QStringLiteral("%1.txt").arg(amount));
        }
    }

    return fileNames;
}

bool EnsureAmountFiles(const QString &directoryPath, QString *errorMessage)
{
    if (!EnsureDirectoryExists(directoryPath, errorMessage)) {
        return false;
    }

    static const QStringList amountFiles = BuildAmountFileNames();
    for (const auto &fileName : amountFiles) {
        if (!EnsureEmptyFileExists(QDir(directoryPath).filePath(fileName), errorMessage)) {
            return false;
        }
    }

    return true;
}

bool WriteTextFile(const QString &filePath, const QString &content, QString *errorMessage)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("无法写入文件：%1").arg(filePath);
        }
        return false;
    }

    file.write(content.toLocal8Bit());
    file.close();
    return true;
}

bool WriteTextFileIfEmpty(const QString &filePath, const QString &content, QString *errorMessage)
{
    QFile file(filePath);
    if (file.exists() && file.size() > 0) {
        return true;
    }

    return WriteTextFile(filePath, content, errorMessage);
}

QString BuildScriptContent(const QString &title, const QStringList &details)
{
    QString content = QStringLiteral("[@main]\r\n#SAY\r\n%1\\\r\n").arg(title);
    for (const auto &detail : details) {
        if (!detail.trimmed().isEmpty()) {
            content += detail + QStringLiteral("\\\r\n");
        }
    }
    content += QStringLiteral("<关闭/@exit>\r\n");
    return content;
}

QString BuildQuestCallScript(const QString &label,
                             const QString &title,
                             const QStringList &details,
                             const QStringList &actCommands = {});
QString BuildMenuLines(const QStringList &entries, const QString &fallbackText);
QString BuildQuestCallPath(const QString &payDir, const QStringList &segments);

struct QuestScriptStage
{
    QString label;
    QString title;
    QStringList details;
    QStringList actCommands;
};

QString BuildNpcForwardSection(const QString &label,
                              const QString &callPath,
                              const QString &callLabel,
                              const QStringList &details,
                              const QString &returnAction)
{
    QStringList lines;
    lines << QStringLiteral("[%1]").arg(label)
          << QStringLiteral("#IF")
          << QStringLiteral("#ACT")
          << QStringLiteral("#CALL [%1] %2").arg(callPath, callLabel)
          << QStringLiteral("#SAY");
    for (const auto &detail : details) {
        if (!detail.trimmed().isEmpty()) {
            lines << detail;
        }
    }
    lines << returnAction;
    return lines.join(QStringLiteral("\r\n"));
}

QString BuildQuestStageScripts(const QVector<QuestScriptStage> &stages)
{
    QStringList scripts;
    scripts.reserve(stages.size());
    for (const auto &stage : stages) {
        scripts.append(BuildQuestCallScript(stage.label, stage.title, stage.details, stage.actCommands).trimmed());
    }
    return scripts.join(QStringLiteral("\r\n\r\n")) + QStringLiteral("\r\n");
}

QString BuildScanSubmitScript(const QString &partitionId,
                              const QString &payDir,
                              const QString &currencyName,
                              const QString &submitFileName)
{
    const QString rewardScriptPath = BuildQuestCallPath(payDir,
                                                        {QStringLiteral("充值%1").arg(currencyName), QStringLiteral("按金额领取.txt")});

    return BuildQuestStageScripts({
        {
            QStringLiteral("@扫码准备提交"),
            QStringLiteral("提交扫码数据"),
            {
                QStringLiteral("分区ID：%1").arg(partitionId.isEmpty() ? QStringLiteral("<empty>") : partitionId),
                QStringLiteral("商品ID：<$STR(S80)>"),
                QStringLiteral("商品名称：<$STR(S81)>"),
                QStringLiteral("金额：<$STR(S82)>"),
                QStringLiteral("提交文件：%1").arg(submitFileName.isEmpty() ? QStringLiteral("<empty>") : submitFileName),
                QStringLiteral("奖励脚本：%1").arg(rewardScriptPath)
            },
            {
                QStringLiteral("MOV S83 %1").arg(submitFileName.isEmpty() ? QStringLiteral("<empty>") : submitFileName),
                QStringLiteral("GOTO @扫码写入请求")
            }
        },
        {
            QStringLiteral("@扫码写入请求"),
            QStringLiteral("写入扫码提交文件"),
            {
                QStringLiteral("提交文件：<$STR(S83)>"),
                QStringLiteral("商品ID：<$STR(S80)>"),
                QStringLiteral("商品名称：<$STR(S81)>"),
                QStringLiteral("金额：<$STR(S82)>")
            },
            {
                QStringLiteral("SENDMSG 6 扫码充值数据已写入提交队列"),
                QStringLiteral("GOTO @扫码完成提交")
            }
        },
        {
            QStringLiteral("@扫码完成提交"),
            QStringLiteral("发放扫码奖励"),
            {
                QStringLiteral("奖励脚本：%1").arg(rewardScriptPath),
                QStringLiteral("当前金额：<$STR(S82)>")
            },
            {
                QStringLiteral("#CALL [%1] @领取充值档位").arg(rewardScriptPath),
                QStringLiteral("SENDMSG 6 扫码充值奖励已发放")
            }
        }
    });
}

QString BuildRechargeAmountBridgeScript(const QString &payDir,
                                        const QString &currencyName,
                                        const QJsonArray &integralGives,
                                        const QJsonArray &additionalGives,
                                        bool transferEnabled,
                                        bool hasEquipGive)
{
    QStringList actCommands;
    const QString dynamicAmountFile = QStringLiteral("<$STR(S82)>.txt");

    actCommands << QStringLiteral("#CALL [%1] @main")
                       .arg(BuildQuestCallPath(payDir, {QStringLiteral("充值%1").arg(currencyName), currencyName, dynamicAmountFile}));

    for (const auto &integralValue : integralGives) {
        if (!integralValue.isObject()) {
            continue;
        }

        const QJsonObject integralObject = integralValue.toObject();
        const QString name = ReadStringField(integralObject, {QStringLiteral("Name"), QStringLiteral("name")});
        if (!name.isEmpty()) {
            actCommands << QStringLiteral("#CALL [%1] @main")
                               .arg(BuildQuestCallPath(payDir, {QStringLiteral("充值%1").arg(currencyName), QStringLiteral("积分充值"), QStringLiteral("%1充值").arg(name), dynamicAmountFile}));
        }
    }

    for (const auto &additionalValue : additionalGives) {
        if (!additionalValue.isObject()) {
            continue;
        }

        const QJsonObject additionalObject = additionalValue.toObject();
        const QString name = ReadStringField(additionalObject, {QStringLiteral("Name"), QStringLiteral("name")});
        if (!name.isEmpty()) {
            actCommands << QStringLiteral("#CALL [%1] @main")
                               .arg(BuildQuestCallPath(payDir, {QStringLiteral("充值%1").arg(currencyName), QStringLiteral("附加赠送"), name, dynamicAmountFile}));
        }
    }

    if (hasEquipGive) {
        actCommands << QStringLiteral("#CALL [%1] @main")
                           .arg(BuildQuestCallPath(payDir, {QStringLiteral("充值%1").arg(currencyName), QStringLiteral("装备赠送"), dynamicAmountFile}));
    }

    if (transferEnabled) {
        actCommands << QStringLiteral("#CALL [%1] @main")
                           .arg(BuildQuestCallPath(payDir, {QStringLiteral("充值%1").arg(currencyName), QStringLiteral("转区功能"), dynamicAmountFile}));
    }

    return BuildQuestCallScript(QStringLiteral("@领取充值档位"),
                                QStringLiteral("按金额领取奖励"),
                                {
                                    QStringLiteral("当前金额：<$STR(S82)>"),
                                    QStringLiteral("当前商品：<$STR(S81)>"),
                                    QStringLiteral("已按金额调用对应奖励脚本")
                                },
                                actCommands);
}

QString BuildWxMbSubmitScript(const QString &partitionId,
                              const QString &payDir,
                              const QString &submitFileName,
                              const QString &machineVar,
                              const QString &openidVar,
                              const QString &wxVar)
{
    QString script = BuildQuestCallScript(QStringLiteral("@认证准备提交"),
                                          QStringLiteral("提交微信密保认证"),
                                          {
                                              QStringLiteral("分区ID：%1").arg(partitionId.isEmpty() ? QStringLiteral("<empty>") : partitionId),
                                              QStringLiteral("机器码变量：%1").arg(machineVar.isEmpty() ? QStringLiteral("<empty>") : machineVar),
                                              QStringLiteral("OpenId变量：%1").arg(openidVar.isEmpty() ? QStringLiteral("<empty>") : openidVar),
                                              QStringLiteral("微信变量：%1").arg(wxVar.isEmpty() ? QStringLiteral("<empty>") : wxVar),
                                              QStringLiteral("提交文件：%1").arg(submitFileName.isEmpty() ? QStringLiteral("<empty>") : submitFileName)
                                          },
                                          {
                                              QStringLiteral("#CALL [%1] @检测密保")
                                                  .arg(BuildQuestCallPath(payDir, {QStringLiteral("微信密保"), QStringLiteral("登录检测.txt")})),
                                              QStringLiteral("MOV S94 %1").arg(submitFileName.isEmpty() ? QStringLiteral("<empty>") : submitFileName),
                                              QStringLiteral("MOV S96 %1").arg(machineVar.isEmpty() ? QStringLiteral("<empty>") : machineVar),
                                              QStringLiteral("MOV S97 %1").arg(openidVar.isEmpty() ? QStringLiteral("<empty>") : openidVar),
                                              QStringLiteral("MOV S98 %1").arg(wxVar.isEmpty() ? QStringLiteral("<empty>") : wxVar),
                                              QStringLiteral("GOTO @写入认证文件")
                                          });

    script += QStringLiteral("\r\n");
    script += BuildQuestCallScript(QStringLiteral("@写入认证文件"),
                                   QStringLiteral("写入微信密保认证文件"),
                                   {
                                       QStringLiteral("提交文件：<$STR(S94)>") ,
                                       QStringLiteral("机器码变量：<$STR(S96)>") ,
                                       QStringLiteral("OpenId变量：<$STR(S97)>") ,
                                       QStringLiteral("微信变量：<$STR(S98)>")
                                   },
                                   {
                                       QStringLiteral("SENDMSG 6 微信密保认证数据已写入提交队列"),
                                       QStringLiteral("GOTO @完成认证提交")
                                   });

    script += QStringLiteral("\r\n");
    script += BuildQuestCallScript(QStringLiteral("@完成认证提交"),
                                   QStringLiteral("完成微信密保认证提交"),
                                   {
                                       QStringLiteral("提交文件：<$STR(S94)>") ,
                                       QStringLiteral("认证数据已准备完成")
                                   },
                                   {
                                       QStringLiteral("SENDMSG 6 微信密保认证数据已准备，请检查提交文件")
                                   });
    return script;
}

QString BuildTransferSubmitScript(const QString &partitionId,
                                  const QString &queryFileName,
                                  const QString &deductFileName,
                                  const QString &applyFileName,
                                  const QString &transferCoinName,
                                  const QString &transferRechargeVar,
                                  const QString &transferUsedVar)
{
    return BuildQuestStageScripts({
        {
            QStringLiteral("@转区准备提交"),
            QStringLiteral("提交转区数据"),
            {
                QStringLiteral("分区ID：%1").arg(partitionId.isEmpty() ? QStringLiteral("<empty>") : partitionId),
                QStringLiteral("转区币名称：%1").arg(transferCoinName.isEmpty() ? QStringLiteral("<empty>") : transferCoinName),
                QStringLiteral("充值变量：%1").arg(transferRechargeVar.isEmpty() ? QStringLiteral("<empty>") : transferRechargeVar),
                QStringLiteral("使用变量：%1").arg(transferUsedVar.isEmpty() ? QStringLiteral("<empty>") : transferUsedVar),
                QStringLiteral("查询文件：%1").arg(queryFileName.isEmpty() ? QStringLiteral("<empty>") : queryFileName),
                QStringLiteral("扣除文件：%1").arg(deductFileName.isEmpty() ? QStringLiteral("<empty>") : deductFileName),
                QStringLiteral("申请文件：%1").arg(applyFileName.isEmpty() ? QStringLiteral("<empty>") : applyFileName)
            },
            {
                QStringLiteral("MOV S94 %1").arg(queryFileName.isEmpty() ? QStringLiteral("<empty>") : queryFileName),
                QStringLiteral("MOV S95 %1").arg(deductFileName.isEmpty() ? QStringLiteral("<empty>") : deductFileName),
                QStringLiteral("MOV S96 %1").arg(applyFileName.isEmpty() ? QStringLiteral("<empty>") : applyFileName),
                QStringLiteral("MOV S97 %1").arg(transferRechargeVar.isEmpty() ? QStringLiteral("<empty>") : transferRechargeVar),
                QStringLiteral("MOV S98 %1").arg(transferUsedVar.isEmpty() ? QStringLiteral("<empty>") : transferUsedVar),
                QStringLiteral("GOTO @转区写入查询")
            }
        },
        {
            QStringLiteral("@转区写入查询"),
            QStringLiteral("提交转区查询数据"),
            {
                QStringLiteral("查询文件：<$STR(S94)>"),
                QStringLiteral("转区币名称：%1").arg(transferCoinName.isEmpty() ? QStringLiteral("<empty>") : transferCoinName)
            },
            {
                QStringLiteral("SENDMSG 6 已准备转区查询文件"),
                QStringLiteral("GOTO @转区写入扣除")
            }
        },
        {
            QStringLiteral("@转区写入扣除"),
            QStringLiteral("提交转区点扣除数据"),
            {
                QStringLiteral("扣除文件：<$STR(S95)>"),
                QStringLiteral("充值变量：<$STR(S97)>")
            },
            {
                QStringLiteral("SENDMSG 6 已准备转区扣除文件"),
                QStringLiteral("GOTO @转区写入申请")
            }
        },
        {
            QStringLiteral("@转区写入申请"),
            QStringLiteral("提交转区申请数据"),
            {
                QStringLiteral("申请文件：<$STR(S96)>"),
                QStringLiteral("使用变量：<$STR(S98)>")
            },
            {
                QStringLiteral("SENDMSG 6 已准备转区申请文件"),
                QStringLiteral("GOTO @转区完成提交")
            }
        },
        {
            QStringLiteral("@转区完成提交"),
            QStringLiteral("完成转区提交"),
            {
                QStringLiteral("查询文件：<$STR(S94)>"),
                QStringLiteral("扣除文件：<$STR(S95)>"),
                QStringLiteral("申请文件：<$STR(S96)>")
            },
            {
                QStringLiteral("SENDMSG 6 转区数据已准备，请检查提交文件")
            }
        }
    });
}

QString BuildWebRechargeScript(const QString &browserCommand,
                               const QString &partitionUuid,
                               const QJsonArray &circuits)
{
    QStringList menus;
    QStringList sections;
    int circuitIndex = 1;
    for (const auto &circuitValue : circuits) {
        if (!circuitValue.isObject()) {
            continue;
        }

        const QJsonObject circuitObject = circuitValue.toObject();
        const QString circuitName = ReadStringField(circuitObject, {QStringLiteral("Name"), QStringLiteral("name")});
        const QString domainName = ReadStringField(circuitObject, {QStringLiteral("DomainName"), QStringLiteral("domainName")});
        const QString port = ReadStringField(circuitObject, {QStringLiteral("Port"), QStringLiteral("port")});
        if (circuitName.isEmpty()) {
            continue;
        }

        const QString sectionName = QStringLiteral("@网页通道%1").arg(circuitIndex++);
        menus << QStringLiteral("<%1/%2>").arg(circuitName, sectionName);

        QStringList actCommands;
        actCommands << QStringLiteral("MOV S84 %1").arg(circuitName);
        actCommands << QStringLiteral("MOV S85 %1").arg(domainName.isEmpty() ? QStringLiteral("<empty>") : domainName);
        actCommands << QStringLiteral("MOV S86 %1").arg(port.isEmpty() ? QStringLiteral("<empty>") : port);
        actCommands << QStringLiteral("MOV S87 %1").arg(partitionUuid.isEmpty() ? QStringLiteral("<empty>") : partitionUuid);
        if (!browserCommand.isEmpty() && !domainName.isEmpty() && !partitionUuid.isEmpty()) {
            const QString rechargeUrl = port.isEmpty()
                                            ? QStringLiteral("%1/Default/Partition/%2").arg(domainName, partitionUuid)
                                            : QStringLiteral("%1:%2/Default/Partition/%3").arg(domainName, port, partitionUuid);
            actCommands << QStringLiteral("MOV S88 %1").arg(rechargeUrl);
            actCommands << QStringLiteral("%1 %2").arg(browserCommand, rechargeUrl);
        } else {
            actCommands << QStringLiteral("MOV S88 <empty>");
        }
        actCommands << QStringLiteral("GOTO @网页准备提交");

        sections << BuildQuestCallScript(sectionName,
                                         QStringLiteral("网页充值通道"),
                                         {
                                             QStringLiteral("通道名称：%1").arg(circuitName),
                                             QStringLiteral("域名：%1").arg(domainName.isEmpty() ? QStringLiteral("<empty>") : domainName),
                                             QStringLiteral("端口：%1").arg(port.isEmpty() ? QStringLiteral("<empty>") : port),
                                             QStringLiteral("分区标识：%1").arg(partitionUuid.isEmpty() ? QStringLiteral("<empty>") : partitionUuid)
                                         },
                                         actCommands);
    }

    QStringList lines;
    lines << QStringLiteral("[@网页充值]")
          << QStringLiteral("#SAY")
          << QStringLiteral("网页充值命令：%1\\").arg(browserCommand.isEmpty() ? QStringLiteral("<empty>") : browserCommand)
          << QStringLiteral("网页充值标识：%1\\").arg(partitionUuid.isEmpty() ? QStringLiteral("<empty>") : partitionUuid)
          << BuildMenuLines(menus, QStringLiteral("充值通道待补充"))
          << QStringLiteral("<关闭/@exit>");

    for (const auto &section : sections) {
        lines << section.trimmed();
    }

    const QString stagedScripts = BuildQuestStageScripts({
        {
            QStringLiteral("@网页准备提交"),
            QStringLiteral("准备网页充值请求"),
            {
                QStringLiteral("通道名称：<$STR(S84)>"),
                QStringLiteral("充值地址：<$STR(S88)>")
            },
            {
                QStringLiteral("GOTO @网页写入请求")
            }
        },
        {
            QStringLiteral("@网页写入请求"),
            QStringLiteral("写入网页充值请求"),
            {
                QStringLiteral("通道名称：<$STR(S84)>"),
                QStringLiteral("域名：<$STR(S85)>"),
                QStringLiteral("端口：<$STR(S86)>"),
                QStringLiteral("分区标识：<$STR(S87)>"),
                QStringLiteral("充值地址：<$STR(S88)>")
            },
            {
                QStringLiteral("SENDMSG 6 网页充值请求已写入提交队列"),
                QStringLiteral("GOTO @网页完成提交")
            }
        },
        {
            QStringLiteral("@网页完成提交"),
            QStringLiteral("完成网页充值请求"),
            {
                QStringLiteral("通道名称：<$STR(S84)>"),
                QStringLiteral("充值地址：<$STR(S88)>")
            },
            {
                QStringLiteral("SENDMSG 6 网页充值通道已打开，请完成后续支付")
            }
        }
    });
    lines << stagedScripts.trimmed();

    return lines.join(QStringLiteral("\r\n")) + QStringLiteral("\r\n");
}

QString BuildMenuLines(const QStringList &entries, const QString &fallbackText)
{
    if (entries.isEmpty()) {
        return fallbackText + QStringLiteral("\\");
    }

    QStringList lines;
    QStringList currentLine;
    for (const auto &entry : entries) {
        currentLine.append(entry);
        if (currentLine.size() == 3) {
            lines.append(currentLine.join(QStringLiteral("  ")) + QStringLiteral("\\"));
            currentLine.clear();
        }
    }

    if (!currentLine.isEmpty()) {
        lines.append(currentLine.join(QStringLiteral("  ")) + QStringLiteral("\\"));
    }

    return lines.join(QStringLiteral("\r\n"));
}

QStringList BuildEquipCommands(const QString &command, const QString &name)
{
    if (command.trimmed().isEmpty() || name.trimmed().isEmpty()) {
        return {};
    }

    QStringList commands;
    const auto parts = name.split(QLatin1Char('+'), Qt::SkipEmptyParts);
    for (const auto &part : parts) {
        const QString trimmedPart = part.trimmed();
        if (trimmedPart.isEmpty()) {
            continue;
        }

        QString itemName = trimmedPart;
        QString itemCount = QStringLiteral("1");
        const int separatorIndex = trimmedPart.indexOf(QLatin1Char('*'));
        if (separatorIndex > 0) {
            itemName = trimmedPart.left(separatorIndex).trimmed();
            const QString parsedCount = trimmedPart.mid(separatorIndex + 1).trimmed();
            if (!parsedCount.isEmpty()) {
                itemCount = parsedCount;
            }
        }

        if (!itemName.isEmpty()) {
            commands.append(QStringLiteral("%1 %2 %3").arg(command.trimmed(), itemName, itemCount));
        }
    }

    return commands;
}

QString BuildDirectActionScript(const QString &title,
                                const QStringList &details,
                                const QStringList &actCommands = {})
{
    QString content = QStringLiteral("[@main]\r\n");
    if (!actCommands.isEmpty()) {
        content += QStringLiteral("#IF\r\n#ACT\r\n");
        for (const auto &command : actCommands) {
            if (!command.trimmed().isEmpty()) {
                content += command + QStringLiteral("\r\n");
            }
        }
    }

    content += QStringLiteral("#SAY\r\n%1\\\r\n").arg(title);
    for (const auto &detail : details) {
        if (!detail.trimmed().isEmpty()) {
            content += detail + QStringLiteral("\\\r\n");
        }
    }
    content += QStringLiteral("<关闭/@exit>\r\n");
    return content;
}

double AmountFromFileName(const QString &fileName)
{
    QString amountText = fileName;
    if (amountText.endsWith(QStringLiteral(".txt"), Qt::CaseInsensitive)) {
        amountText.chop(4);
    }

    if (amountText.size() == 2 && amountText.startsWith(QLatin1Char('0'))) {
        return 0.0;
    }

    bool ok = false;
    const double amount = amountText.toDouble(&ok);
    return ok ? amount : 0.0;
}

bool PopulateAmountScriptFiles(const QString &directoryPath,
                               const QString &titlePrefix,
                               const QString &rewardName,
                               const std::function<QStringList(double)> &actCommandFactory,
                               const std::function<QStringList(double)> &detailFactory,
                               QString *errorMessage)
{
    static const QStringList amountFiles = BuildAmountFileNames();
    for (const auto &fileName : amountFiles) {
        const double amount = AmountFromFileName(fileName);
        const QString displayAmount = fileName.left(fileName.size() - 4);
        const QStringList details = detailFactory(amount)
                                    << QStringLiteral("充值档位：%1").arg(displayAmount)
                                    << QStringLiteral("目标目录：%1").arg(QDir::toNativeSeparators(directoryPath))
                                    << QStringLiteral("奖励类型：%1").arg(rewardName.isEmpty() ? QStringLiteral("<empty>") : rewardName);
        if (!WriteTextFileIfEmpty(QDir(directoryPath).filePath(fileName),
                                  BuildDirectActionScript(QStringLiteral("%1 %2").arg(titlePrefix, displayAmount),
                                                          details,
                                                          actCommandFactory(amount)),
                                  errorMessage)) {
            return false;
        }
    }

    return true;
}

QString FormatNumber(double value)
{
    QString text = QString::number(value, 'f', 2);
    while (text.contains(QLatin1Char('.')) && text.endsWith(QLatin1Char('0'))) {
        text.chop(1);
    }
    if (text.endsWith(QLatin1Char('.'))) {
        text.chop(1);
    }
    return text;
}

QString BuildQuestCallScript(const QString &label,
                             const QString &title,
                             const QStringList &details,
                             const QStringList &actCommands)
{
    QString content = QStringLiteral("[%1]\r\n").arg(label);
    if (!actCommands.isEmpty()) {
        content += QStringLiteral("#IF\r\n#ACT\r\n");
        for (const auto &command : actCommands) {
            if (!command.trimmed().isEmpty()) {
                content += command + QStringLiteral("\r\n");
            }
        }
    }

    content += QStringLiteral("#SAY\r\n%1\\\r\n").arg(title);
    for (const auto &detail : details) {
        if (!detail.trimmed().isEmpty()) {
            content += detail + QStringLiteral("\\\r\n");
        }
    }
    content += QStringLiteral("<关闭/@exit>\r\n");
    return content;
}

QString BuildMerchantNpcLine(const QString &name, const QString &map, const QString &x, const QString &y, const QString &look)
{
    if (name.trimmed().isEmpty() || map.trimmed().isEmpty()) {
        return {};
    }

    return QStringLiteral("%1     %2    %3    %4    %1     0     %5     0     0     0     0     0")
        .arg(name.trimmed(),
             map.trimmed(),
             x.trimmed().isEmpty() ? QStringLiteral("0") : x.trimmed(),
             y.trimmed().isEmpty() ? QStringLiteral("0") : y.trimmed(),
             look.trimmed().isEmpty() ? QStringLiteral("0") : look.trimmed());
}

QString ReadExistingTextFile(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }

    return QString::fromLocal8Bit(file.readAll());
}

QString TemplateRootPath()
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("模板"));
}

QString ResolveEngineTemplateDirectory(const QString &gameEngine)
{
    const QString engine = gameEngine.trimmed();
    if (engine.contains(QStringLiteral("996M2"), Qt::CaseInsensitive)) {
        return QStringLiteral("996M2脚本");
    }
    if (engine.contains(QStringLiteral("BLUE"), Qt::CaseInsensitive)) {
        return QStringLiteral("BLUE脚本");
    }
    if (engine.contains(QStringLiteral("GEE"), Qt::CaseInsensitive)
        || engine.contains(QStringLiteral("翎风"), Qt::CaseInsensitive)
        || engine.contains(QStringLiteral("LF"), Qt::CaseInsensitive)) {
        return QStringLiteral("GEE脚本");
    }
    if (engine.compare(QStringLiteral("GOM"), Qt::CaseInsensitive) == 0
        || (engine.contains(QStringLiteral("GOM"), Qt::CaseInsensitive)
            && !engine.contains(QStringLiteral("新GOM"), Qt::CaseInsensitive))) {
        return QStringLiteral("GOM脚本");
    }
    if (engine.contains(QStringLiteral("HGE"), Qt::CaseInsensitive)) {
        return QStringLiteral("HGE脚本");
    }
    if (engine.contains(QStringLiteral("新GOM"), Qt::CaseInsensitive)) {
        return QStringLiteral("NEWGOM脚本");
    }
    if (engine.contains(QStringLiteral("OTHER"), Qt::CaseInsensitive)
        || engine.contains(QStringLiteral("其他"), Qt::CaseInsensitive)
        || engine.contains(QStringLiteral("Herom2"), Qt::CaseInsensitive)) {
        return QStringLiteral("OTHER脚本");
    }
    if (engine.contains(QStringLiteral("WOOOL"), Qt::CaseInsensitive)) {
        return QStringLiteral("WOOOL脚本");
    }

    return {};
}

QString ResolveTemplateFilePath(const QJsonObject &templateObject, const QString &fileName)
{
    const QString templateRoot = TemplateRootPath();
    const QString engineDirectory = ResolveEngineTemplateDirectory(ReadStringField(templateObject,
                                                                                   {QStringLiteral("GameEngine"), QStringLiteral("gameEngine")}));
    if (!engineDirectory.isEmpty()) {
        const QString engineFilePath = QDir(QDir(templateRoot).filePath(engineDirectory)).filePath(fileName);
        if (QFileInfo::exists(engineFilePath)) {
            return engineFilePath;
        }
    }

    const QString directFilePath = QDir(templateRoot).filePath(fileName);
    return QFileInfo::exists(directFilePath) ? directFilePath : QString();
}

QString ReadTemplateText(const QJsonObject &templateObject, const QString &fileName)
{
    const QString templateFilePath = ResolveTemplateFilePath(templateObject, fileName);
    if (templateFilePath.isEmpty()) {
        return {};
    }

    return ReadExistingTextFile(templateFilePath);
}

QString RequireTemplateText(const QJsonObject &templateObject, const QString &fileName, QString *errorMessage)
{
    const QString text = ReadTemplateText(templateObject, fileName);
    if (!text.isEmpty()) {
        return text;
    }

    if (errorMessage) {
        *errorMessage = QStringLiteral("未找到安装脚本模板：%1").arg(fileName);
    }
    return {};
}

QString RelativePlatformPath()
{
    return QStringLiteral("..\\..\\..\\..\\平台验证");
}

QString NormalizeScriptPath(QString path)
{
    path = QDir::toNativeSeparators(path);
    return path.replace(QLatin1Char('/'), QLatin1Char('\\'));
}

QString GetExtendedInstruction(const QJsonObject &templateObject)
{
    const QString engine = ReadStringField(templateObject, {QStringLiteral("GameEngine"), QStringLiteral("gameEngine")}).trimmed().toLower();
    if (engine == QLatin1String("gom") || engine == QLatin1String("gee")) {
        return QStringLiteral("0");
    }
    if (engine == QLatin1String("blue") || engine == QLatin1String("bluem2") || engine == QLatin1String("blue2017")) {
        return QStringLiteral("HardDisk");
    }
    if (engine == QLatin1String("hgem2")) {
        return QStringLiteral("0 Force");
    }
    return {};
}

QString LoadRechargeTemplateFileName(int type)
{
    switch (type) {
    case 2:
        return QStringLiteral("通区测试充值.txt");
    case 0:
    case 1:
    case 3:
    default:
        return QStringLiteral("充值.txt");
    }
}

QString ExtractCommentBlock(const QString &text, QString *wrappedBlock = nullptr)
{
    const int startIndex = text.indexOf(QStringLiteral("<!--"));
    const int endIndex = text.indexOf(QStringLiteral("-->"), startIndex + 4);
    if (startIndex < 0 || endIndex < 0) {
        if (wrappedBlock) {
            wrappedBlock->clear();
        }
        return {};
    }

    if (wrappedBlock) {
        *wrappedBlock = text.mid(startIndex, endIndex - startIndex + 3);
    }

    return text.mid(startIndex + 4, endIndex - startIndex - 4).trimmed();
}

QString ReplaceWrappedBlock(const QString &text, const QString &wrappedBlock, const QString &replacement)
{
    if (wrappedBlock.isEmpty()) {
        return text;
    }
    return QString(text).replace(wrappedBlock, replacement);
}

QString ReplaceSection(const QString &text, const QString &startMarker, const QString &endMarker, const QString &replacement)
{
    const int startIndex = text.indexOf(startMarker, 0, Qt::CaseInsensitive);
    const int endIndex = text.indexOf(endMarker, startIndex < 0 ? 0 : startIndex, Qt::CaseInsensitive);
    if (startIndex < 0 || endIndex < 0 || endIndex < startIndex) {
        return text;
    }

    return text.left(startIndex) + replacement + text.mid(endIndex + endMarker.size());
}

QString RemoveSection(const QString &text, const QString &startMarker, const QString &endMarker)
{
    return ReplaceSection(text, startMarker, endMarker, QString());
}

QString BuildRewardStoragePath(const InstallContext &context,
                               const QString &payDir,
                               const QString &currencyName,
                               const QStringList &segments)
{
    QString basePath;
    if (context.isTongQu) {
        if (context.isDir == 0) {
            basePath = QStringLiteral("..\\..\\..\\..\\%1\\Mir200\\Envir\\QuestDiary\\%2\\充值%3")
                           .arg(QFileInfo(context.tongQuDir).fileName(), payDir, currencyName);
        } else {
            basePath = QDir(context.tongQuDir).filePath(QStringLiteral("Mir200/Envir/QuestDiary/%1/充值%2").arg(payDir, currencyName));
        }
    } else {
        basePath = QStringLiteral("..\\QuestDiary\\%1\\充值%2").arg(payDir, currencyName);
    }

    for (const auto &segment : segments) {
        if (!segment.trimmed().isEmpty()) {
            basePath = QDir(basePath).filePath(segment.trimmed());
        }
    }

    return NormalizeScriptPath(basePath);
}

QString BuildEngineCommandToken(const QJsonObject &templateObject, const QString &currencyName)
{
    if (currencyName == QStringLiteral("元宝")) {
        return QStringLiteral("GAMEGOLD");
    }

    const QString engine = ReadStringField(templateObject, {QStringLiteral("GameEngine"), QStringLiteral("gameEngine")});
    if (currencyName == QStringLiteral("金币")) {
        return engine.contains(QStringLiteral("翎风"), Qt::CaseInsensitive)
                   ? QStringLiteral("GOLDCOUNT")
                   : QStringLiteral("give 金币");
    }

    const QString scriptCommand = ReadStringField(templateObject, {QStringLiteral("ScriptCommand"), QStringLiteral("scriptCommand")}).trimmed();
    return scriptCommand.section(QLatin1Char(' '), 0, 0);
}

QString BuildTransferMirCommandBlock(const QJsonArray &integralGives)
{
    QString block;
    const QStringList builtIns = {
        QStringLiteral("GAMEGOLD"),
        QStringLiteral("GAMEDIAMOND"),
        QStringLiteral("GAMEGLORY"),
        QStringLiteral("GAMEPOINT"),
        QStringLiteral("GAMEGIRD"),
        QStringLiteral("CREDITPOINT")
    };

    for (int index = 0; index < builtIns.size(); ++index) {
        const int variableIndex = 11 + index;
        block += QStringLiteral("MOV P%1 <$STR(N15)>\r\nMUL P%1 1\r\n%2 + <$STR(P%1)>\r\n\r\n")
                     .arg(variableIndex)
                     .arg(builtIns.at(index));
    }

    int customIndex = 17;
    for (const auto &integralValue : integralGives) {
        if (!integralValue.isObject()) {
            continue;
        }
        const QJsonObject integralObject = integralValue.toObject();
        const int type = ReadIntField(integralObject, {QStringLiteral("Type"), QStringLiteral("type")});
        if (type == 0) {
            continue;
        }

        const QString name = ReadStringField(integralObject, {QStringLiteral("Name"), QStringLiteral("name")});
        const QString file = ReadStringField(integralObject, {QStringLiteral("File"), QStringLiteral("file")});
        if (name.isEmpty() || file.isEmpty()) {
            continue;
        }

        block += QStringLiteral("MOV P%1 <$STR(N15)>\r\nMUL P%1 1\r\nCALCVAR HUMAN %2 + <$STR(P%1)>\r\nSAVEVAR HUMAN %2 %3\r\n\r\n")
                     .arg(customIndex++)
                     .arg(name, file);
    }

    return block;
}

bool UpdateMerchantInfo(const QString &envirPath,
                        const QJsonObject &templateObject,
                        const QJsonObject &wxmbTemplateObject,
                        QString *errorMessage)
{
    const QString currencyName = ReadStringField(templateObject, {QStringLiteral("CurrencyName"), QStringLiteral("currencyName")});
    if (currencyName.isEmpty()) {
        return true;
    }

    QStringList npcLines;
    const QJsonArray templateNpcs = ReadArrayField(templateObject, {QStringLiteral("Npcs"), QStringLiteral("npcs")});
    for (const auto &npcValue : templateNpcs) {
        if (!npcValue.isObject()) {
            continue;
        }

        const QJsonObject npcObject = npcValue.toObject();
        const QString npcLine = BuildMerchantNpcLine(
            ReadStringField(npcObject, {QStringLiteral("Name"), QStringLiteral("name")}),
            ReadStringField(npcObject, {QStringLiteral("Map"), QStringLiteral("map")}),
            ReadStringField(npcObject, {QStringLiteral("XAxis"), QStringLiteral("xAxis"), QStringLiteral("X")}),
            ReadStringField(npcObject, {QStringLiteral("YAxis"), QStringLiteral("yAxis"), QStringLiteral("Y")}),
            ReadStringField(npcObject, {QStringLiteral("Looks"), QStringLiteral("looks"), QStringLiteral("LookId")}));
        if (!npcLine.isEmpty()) {
            npcLines.append(npcLine);
        }
    }

    const QString wxmbNpcName = ReadStringField(wxmbTemplateObject, {QStringLiteral("NpcName"), QStringLiteral("npcName")});
    const QJsonArray wxmbNpcs = ReadArrayField(wxmbTemplateObject, {QStringLiteral("npcs"), QStringLiteral("Npcs")});
    for (const auto &npcValue : wxmbNpcs) {
        if (!npcValue.isObject()) {
            continue;
        }

        const QJsonObject npcObject = npcValue.toObject();
        const QString npcLine = BuildMerchantNpcLine(
            wxmbNpcName,
            ReadStringField(npcObject, {QStringLiteral("MapId"), QStringLiteral("mapId")}),
            ReadStringField(npcObject, {QStringLiteral("X"), QStringLiteral("x")}),
            ReadStringField(npcObject, {QStringLiteral("Y"), QStringLiteral("y")}),
            ReadStringField(npcObject, {QStringLiteral("LookId"), QStringLiteral("lookId")}));
        if (!npcLine.isEmpty()) {
            npcLines.append(npcLine);
        }
    }

    const QString transferNpcName = ReadStringField(wxmbTemplateObject, {QStringLiteral("TransferNpcName"), QStringLiteral("transferNpcName")});
    const QJsonArray transferNpcs = ReadArrayField(wxmbTemplateObject, {QStringLiteral("transferNpcs"), QStringLiteral("TransferNpcs")});
    for (const auto &npcValue : transferNpcs) {
        if (!npcValue.isObject()) {
            continue;
        }

        const QJsonObject npcObject = npcValue.toObject();
        const QString npcLine = BuildMerchantNpcLine(
            transferNpcName,
            ReadStringField(npcObject, {QStringLiteral("MapId"), QStringLiteral("mapId")}),
            ReadStringField(npcObject, {QStringLiteral("X"), QStringLiteral("x")}),
            ReadStringField(npcObject, {QStringLiteral("Y"), QStringLiteral("y")}),
            ReadStringField(npcObject, {QStringLiteral("LookId"), QStringLiteral("lookId")}));
        if (!npcLine.isEmpty()) {
            npcLines.append(npcLine);
        }
    }

    const QString merchantFilePath = QDir(envirPath).filePath(QStringLiteral("Merchant.txt"));
    QString text = ReadExistingTextFile(merchantFilePath);

    QString newBlock = QStringLiteral(";7XPAY %1\r\n").arg(currencyName);
    for (const auto &line : npcLines) {
        newBlock += line + QStringLiteral("\r\n");
    }
    newBlock += QStringLiteral(";网络 %1\r\n").arg(currencyName);

    const QString startMarker = QStringLiteral(";7XPAY %1").arg(currencyName);
    const QString endMarker = QStringLiteral(";网络 %1").arg(currencyName);
    const int startIndex = text.indexOf(startMarker);
    const int endIndex = startIndex >= 0 ? text.indexOf(endMarker, startIndex + startMarker.size()) : -1;

    if (startIndex >= 0 && endIndex >= 0) {
        const int replaceEnd = endIndex + endMarker.size();
        text = text.left(startIndex) + newBlock + text.mid(replaceEnd);
    } else if (text.isEmpty()) {
        text = newBlock;
    } else {
        if (!text.endsWith(QLatin1String("\n")) && !text.endsWith(QLatin1String("\r"))) {
            text += QStringLiteral("\r\n");
        }
        text += newBlock;
    }

    return WriteTextFile(merchantFilePath, text, errorMessage);
}

QString BuildMarketScriptFileName(const QString &name, const QString &map)
{
    if (name.trimmed().isEmpty() || map.trimmed().isEmpty()) {
        return {};
    }

    return QStringLiteral("%1-%2.txt").arg(name.trimmed(), map.trimmed());
}

bool WriteMarketScript(const QString &filePath,
                       const QString &title,
                       const QStringList &details,
                       QString *errorMessage)
{
    return WriteTextFile(filePath, BuildScriptContent(title, details), errorMessage);
}

QString BuildRechargeNpcScript(const QString &currencyName,
                               const QString &partitionId,
                               const QString &partitionUuid,
                               const QString &payDir,
                               const QString &browserCommand,
                               const QString &transferCoinName,
                               const QJsonArray &products,
                               int scanMode,
                               bool wxmbEnabled,
                               bool transferEnabled)
{
    QStringList lines;
    lines << QStringLiteral("[@main]")
          << QStringLiteral("#SAY")
          << QStringLiteral("欢迎使用%1充值服务\\").arg(currencyName.isEmpty() ? QStringLiteral("网关") : currencyName)
          << QStringLiteral("分区ID：%1\\").arg(partitionId.isEmpty() ? QStringLiteral("<empty>") : partitionId)
          << QStringLiteral("脚本目录：%1\\").arg(payDir.isEmpty() ? QStringLiteral("<empty>") : payDir);

    QStringList menus;
    menus << QStringLiteral("<领取充值/@领取>");
    if (scanMode == 0 || scanMode == 1) {
        menus << QStringLiteral("<扫码充值/@扫码充值>");
    }
    if (scanMode == 1 || scanMode == 2) {
        menus << QStringLiteral("<网页充值/@网页充值>");
    }
    if (wxmbEnabled && transferEnabled) {
        menus << QStringLiteral("<游戏币充值/@游戏币充值>");
    }
    menus << QStringLiteral("<关闭/@exit>");
    lines << menus.join(QStringLiteral("  ┆ ")) + QStringLiteral("\\");

    lines << QStringLiteral("[@领取]")
          << QStringLiteral("#SAY")
          << QStringLiteral("请检查 QuestDiary/%1/充值%2 目录下的脚本内容。\\").arg(payDir, currencyName)
          << QStringLiteral("<返回/@main>");

    if (scanMode == 0 || scanMode == 1) {
        QStringList productMenus;
        QStringList productSections;
        int productIndex = 1;
        for (const auto &productValue : products) {
            if (!productValue.isObject()) {
                continue;
            }

            const QJsonObject productObject = productValue.toObject();
            const QString productName = ReadStringField(productObject, {QStringLiteral("Name"), QStringLiteral("name")});
            const QString productId = ReadStringField(productObject, {QStringLiteral("Id"), QStringLiteral("id")});
            const QString markName = ReadStringField(productObject, {QStringLiteral("MarkName"), QStringLiteral("markName")});
            const QString amountText = ReadStringField(productObject, {QStringLiteral("Amount"), QStringLiteral("amount"), QStringLiteral("Price"), QStringLiteral("price")});
            if (productName.isEmpty()) {
                continue;
            }

            const QString sectionName = QStringLiteral("@扫码商品%1").arg(productIndex++);
            productMenus << QStringLiteral("<%1/%2>").arg(productName, sectionName);
            QStringList actCommands;
            if (!productId.isEmpty()) {
                actCommands << QStringLiteral("MOV S80 %1").arg(productId);
            }
            actCommands << QStringLiteral("MOV S81 %1").arg(productName);
            if (!amountText.isEmpty()) {
                actCommands << QStringLiteral("MOV S82 %1").arg(amountText);
            }
            actCommands << QStringLiteral("GOTO @扫码准备提交");

            productSections << BuildQuestCallScript(sectionName,
                                                    QStringLiteral("扫码充值商品"),
                                                    {
                                                        QStringLiteral("商品名称：%1").arg(productName),
                                                        QStringLiteral("商品ID：%1").arg(productId.isEmpty() ? QStringLiteral("<empty>") : productId),
                                                        QStringLiteral("分类：%1").arg(markName.isEmpty() ? QStringLiteral("<empty>") : markName),
                                                        QStringLiteral("金额：%1").arg(amountText.isEmpty() ? QStringLiteral("<empty>") : amountText),
                                                        QStringLiteral("提交文件：%1_提交数据.txt").arg(partitionId.isEmpty() ? QStringLiteral("<empty>") : partitionId)
                                                    },
                                                    actCommands);
        }

        lines << QStringLiteral("[@扫码充值]")
              << QStringLiteral("#SAY")
              << QStringLiteral("扫码充值目录已创建：平台验证\\充值二维码\\充值提交\\")
              << QStringLiteral("提交文件：%1_提交数据.txt\\").arg(partitionId.isEmpty() ? QStringLiteral("<empty>") : partitionId)
              << BuildMenuLines(productMenus, QStringLiteral("商品菜单待补充"))
              << QStringLiteral("<返回/@main>");

        for (const auto &section : productSections) {
            lines << section.trimmed();
        }

        lines << BuildNpcForwardSection(QStringLiteral("@扫码准备提交"),
                                        BuildQuestCallPath(payDir, {QStringLiteral("充值%1").arg(currencyName), QStringLiteral("扫码提交"), QStringLiteral("提交扫码数据.txt")}),
                                        QStringLiteral("@扫码准备提交"),
                                        {
                                            QStringLiteral("扫码充值数据已准备提交\\"),
                                            QStringLiteral("提交文件：%1_提交数据.txt\\").arg(partitionId.isEmpty() ? QStringLiteral("<empty>") : partitionId)
                                        },
                                        QStringLiteral("<返回/@扫码充值>"));
    }

    if (scanMode == 1 || scanMode == 2) {
        lines << BuildNpcForwardSection(QStringLiteral("@网页充值"),
                                        BuildQuestCallPath(payDir, {QStringLiteral("充值%1").arg(currencyName), QStringLiteral("网页充值"), QStringLiteral("网页充值.txt")}),
                                        QStringLiteral("@网页充值"),
                                        {QStringLiteral("网页充值入口已转到 QuestDiary 脚本\\")},
                                        QStringLiteral("<返回/@main>"));
    }

    if (wxmbEnabled && transferEnabled) {
        lines << BuildNpcForwardSection(QStringLiteral("@游戏币充值"),
                                        BuildQuestCallPath(payDir, {QStringLiteral("充值%1").arg(currencyName), QStringLiteral("转区功能"), QStringLiteral("转区点.txt")}),
                                        QStringLiteral("@领取转区点"),
                                        {QStringLiteral("转区币名称：%1\\").arg(transferCoinName.isEmpty() ? QStringLiteral("<empty>") : transferCoinName)},
                                        QStringLiteral("<返回/@main>"));
    }

    return lines.join(QStringLiteral("\r\n")) + QStringLiteral("\r\n");
}

QString BuildWxmbNpcScript(const QString &partitionId,
                           const QString &payDir,
                           const QString &machineVar,
                           const QString &openidVar,
                           const QString &wxVar)
{
    QStringList lines;
    lines << QStringLiteral("[@main]")
          << QStringLiteral("#SAY")
          << QStringLiteral("微信密保服务\\")
          << QStringLiteral("分区ID：%1\\").arg(partitionId.isEmpty() ? QStringLiteral("<empty>") : partitionId)
          << QStringLiteral("机器码变量：%1\\").arg(machineVar.isEmpty() ? QStringLiteral("<empty>") : machineVar)
          << QStringLiteral("OpenId变量：%1\\").arg(openidVar.isEmpty() ? QStringLiteral("<empty>") : openidVar)
          << QStringLiteral("微信变量：%1\\").arg(wxVar.isEmpty() ? QStringLiteral("<empty>") : wxVar)
          << QStringLiteral("<登录检测/@检测密保>  ┆  <提交认证/@认证准备提交>  ┆  <关闭/@exit>\\");

    lines << BuildNpcForwardSection(QStringLiteral("@检测密保"),
                                    BuildQuestCallPath(payDir, {QStringLiteral("微信密保"), QStringLiteral("登录检测.txt")}),
                                    QStringLiteral("@检测密保"),
                                    {QStringLiteral("已调用微信密保登录检测脚本\\")},
                                    QStringLiteral("<关闭/@exit>"));

    lines << BuildNpcForwardSection(QStringLiteral("@认证准备提交"),
                                    BuildQuestCallPath(payDir, {QStringLiteral("微信密保"), QStringLiteral("提交认证.txt")}),
                                    QStringLiteral("@认证准备提交"),
                                    {QStringLiteral("已调用微信密保提交脚本\\")},
                                    QStringLiteral("<关闭/@exit>"));
    return lines.join(QStringLiteral("\r\n")) + QStringLiteral("\r\n");
}

QString BuildTransferNpcScript(const QString &partitionId,
                               const QString &payDir,
                               const QString &currencyName,
                               const QString &transferCoinName,
                               const QString &transferRechargeVar,
                               const QString &transferUsedVar)
{
    QStringList lines;
    lines << QStringLiteral("[@main]")
          << QStringLiteral("#SAY")
          << QStringLiteral("自助转区服务\\")
          << QStringLiteral("分区ID：%1\\").arg(partitionId.isEmpty() ? QStringLiteral("<empty>") : partitionId)
          << QStringLiteral("转区币名称：%1\\").arg(transferCoinName.isEmpty() ? QStringLiteral("<empty>") : transferCoinName)
          << QStringLiteral("充值变量：%1\\").arg(transferRechargeVar.isEmpty() ? QStringLiteral("<empty>") : transferRechargeVar)
          << QStringLiteral("使用变量：%1\\").arg(transferUsedVar.isEmpty() ? QStringLiteral("<empty>") : transferUsedVar)
          << QStringLiteral("<领取转区点/@领取转区点>  ┆  <提交转区/@转区准备提交>  ┆  <关闭/@exit>\\");

    lines << BuildNpcForwardSection(QStringLiteral("@领取转区点"),
                                    BuildQuestCallPath(payDir, {QStringLiteral("充值%1").arg(currencyName), QStringLiteral("转区功能"), QStringLiteral("转区点.txt")}),
                                    QStringLiteral("@领取转区点"),
                                    {QStringLiteral("已调用转区点脚本\\")},
                                    QStringLiteral("<关闭/@exit>"));

    lines << BuildNpcForwardSection(QStringLiteral("@转区准备提交"),
                                    BuildQuestCallPath(payDir, {QStringLiteral("充值%1").arg(currencyName), QStringLiteral("转区功能"), QStringLiteral("提交转区数据.txt")}),
                                    QStringLiteral("@转区准备提交"),
                                    {QStringLiteral("已调用转区提交脚本\\")},
                                    QStringLiteral("<关闭/@exit>"));
    return lines.join(QStringLiteral("\r\n")) + QStringLiteral("\r\n");
}

QString BuildQuestCallPath(const QString &payDir, const QStringList &segments)
{
    QString path = QStringLiteral("\\") + payDir;
    for (const auto &segment : segments) {
        if (!segment.trimmed().isEmpty()) {
            path += QStringLiteral("\\") + segment.trimmed();
        }
    }
    return path;
}

QString BuildRechargeReceiveScript(const QString &payDir,
                                   const QString &currencyName,
                                   const QJsonArray &integralGives,
                                   const QJsonArray &additionalGives,
                                   bool transferEnabled,
                                   bool hasEquipGive)
{
    QStringList lines;
    lines << QStringLiteral("[@领取]")
          << QStringLiteral("#IF")
          << QStringLiteral("#ACT")
          << QStringLiteral("#CALL [%1] @领取%2")
                 .arg(BuildQuestCallPath(payDir, {QStringLiteral("充值%1").arg(currencyName), currencyName, QStringLiteral("%1.txt").arg(currencyName)}),
                      currencyName);

    for (const auto &integralValue : integralGives) {
        if (!integralValue.isObject()) {
            continue;
        }

        const QJsonObject integralObject = integralValue.toObject();
        const QString name = ReadStringField(integralObject, {QStringLiteral("Name"), QStringLiteral("name")});
        if (name.isEmpty()) {
            continue;
        }

        lines << QStringLiteral("#CALL [%1] @领取%2")
                     .arg(BuildQuestCallPath(payDir, {QStringLiteral("充值%1").arg(currencyName), QStringLiteral("积分充值"), QStringLiteral("%1充值").arg(name), QStringLiteral("%1.txt").arg(name)}),
                          name);
    }

    for (const auto &additionalValue : additionalGives) {
        if (!additionalValue.isObject()) {
            continue;
        }

        const QJsonObject additionalObject = additionalValue.toObject();
        const QString name = ReadStringField(additionalObject, {QStringLiteral("Name"), QStringLiteral("name")});
        if (name.isEmpty()) {
            continue;
        }

        lines << QStringLiteral("#CALL [%1] @领取%2")
                     .arg(BuildQuestCallPath(payDir, {QStringLiteral("充值%1").arg(currencyName), QStringLiteral("附加赠送"), name, QStringLiteral("%1.txt").arg(name)}),
                          name);
    }

    if (hasEquipGive) {
        lines << QStringLiteral("#CALL [%1] @领取装备")
                     .arg(BuildQuestCallPath(payDir, {QStringLiteral("充值%1").arg(currencyName), QStringLiteral("装备赠送"), QStringLiteral("装备.txt")}));
    }

    if (transferEnabled) {
        lines << QStringLiteral("#CALL [%1] @领取转区点")
                     .arg(BuildQuestCallPath(payDir, {QStringLiteral("充值%1").arg(currencyName), QStringLiteral("转区功能"), QStringLiteral("转区点.txt")}));
    }

    lines << QStringLiteral("<返回/@main>");
    return lines.join(QStringLiteral("\r\n")) + QStringLiteral("\r\n");
}

bool UpdateMarketDefScripts(const QString &envirPath,
                            const QJsonObject &partitionObject,
                            const QJsonObject &templateObject,
                            const InstallContext &installContext,
                            const QJsonObject &scanObject,
                            const QJsonArray &circuits,
                            const QJsonArray &products,
                            const QJsonObject &wxmbTemplateObject,
                            QString *errorMessage)
{
    const QString marketDefPath = QDir(envirPath).filePath(QStringLiteral("Market_Def"));
    const QString currencyName = ReadStringField(templateObject, {QStringLiteral("CurrencyName"), QStringLiteral("currencyName")});
    const QString payDir = ReadStringField(templateObject, {QStringLiteral("PayDir"), QStringLiteral("payDir")});
    const QString partitionId = ReadStringField(partitionObject, {QStringLiteral("Id"), QStringLiteral("id")});
    const int scanMode = ReadIntField(partitionObject, {QStringLiteral("Scan"), QStringLiteral("scan")});
    const bool wxmbEnabled = ReadIntField(templateObject, {QStringLiteral("IsWxmb"), QStringLiteral("isWxmb")}) != 0;
    const bool transferEnabled = ReadBoolField(wxmbTemplateObject, {QStringLiteral("TransferEnable"), QStringLiteral("transferEnable")});
    const QString machineVar = ReadStringField(wxmbTemplateObject, {QStringLiteral("MachineVar"), QStringLiteral("machineVar")});
    const QString openidVar = ReadStringField(wxmbTemplateObject, {QStringLiteral("OpenidVar"), QStringLiteral("openidVar")});
    const QString wxVar = ReadStringField(wxmbTemplateObject, {QStringLiteral("WxVar"), QStringLiteral("wxVar")});
    const QString transferCoinName = ReadStringField(wxmbTemplateObject, {QStringLiteral("TransferCoinName"), QStringLiteral("transferCoinName")});
    const QString transferRechargeVar = ReadStringField(wxmbTemplateObject, {QStringLiteral("TransferRechargeVar"), QStringLiteral("transferRechargeVar")});
    const QString transferUsedVar = ReadStringField(wxmbTemplateObject, {QStringLiteral("TransferUsedVar"), QStringLiteral("transferUsedVar")});
    const QString transferCoinVar = ReadStringField(wxmbTemplateObject, {QStringLiteral("TransferCoinVar"), QStringLiteral("transferCoinVar")});
    const QString templateId = ReadStringField(partitionObject, {QStringLiteral("TemplateId"), QStringLiteral("templateId")});
    const QString partitionUuid = ReadStringField(partitionObject, {QStringLiteral("Uuid"), QStringLiteral("uuid")});
    const QString browserCommand = ReadStringField(templateObject, {QStringLiteral("BrowserCommand"), QStringLiteral("browserCommand")});
    const QJsonArray integralGives = ReadArrayField(templateObject, {QStringLiteral("IntegralGives"), QStringLiteral("integralGives")});
    const QJsonArray additionalGives = ReadArrayField(templateObject, {QStringLiteral("AdditionalGives"), QStringLiteral("additionalGives")});
    const bool hasEquipGive = !ReadArrayField(templateObject, {QStringLiteral("EquipGives"), QStringLiteral("equipGives")}).isEmpty();
    const QString npcTemplateText = RequireTemplateText(templateObject, QStringLiteral("NPC.txt"), errorMessage);
    const QString wxmbTemplateText = wxmbEnabled ? RequireTemplateText(templateObject, QStringLiteral("微信密保.txt"), errorMessage) : QString();
    const QString transferTemplateText = (wxmbEnabled && transferEnabled) ? RequireTemplateText(templateObject, QStringLiteral("自助转区.txt"), errorMessage) : QString();
    if (npcTemplateText.isEmpty() || (wxmbEnabled && wxmbTemplateText.isEmpty()) || (wxmbEnabled && transferEnabled && transferTemplateText.isEmpty())) {
        return false;
    }

    const QString commandToken = BuildEngineCommandToken(templateObject, currencyName);
    const QString ratioText = FormatNumber(ReadDoubleField(templateObject, {QStringLiteral("Ratio"), QStringLiteral("ratio")}, 1.0));
    const QString resourceCode = ReadStringField(scanObject, {QStringLiteral("ResourceCode"), QStringLiteral("resourceCode")});
    const QString imageCode = ReadStringField(scanObject, {QStringLiteral("ImageCode"), QStringLiteral("imageCode")});
    const QString xOffset = ReadStringField(scanObject, {QStringLiteral("XOffset"), QStringLiteral("xOffset")});
    const QString yOffset = ReadStringField(scanObject, {QStringLiteral("YOffset"), QStringLiteral("yOffset")});
    const QString serial = ReadStringField(scanObject, {QStringLiteral("Serial"), QStringLiteral("serial")});

    auto replacePlaceholderLine = [](QString text, const QString &placeholder, const QString &replacement) {
        return text.replace(placeholder, replacement);
    };

    auto buildGiveCallBlock = [&](bool redPacketMode) {
        QStringList calls;
        const QString rechargePrefix = redPacketMode
                                           ? QStringLiteral("充值%1\\红包赠送").arg(currencyName)
                                           : QStringLiteral("充值%1").arg(currencyName);

        calls << QStringLiteral("#CALL [\\%1\\%2\\%2.txt] @领取%2").arg(payDir + (payDir.isEmpty() ? QString() : QStringLiteral("\\")) + rechargePrefix,
                                                                              currencyName);

        for (const auto &integralValue : integralGives) {
            if (!integralValue.isObject()) {
                continue;
            }
            const QJsonObject integralObject = integralValue.toObject();
            const int type = ReadIntField(integralObject, {QStringLiteral("Type"), QStringLiteral("type")});
            const QString name = ReadStringField(integralObject, {QStringLiteral("Name"), QStringLiteral("name")});
            if (type == 0 || name.isEmpty()) {
                continue;
            }
            calls << QStringLiteral("#CALL [\\%1\\积分充值\\%2充值\\%2.txt] @领取%2")
                         .arg(payDir + (payDir.isEmpty() ? QString() : QStringLiteral("\\")) + rechargePrefix,
                              name);
        }

        if (hasEquipGive) {
            calls << QStringLiteral("#CALL [\\%1\\装备赠送\\装备.txt] @领取装备")
                         .arg(payDir + (payDir.isEmpty() ? QString() : QStringLiteral("\\")) + rechargePrefix);
        }

        for (const auto &additionalValue : additionalGives) {
            if (!additionalValue.isObject()) {
                continue;
            }
            const QJsonObject additionalObject = additionalValue.toObject();
            const int type = ReadIntField(additionalObject, {QStringLiteral("Type"), QStringLiteral("type")});
            const QString name = ReadStringField(additionalObject, {QStringLiteral("Name"), QStringLiteral("name")});
            if (type == 0 || name.isEmpty()) {
                continue;
            }
            calls << QStringLiteral("#CALL [\\%1\\附加赠送\\%2\\%2.txt] @领取%2")
                         .arg(payDir + (payDir.isEmpty() ? QString() : QStringLiteral("\\")) + rechargePrefix,
                              name);
        }

        if (wxmbEnabled && transferEnabled) {
            calls << QStringLiteral("#CALL [\\%1\\充值%2\\转区功能\\转区点.txt] @领取转区点").arg(payDir, currencyName);
        }

        return calls.join(QStringLiteral("\r\n"));
    };

    auto groupedProductMenu = [&]() {
        QStringList lines;
        QMap<QString, QStringList> groupedEntries;
        int inputIndex = 21;
        for (const auto &productValue : products) {
            if (!productValue.isObject()) {
                continue;
            }
            const QJsonObject productObject = productValue.toObject();
            const QString markName = ReadStringField(productObject, {QStringLiteral("MarkName"), QStringLiteral("markName")});
            const QString productName = ReadStringField(productObject, {QStringLiteral("Name"), QStringLiteral("name")});
            if (productName.isEmpty()) {
                continue;
            }
            groupedEntries[markName].append(QStringLiteral("<%1/@@InPutInteger%2(请输入本次要充值的金额)>").arg(productName).arg(inputIndex++));
        }

        for (auto it = groupedEntries.cbegin(); it != groupedEntries.cend(); ++it) {
            const QString groupName = it.key().isEmpty() ? QStringLiteral("充值") : it.key();
            const auto entries = it.value();
            for (int index = 0; index < entries.size(); index += 3) {
                QString line = groupName + QStringLiteral("：") + entries.mid(index, 3).join(QStringLiteral("  "));
                lines << line + QStringLiteral("\\");
            }
        }

        return lines.join(QStringLiteral("\r\n"));
    };

    auto scanInputSections = [&]() {
        QStringList sections;
        int inputIndex = 21;
        for (const auto &productValue : products) {
            if (!productValue.isObject()) {
                continue;
            }
            const QJsonObject productObject = productValue.toObject();
            const QString productId = ReadStringField(productObject, {QStringLiteral("Id"), QStringLiteral("id")});
            const QString productName = ReadStringField(productObject, {QStringLiteral("Name"), QStringLiteral("name")});
            if (productName.isEmpty()) {
                continue;
            }
            sections << QStringLiteral("[@InPutInteger%1]\r\n#IF\r\nCHECKLEVELEX > 0\r\n#ACT\r\nMOV S81 %2\r\nMOV S82 %3\r\nMOV N80 <$STR(N%1)>\r\nGOTO @提交扫码数据\r\nbreak")
                            .arg(inputIndex++)
                            .arg(productId.isEmpty() ? QStringLiteral("0") : productId, productName);
        }
        return sections.join(QStringLiteral("\r\n"));
    };

    auto webCircuitMenu = [&]() {
        QStringList lineEntries;
        for (const auto &circuitValue : circuits) {
            if (!circuitValue.isObject()) {
                continue;
            }
            const QJsonObject circuitObject = circuitValue.toObject();
            const QString circuitName = ReadStringField(circuitObject, {QStringLiteral("Name"), QStringLiteral("name")});
            if (!circuitName.isEmpty()) {
                lineEntries << QStringLiteral("<%1/@%1>").arg(circuitName);
            }
        }
        return BuildMenuLines(lineEntries, QStringLiteral("充值通道待补充"));
    };

    auto webCircuitSections = [&]() {
        QStringList sections;
        for (const auto &circuitValue : circuits) {
            if (!circuitValue.isObject()) {
                continue;
            }
            const QJsonObject circuitObject = circuitValue.toObject();
            const QString circuitName = ReadStringField(circuitObject, {QStringLiteral("Name"), QStringLiteral("name")});
            const QString domainName = ReadStringField(circuitObject, {QStringLiteral("DomainName"), QStringLiteral("domainName")});
            const QString port = ReadStringField(circuitObject, {QStringLiteral("Port"), QStringLiteral("port")});
            if (circuitName.isEmpty()) {
                continue;
            }
            sections << QStringLiteral("[@%1]\r\n#IF\r\n#ACT\r\n%2 %3:%4/Default/Partition/%5\r\n")
                            .arg(circuitName,
                                 browserCommand,
                                 domainName,
                                 port.isEmpty() ? QStringLiteral("80") : port,
                                 partitionUuid);
        }
        return sections.join(QStringLiteral("\r\n"));
    };

    const QJsonArray templateNpcs = ReadArrayField(templateObject, {QStringLiteral("Npcs"), QStringLiteral("npcs")});
    for (const auto &npcValue : templateNpcs) {
        if (!npcValue.isObject()) {
            continue;
        }

        const QJsonObject npcObject = npcValue.toObject();
        const QString npcName = ReadStringField(npcObject, {QStringLiteral("Name"), QStringLiteral("name")});
        const QString mapName = ReadStringField(npcObject, {QStringLiteral("Map"), QStringLiteral("map")});
        const QString fileName = BuildMarketScriptFileName(npcName, mapName);
        if (fileName.isEmpty()) {
            continue;
        }

        QString script = npcTemplateText;
        script.replace(QStringLiteral("#command#"), commandToken);
        script.replace(QStringLiteral("#type#"), currencyName);
        script.replace(QStringLiteral("#czbili#"), QStringLiteral("1:%1").arg(ratioText));
        script.replace(QStringLiteral("%cur_name%"), transferCoinName);
        script.replace(QStringLiteral("%currency%"), transferCoinVar);
        script.replace(QStringLiteral("${领取}"),
                       (installContext.isTongQu && installContext.buildType == 2)
                           ? QStringLiteral("#IF\r\nNOT CHECKTEXTLIST ..\\QuestDiary\\开区状态.txt 测试\r\n#ACT\r\n#CALL [\\测试充值记录\\测试领取.txt] @测试领取")
                           : QString());

        QString czButton = QStringLiteral("<领取充值/@领取>");
        if (scanMode == 0) {
            czButton += QStringLiteral(" ┆ <扫码充值/@扫码充值>");
        } else if (scanMode == 1) {
            czButton += QStringLiteral("┆ <网页充值/@网页充值> ┆ <扫码充值/@扫码充值>");
        } else if (scanMode == 2) {
            czButton += QStringLiteral(" ┆ <网页充值/@网页充值>");
        }
        if (wxmbEnabled && transferEnabled) {
            czButton += QStringLiteral(" ┆ <游戏币充值/@游戏币充值>");
        }
        czButton += QStringLiteral("  ┆ <退出/@exit>\\");
        script.replace(QStringLiteral("%CZ_button%"), czButton);
        script = replacePlaceholderLine(script, QStringLiteral("#czTdao#"), webCircuitMenu());
        script.replace(QStringLiteral("#czTdaoList#"), webCircuitSections());
        script = replacePlaceholderLine(script, QStringLiteral("#smTdao#"), groupedProductMenu());
        script = ReplaceSection(script, QStringLiteral(";====begin"), QStringLiteral(";====end"), QStringLiteral(";====begin\r\n%1\r\n;====end").arg(scanInputSections()));
        script.replace(QStringLiteral("%AreaID%"), partitionId);
        script.replace(QStringLiteral("%TemplateID%"), templateId);
        script.replace(QStringLiteral("%Wil%"), resourceCode);
        script.replace(QStringLiteral("%pic%"), imageCode);
        script.replace(QStringLiteral("%x%"), xOffset);
        script.replace(QStringLiteral("%y%"), yOffset);
        script.replace(QStringLiteral("%size%"), serial);
        script.replace(QStringLiteral("%path%"), RelativePlatformPath());
        script.replace(QStringLiteral("#mir2command#"), BuildTransferMirCommandBlock(integralGives));
        script.replace(QStringLiteral("#CALL_CZ##CALL_FJZS##CALL_JF##CALL_ZB##CALL_CT##CALL_ZQ#"),
                       buildGiveCallBlock(!ReadBoolField(npcObject, {QStringLiteral("Type"), QStringLiteral("type")}, true)));

        if (!transferEnabled || !wxmbEnabled) {
            script = RemoveSection(script, QStringLiteral(";<!----(通用币充值)"), QStringLiteral(";---->"));
        }
        if (scanMode == 0) {
            script = RemoveSection(script, QStringLiteral(";<!----(网页充值)"), QStringLiteral(";---->"));
        }
        if (scanMode == 2) {
            script = RemoveSection(script, QStringLiteral(";<!----(扫码充值)"), QStringLiteral(";---->"));
        }

        if (!WriteTextFile(QDir(marketDefPath).filePath(fileName), script, errorMessage)) {
            return false;
        }
    }

    const QString wxmbNpcName = ReadStringField(wxmbTemplateObject, {QStringLiteral("NpcName"), QStringLiteral("npcName")});
    const QJsonArray wxmbNpcs = ReadArrayField(wxmbTemplateObject, {QStringLiteral("npcs"), QStringLiteral("Npcs")});
    for (const auto &npcValue : wxmbNpcs) {
        if (!npcValue.isObject()) {
            continue;
        }

        const QJsonObject npcObject = npcValue.toObject();
        const QString mapName = ReadStringField(npcObject, {QStringLiteral("MapId"), QStringLiteral("mapId")});
        const QString fileName = BuildMarketScriptFileName(wxmbNpcName, mapName);
        if (fileName.isEmpty()) {
            continue;
        }

        QString script = wxmbTemplateText;
        script.replace(QStringLiteral("%AreaID%"), partitionId);
        script.replace(QStringLiteral("%path%"), RelativePlatformPath());
        script.replace(QStringLiteral("%machine%"), machineVar);
        script.replace(QStringLiteral("%wechatid%"), openidVar);
        script.replace(QStringLiteral("%wechatna%"), wxVar);
        if (!WriteTextFile(QDir(marketDefPath).filePath(fileName), script, errorMessage)) {
            return false;
        }
    }

    const QString transferNpcName = ReadStringField(wxmbTemplateObject, {QStringLiteral("TransferNpcName"), QStringLiteral("transferNpcName")});
    const QJsonArray transferNpcs = ReadArrayField(wxmbTemplateObject, {QStringLiteral("transferNpcs"), QStringLiteral("TransferNpcs")});
    for (const auto &npcValue : transferNpcs) {
        if (!npcValue.isObject()) {
            continue;
        }

        const QJsonObject npcObject = npcValue.toObject();
        const QString mapName = ReadStringField(npcObject, {QStringLiteral("MapId"), QStringLiteral("mapId")});
        const QString fileName = BuildMarketScriptFileName(transferNpcName, mapName);
        if (fileName.isEmpty()) {
            continue;
        }

        QString script = transferTemplateText;
        script.replace(QStringLiteral("%AreaID%"), partitionId);
        script.replace(QStringLiteral("%path%"), RelativePlatformPath());
        script.replace(QStringLiteral("%cur_name%"), transferCoinName);
        script.replace(QStringLiteral("%wechatid%"), openidVar);
        script.replace(QStringLiteral("%minimum%"), QString::number(int(ReadDoubleField(wxmbTemplateObject, {QStringLiteral("TransferMinAmount"), QStringLiteral("transferMinAmount")}, 0.0))));
        script.replace(QStringLiteral("%switchval%"), transferRechargeVar);
        script.replace(QStringLiteral("%usedval%"), transferUsedVar);
        script.replace(QStringLiteral("%isbind%"), ReadStringField(wxmbTemplateObject, {QStringLiteral("TransferFlagVar"), QStringLiteral("transferFlagVar")}));
        if (!WriteTextFile(QDir(marketDefPath).filePath(fileName), script, errorMessage)) {
            return false;
        }
    }

    return true;
}

bool CreateRechargeScriptSkeleton(const QString &envirPath,
                                  const QString &questDiaryPath,
                                  const QJsonObject &templateObject,
                                  const InstallContext &installContext,
                                  const QJsonArray &circuits,
                                  const QJsonObject &wxmbTemplateObject,
                                  const QJsonObject &partitionObject,
                                  QString *errorMessage)
{
    const QString currencyName = ReadStringField(templateObject, {QStringLiteral("CurrencyName"), QStringLiteral("currencyName")});
    if (currencyName.isEmpty()) {
        return true;
    }

    const QString partitionId = ReadStringField(partitionObject, {QStringLiteral("Id"), QStringLiteral("id")});
    const QString scriptCommand = ReadStringField(templateObject, {QStringLiteral("ScriptCommand"), QStringLiteral("scriptCommand")});
    const double ratio = ReadDoubleField(templateObject, {QStringLiteral("Ratio"), QStringLiteral("ratio")}, 1.0);
    const QString formattedRatio = FormatNumber(ratio);
    const QString transferRechargeVar = ReadStringField(wxmbTemplateObject, {QStringLiteral("TransferRechargeVar"), QStringLiteral("transferRechargeVar")});
    const QString payDir = ReadStringField(templateObject, {QStringLiteral("PayDir"), QStringLiteral("payDir")});
    const QJsonArray integralGives = ReadArrayField(templateObject, {QStringLiteral("IntegralGives"), QStringLiteral("integralGives")});
    const QJsonArray additionalGives = ReadArrayField(templateObject, {QStringLiteral("AdditionalGives"), QStringLiteral("additionalGives")});
    const bool hasEquipGive = !ReadArrayField(templateObject, {QStringLiteral("EquipGives"), QStringLiteral("equipGives")}).isEmpty();
    const QString gameEngine = ReadStringField(templateObject, {QStringLiteral("GameEngine"), QStringLiteral("gameEngine")});
    const QString rechargeTemplateFileName = LoadRechargeTemplateFileName(installContext.buildType);
    const QString rechargeTemplateText = RequireTemplateText(templateObject, rechargeTemplateFileName, errorMessage);
    if (rechargeTemplateText.isEmpty()) {
        return false;
    }
    const QString additionalTemplateText = RequireTemplateText(templateObject, QStringLiteral("附加赠送.txt"), errorMessage);
    const QString equipTemplateText = RequireTemplateText(templateObject, QStringLiteral("装备.txt"), errorMessage);
    const QString integralTemplateText = RequireTemplateText(templateObject, QStringLiteral("积分.txt"), errorMessage);
    const QString transferTemplateText = ReadStringField(wxmbTemplateObject, {QStringLiteral("TransferCoinName"), QStringLiteral("transferCoinName")}).isEmpty()
                                             ? QString()
                                             : RequireTemplateText(templateObject, QStringLiteral("转区点.txt"), errorMessage);
    if (additionalTemplateText.isEmpty() || equipTemplateText.isEmpty() || integralTemplateText.isEmpty()
        || (!ReadStringField(wxmbTemplateObject, {QStringLiteral("TransferCoinName"), QStringLiteral("transferCoinName")}).isEmpty() && transferTemplateText.isEmpty())) {
        return false;
    }

    const QString rechargeRootPath = QDir(questDiaryPath).filePath(QStringLiteral("充值%1").arg(currencyName));
    const QString currencyPath = QDir(rechargeRootPath).filePath(currencyName);
    const QString additionalPath = QDir(rechargeRootPath).filePath(QStringLiteral("附加赠送"));
    const QString equipPath = QDir(rechargeRootPath).filePath(QStringLiteral("装备赠送"));
    const QString integerPath = QDir(rechargeRootPath).filePath(QStringLiteral("积分充值"));
    const QString transferPath = QDir(rechargeRootPath).filePath(QStringLiteral("转区功能"));
    const QString rechargeIntegralPath = QDir(questDiaryPath).filePath(QStringLiteral("充值积分"));

    const QStringList directories = {
        rechargeRootPath,
        currencyPath,
        additionalPath,
        equipPath,
        integerPath,
        rechargeIntegralPath,
        transferPath
    };

    for (const auto &directory : directories) {
        if (!EnsureDirectoryExists(directory, errorMessage)) {
            return false;
        }
    }

    if (!EnsureAmountFiles(currencyPath, errorMessage)) {
        return false;
    }

    const QStringList files = {
        QDir(currencyPath).filePath(QStringLiteral("%1.txt").arg(currencyName)),
        QDir(equipPath).filePath(QStringLiteral("装备.txt"))
    };
    for (const auto &filePath : files) {
        if (!EnsureEmptyFileExists(filePath, errorMessage)) {
            return false;
        }
    }

    auto renderRechargeTemplate = [&](const QString &sourceText) {
        QString wrappedBlock;
        const QString commentBlock = ExtractCommentBlock(sourceText, &wrappedBlock);
        QString block = commentBlock;
        const QString rewardPath = BuildRewardStoragePath(installContext, payDir, currencyName, {currencyName});
        if (gameEngine.contains(QStringLiteral("blue"), Qt::CaseInsensitive)) {
            block.replace(QStringLiteral("..<FILEPATH><FILENAME>"), QStringLiteral("%1\\${充值路径}  HardDisk").arg(rewardPath));
        } else {
            block.replace(QStringLiteral("..<FILEPATH><FILENAME>"), QStringLiteral("%1\\${充值路径}").arg(rewardPath));
        }
        block.replace(QStringLiteral("${游戏币}"), QStringLiteral("${命令}"));
        block.replace(QStringLiteral("<$游戏币>"), QStringLiteral("${数量}%1").arg(currencyName));
        block.replace(QStringLiteral("<show>"), QString());
        block.replace(QStringLiteral("<!show>"), QString());

        QStringList generated;
        for (int amount = 1; amount < 10; ++amount) {
            QString item = block;
            item.replace(QStringLiteral("${RMB}"), QStringLiteral("0%1").arg(amount));
            item.replace(QStringLiteral("${充值路径}"), QStringLiteral("%1.txt").arg(amount, 2, 10, QLatin1Char('0')));
            item.replace(QStringLiteral("${数量}"), QStringLiteral("0"));
            item.replace(QStringLiteral("${命令}"), QStringLiteral("%1 0").arg(scriptCommand));
            generated << item;
        }
        for (const auto &fileName : BuildAmountFileNames()) {
            QString amountText = fileName;
            amountText.chop(4);
            bool ok = false;
            const int amount = amountText.toInt(&ok);
            if (!ok) {
                continue;
            }
            QString item = block;
            item.replace(QStringLiteral("${充值路径}"), fileName);
            item.replace(QStringLiteral("${RMB}"), QString::number(amount));
            item.replace(QStringLiteral("${数量}"), FormatNumber(amount * ratio));
            item.replace(QStringLiteral("${命令}"), QStringLiteral("%1 %2").arg(scriptCommand, FormatNumber(amount * ratio)));
            generated << item;
        }

        QString result = ReplaceWrappedBlock(sourceText, wrappedBlock, generated.join(QStringLiteral("\r\n")));
        result.replace(QStringLiteral("<游戏币>"), currencyName);
        result.replace(QStringLiteral("<$游戏币>"), currencyName);
        result.replace(QStringLiteral("%AreaID%"), partitionId);
        result.replace(QStringLiteral("${扩展指令}"), GetExtendedInstruction(templateObject));
        return result;
    };

    if (!WriteTextFile(QDir(currencyPath).filePath(QStringLiteral("%1.txt").arg(currencyName)), renderRechargeTemplate(rechargeTemplateText), errorMessage)) {
        return false;
    }

    const QString transferCoinName = ReadStringField(wxmbTemplateObject, {QStringLiteral("TransferCoinName"), QStringLiteral("transferCoinName")});
    if (!transferCoinName.isEmpty() && !EnsureAmountFiles(transferPath, errorMessage)) {
        return false;
    }

    for (const auto &integralValue : integralGives) {
        if (!integralValue.isObject()) {
            continue;
        }

        const QJsonObject integralObject = integralValue.toObject();
        const QString name = ReadStringField(integralObject, {QStringLiteral("Name"), QStringLiteral("name")});
        const QString fileName = ReadStringField(integralObject, {QStringLiteral("File"), QStringLiteral("file")});
        const double itemRatio = ReadDoubleField(integralObject, {QStringLiteral("Ratio"), QStringLiteral("ratio")}, 0.0);
        const int type = ReadIntField(integralObject, {QStringLiteral("Type"), QStringLiteral("type")});
        if (name.isEmpty() || type == 0) {
            continue;
        }

        const QString integralDir = QDir(integerPath).filePath(QStringLiteral("%1充值").arg(name));
        if (!EnsureDirectoryExists(integralDir, errorMessage)) {
            return false;
        }
        if (!EnsureAmountFiles(integralDir, errorMessage)) {
            return false;
        }
        const QString integralFilePath = QDir(integralDir).filePath(QStringLiteral("%1.txt").arg(name));
        if (!EnsureEmptyFileExists(integralFilePath, errorMessage)
            || !EnsureEmptyFileExists(QDir(rechargeIntegralPath).filePath(QFileInfo(fileName).fileName()), errorMessage)) {
            return false;
        }

        QString wrappedBlock;
        QString block = ExtractCommentBlock(integralTemplateText, &wrappedBlock);
        const QString rewardPath = BuildRewardStoragePath(installContext, payDir, currencyName, {QStringLiteral("积分充值"), QStringLiteral("%1充值").arg(name)});
        block.replace(QStringLiteral("..<FILEPATH><FILENAME>"), QStringLiteral("%1\\${#充值路径}.txt%2").arg(rewardPath, gameEngine.contains(QStringLiteral("blue"), Qt::CaseInsensitive) ? QStringLiteral(" HardDisk") : QString()));
        block.replace(QStringLiteral("${游戏币}"), currencyName);
        block.replace(QStringLiteral("${积分名称}]"), name);
        block.replace(QStringLiteral("<show>"), QString());
        block.replace(QStringLiteral("<!show>"), QString());
        QStringList generated;
        for (const auto &amountFile : BuildAmountFileNames()) {
            QString amountText = amountFile;
            amountText.chop(4);
            bool ok = false;
            const int amount = amountText.toInt(&ok);
            if (!ok) {
                continue;
            }
            QString item = block;
            item.replace(QStringLiteral("${RMB}"), amount < 10 ? QStringLiteral("0%1").arg(amount) : QString::number(amount));
            item.replace(QStringLiteral("${数量}"), amountFile.left(amountFile.size() - 4));
            item.replace(QStringLiteral("${#充值路径}"), amount < 10 ? QStringLiteral("0%1").arg(amount) : QString::number(amount));
            item.replace(QStringLiteral("${命令}"), QStringLiteral("%1 + %2").arg(name, FormatNumber(amount * itemRatio)));
            item.replace(QStringLiteral("${命令2}"), QStringLiteral("%1 %2").arg(name, fileName));
            item.replace(QStringLiteral("${积分名称}"), name);
            item.replace(QStringLiteral("${获得数量}"), QString::number(amount * itemRatio));
            generated << item;
        }
        QString result = ReplaceWrappedBlock(integralTemplateText, wrappedBlock, generated.join(QStringLiteral("\r\n")));
        result.replace(QStringLiteral("<积分>"), name);
        result.replace(QStringLiteral("${积分名称}"), name);
        result.replace(QStringLiteral("${扩展指令}"), GetExtendedInstruction(templateObject));
        if (!WriteTextFile(integralFilePath, result, errorMessage)) {
            return false;
        }
    }

    for (const auto &additionalValue : additionalGives) {
        if (!additionalValue.isObject()) {
            continue;
        }

        const QJsonObject additionalObject = additionalValue.toObject();
        const QString name = ReadStringField(additionalObject, {QStringLiteral("Name"), QStringLiteral("name")});
        const QString command = ReadStringField(additionalObject, {QStringLiteral("Command"), QStringLiteral("command")});
        const double itemRatio = ReadDoubleField(additionalObject, {QStringLiteral("Ratio"), QStringLiteral("ratio")}, 0.0);
        const int type = ReadIntField(additionalObject, {QStringLiteral("Type"), QStringLiteral("type")});
        if (name.isEmpty() || type == 0) {
            continue;
        }

        const QString additionalDir = QDir(additionalPath).filePath(name);
        if (!EnsureDirectoryExists(additionalDir, errorMessage)) {
            return false;
        }
        if (!EnsureAmountFiles(additionalDir, errorMessage)) {
            return false;
        }
        const QString additionalFilePath = QDir(additionalDir).filePath(QStringLiteral("%1.txt").arg(name));
        if (!EnsureEmptyFileExists(additionalFilePath, errorMessage)) {
            return false;
        }
        QString wrappedBlock;
        QString block = ExtractCommentBlock(additionalTemplateText, &wrappedBlock);
        const QString rewardPath = BuildRewardStoragePath(installContext, payDir, currencyName, {QStringLiteral("附加赠送"), name});
        block.replace(QStringLiteral("..<FILEPATH><FILENAME>"), QStringLiteral("%1\\${充值路径}%2").arg(rewardPath, gameEngine.contains(QStringLiteral("blue"), Qt::CaseInsensitive) ? QStringLiteral(" HardDisk") : QString()));
        block.replace(QStringLiteral("${附加名称}"), name);
        block.replace(QStringLiteral("<show>"), QString());
        block.replace(QStringLiteral("<!show>"), QString());
        QStringList generated;
        for (const auto &amountFile : BuildAmountFileNames()) {
            QString amountText = amountFile;
            amountText.chop(4);
            bool ok = false;
            const int amount = amountText.toInt(&ok);
            if (!ok) {
                continue;
            }
            QString item = block;
            item.replace(QStringLiteral("${充值路径}"), amountFile);
            item.replace(QStringLiteral("${RMB}"), amount < 10 ? QStringLiteral("0%1").arg(amount) : QString::number(amount));
            item.replace(QStringLiteral("${命令}"), QStringLiteral("%1 %2").arg(command, FormatNumber(amount * itemRatio)));
            item.replace(QStringLiteral("<$物品数量>"), QStringLiteral("%1%2").arg(FormatNumber(amount * itemRatio), name));
            generated << item;
        }
        QString result = ReplaceWrappedBlock(additionalTemplateText, wrappedBlock, generated.join(QStringLiteral("\r\n")));
        result.replace(QStringLiteral("<物品>"), name);
        result.replace(QStringLiteral("${附加名称}"), name);
        result.replace(QStringLiteral("${扩展指令}"), GetExtendedInstruction(templateObject));
        if (!WriteTextFile(additionalFilePath, result, errorMessage)) {
            return false;
        }
    }

    const QJsonArray equipGives = ReadArrayField(templateObject, {QStringLiteral("EquipGives"), QStringLiteral("equipGives")});
    QStringList equipSections;
    QString wrappedEquipBlock;
    const QString equipBlockTemplate = ExtractCommentBlock(equipTemplateText, &wrappedEquipBlock);
    for (const auto &equipValue : equipGives) {
        if (!equipValue.isObject()) {
            continue;
        }

        const QJsonObject equipObject = equipValue.toObject();
        const QString amount = ReadStringField(equipObject, {QStringLiteral("Amount"), QStringLiteral("amount")});
        const QString name = ReadStringField(equipObject, {QStringLiteral("Name"), QStringLiteral("name")});
        const QString command = ReadStringField(equipObject, {QStringLiteral("Command"), QStringLiteral("command")});
        if (amount.isEmpty()) {
            continue;
        }

        if (!EnsureEmptyFileExists(QDir(equipPath).filePath(QStringLiteral("%1.txt").arg(amount)), errorMessage)) {
            return false;
        }

        const QStringList commands = BuildEquipCommands(command, name);
        QString item = equipBlockTemplate;
        const QString rewardPath = BuildRewardStoragePath(installContext, payDir, currencyName, {QStringLiteral("装备赠送")});
        item.replace(QStringLiteral("..<FILEPATH><FILENAME>"), QStringLiteral("%1\\%2.txt%3").arg(rewardPath, amount, gameEngine.contains(QStringLiteral("blue"), Qt::CaseInsensitive) ? QStringLiteral(" HardDisk") : QString()));
        item.replace(QStringLiteral("${装备}"), name);
        item.replace(QStringLiteral("${命令}"), commands.join(QStringLiteral("\r\n")));
        item.replace(QStringLiteral("<$装备数量>"), name);
        item.replace(QStringLiteral("<show>"), QString());
        item.replace(QStringLiteral("<!show>"), QString());
        equipSections << item;
    }

    QString equipResult = ReplaceWrappedBlock(equipTemplateText, wrappedEquipBlock, equipSections.join(QStringLiteral("\r\n")));
    equipResult.replace(QStringLiteral("${扩展指令}"), GetExtendedInstruction(templateObject));
    if (!WriteTextFile(QDir(equipPath).filePath(QStringLiteral("装备.txt")), equipResult, errorMessage)) {
        return false;
    }

    if (!transferCoinName.isEmpty()) {
        QString wrappedTransferBlock;
        QString transferBlock = ExtractCommentBlock(transferTemplateText, &wrappedTransferBlock);
        const QString rewardPath = BuildRewardStoragePath(installContext, payDir, currencyName, {QStringLiteral("转区功能")});
        transferBlock.replace(QStringLiteral("..<FILEPATH><FILENAME>"), QStringLiteral("%1\\${充值路径}%2").arg(rewardPath, gameEngine.contains(QStringLiteral("blue"), Qt::CaseInsensitive) ? QStringLiteral(" HardDisk") : QString()));
        transferBlock.replace(QStringLiteral("${转区点}"), QStringLiteral("INC %1 ${数量}").arg(transferRechargeVar));

        QStringList generated;
        for (const auto &amountFile : BuildAmountFileNames()) {
            QString item = transferBlock;
            item.replace(QStringLiteral("${充值路径}"), amountFile);
            item.replace(QStringLiteral("${数量}"), amountFile.left(amountFile.size() - 4));
            generated << item;
        }

        QString transferResult = ReplaceWrappedBlock(transferTemplateText, wrappedTransferBlock, generated.join(QStringLiteral("\r\n")));
        transferResult.replace(QStringLiteral("<转区点>"), QStringLiteral("转区点"));
        if (!WriteTextFile(QDir(transferPath).filePath(QStringLiteral("转区点.txt")), transferResult, errorMessage)) {
            return false;
        }
    }

    return true;
}

bool EnsureLoginCheckHook(const QString &envirPath,
                          const QString &payDir,
                          QString *errorMessage)
{
    const QString qManageFilePath = QDir(envirPath).filePath(QStringLiteral("MapQuest_def/QManage.txt"));
    QFileInfo fileInfo(qManageFilePath);
    if (!fileInfo.exists()) {
        return true;
    }

    QString text = ReadExistingTextFile(qManageFilePath);
    if (text.contains(QStringLiteral("#CALL [\\%1\\微信密保\\登录检测.txt] @检测密保").arg(payDir), Qt::CaseInsensitive)) {
        return true;
    }

    const int loginIndex = text.indexOf(QStringLiteral("[@Login]"), 0, Qt::CaseInsensitive);
    if (loginIndex < 0) {
        return true;
    }

    int insertIndex = text.indexOf(QLatin1Char('\n'), loginIndex);
    if (insertIndex < 0) {
        insertIndex = text.size();
    } else {
        ++insertIndex;
    }

    const QString hookBlock = QStringLiteral("#IF\r\n#AC\r\n#CALL [\\%1\\微信密保\\登录检测.txt] @检测密保\r\n")
                                  .arg(payDir);
    text.insert(insertIndex, hookBlock + QStringLiteral("\r\n"));
    return WriteTextFile(qManageFilePath, text, errorMessage);
}

bool CreateWxMbScriptSkeleton(const QString &envirPath,
                              const QString &questDiaryPath,
                              const QJsonObject &partitionObject,
                              const QJsonObject &templateObject,
                              const QJsonObject &wxmbTemplateObject,
                              const QJsonObject &scanObject,
                              QString *errorMessage)
{
    const QString payDir = ReadStringField(templateObject, {QStringLiteral("PayDir"), QStringLiteral("payDir")});
    if (payDir.isEmpty()) {
        return true;
    }

    const QString wxmbPath = QDir(questDiaryPath).filePath(QStringLiteral("微信密保"));
    if (!EnsureDirectoryExists(wxmbPath, errorMessage)) {
        return false;
    }

    const QString partitionId = ReadStringField(partitionObject, {QStringLiteral("Id"), QStringLiteral("id")});
    const QString machineVar = ReadStringField(wxmbTemplateObject, {QStringLiteral("MachineVar"), QStringLiteral("machineVar")});
    const QString openidVar = ReadStringField(wxmbTemplateObject, {QStringLiteral("OpenidVar"), QStringLiteral("openidVar")});
    const QString wxVar = ReadStringField(wxmbTemplateObject, {QStringLiteral("WxVar"), QStringLiteral("wxVar")});
    const QString mbMapId = ReadStringField(wxmbTemplateObject, {QStringLiteral("MbMapId"), QStringLiteral("mbMapId")});
    const QString finishMapId = ReadStringField(wxmbTemplateObject, {QStringLiteral("FinishMapId"), QStringLiteral("finishMapId")});
    const QString transferFlagVar = ReadStringField(wxmbTemplateObject, {QStringLiteral("TransferFlagVar"), QStringLiteral("transferFlagVar")});
    const QString transferRechargeVar = ReadStringField(wxmbTemplateObject, {QStringLiteral("TransferRechargeVar"), QStringLiteral("transferRechargeVar")});
    const QString transferUsedVar = ReadStringField(wxmbTemplateObject, {QStringLiteral("TransferUsedVar"), QStringLiteral("transferUsedVar")});
    const QString transferCoinName = ReadStringField(wxmbTemplateObject, {QStringLiteral("TransferCoinName"), QStringLiteral("transferCoinName")});
    const QString transferCoinVar = ReadStringField(wxmbTemplateObject, {QStringLiteral("TransferCoinVar"), QStringLiteral("transferCoinVar")});
    const QString resourceCode = ReadStringField(scanObject, {QStringLiteral("ResourceCode"), QStringLiteral("resourceCode")});
    const bool isForce = ReadBoolField(wxmbTemplateObject, {QStringLiteral("IsForce"), QStringLiteral("isForce")});

    const QString loginCheckPath = QDir(wxmbPath).filePath(QStringLiteral("登录检测.txt"));
    QString loginCheckContent = ReadTemplateText(templateObject, QStringLiteral("密保登陆.txt"));
    if (!loginCheckContent.isEmpty()) {
        loginCheckContent.replace(QStringLiteral("%isbind%"), transferFlagVar);
        loginCheckContent.replace(QStringLiteral("%WilID%"), resourceCode);
        loginCheckContent.replace(QStringLiteral("%CheckMapID%"), mbMapId);
        loginCheckContent.replace(QStringLiteral("%mapid%"), finishMapId);
        loginCheckContent.replace(QStringLiteral("%State%"), isForce ? QStringLiteral("1") : QStringLiteral("0"));
        loginCheckContent.replace(QStringLiteral("%path%"), QStringLiteral("..\\..\\..\\..\\平台验证"));
        loginCheckContent.replace(QStringLiteral("%machine%"), machineVar);
        loginCheckContent.replace(QStringLiteral("%wechatid%"), openidVar);
        loginCheckContent.replace(QStringLiteral("%wechatna%"), wxVar);
        loginCheckContent.replace(QStringLiteral("%AreaID%"), partitionId);
        loginCheckContent.replace(QStringLiteral("%switchval%"), transferRechargeVar);
        loginCheckContent.replace(QStringLiteral("%usedval%"), transferUsedVar);
        loginCheckContent.replace(QStringLiteral("%cur_name%"), transferCoinName);
        loginCheckContent.replace(QStringLiteral("%currency%"), transferCoinVar);

        if (!WriteTextFile(loginCheckPath, loginCheckContent, errorMessage)) {
            return false;
        }
    } else if (!WriteTextFileIfEmpty(loginCheckPath,
                                     BuildQuestCallScript(QStringLiteral("@检测密保"),
                                                          QStringLiteral("微信密保登录检测"),
                                                          {
                                                              QStringLiteral("分区ID：%1").arg(partitionId.isEmpty() ? QStringLiteral("<empty>") : partitionId),
                                                              QStringLiteral("机器码变量：%1").arg(machineVar.isEmpty() ? QStringLiteral("<empty>") : machineVar),
                                                              QStringLiteral("OpenId变量：%1").arg(openidVar.isEmpty() ? QStringLiteral("<empty>") : openidVar),
                                                              QStringLiteral("微信变量：%1").arg(wxVar.isEmpty() ? QStringLiteral("<empty>") : wxVar),
                                                              QStringLiteral("认证地图：%1").arg(mbMapId.isEmpty() ? QStringLiteral("<empty>") : mbMapId),
                                                              QStringLiteral("完成地图：%1").arg(finishMapId.isEmpty() ? QStringLiteral("<empty>") : finishMapId),
                                                              QStringLiteral("绑定标识：%1").arg(transferFlagVar.isEmpty() ? QStringLiteral("<empty>") : transferFlagVar)
                                                          }),
                                     errorMessage)) {
        return false;
    }

    if (!WriteTextFileIfEmpty(QDir(wxmbPath).filePath(QStringLiteral("提交认证.txt")),
                              BuildWxMbSubmitScript(partitionId,
                                                    payDir,
                                                    QStringLiteral("%1_提交认证.txt").arg(partitionId.isEmpty() ? QStringLiteral("<empty>") : partitionId),
                                                    machineVar,
                                                    openidVar,
                                                    wxVar),
                              errorMessage)) {
        return false;
    }

    return EnsureLoginCheckHook(envirPath, payDir, errorMessage);
}

void RegisterScanMonitorPath(const QString &monitorFilePath)
{
    AppConfigValues values = AppConfig::Load();
    QStringList paths = values.yxsmDir.split(QLatin1Char('|'), Qt::SkipEmptyParts);
    const QString absolutePath = QFileInfo(monitorFilePath).absoluteFilePath();
    QString normalizedPath;
    if (absolutePath.size() >= 3 && absolutePath.at(1) == QLatin1Char(':')
        && (absolutePath.at(2) == QLatin1Char('/') || absolutePath.at(2) == QLatin1Char('\\'))) {
        normalizedPath = QDir::toNativeSeparators(absolutePath.left(3)).trimmed();
    } else {
        normalizedPath = QDir::toNativeSeparators(QDir(absolutePath).rootPath()).trimmed();
    }
    bool exists = false;
    for (const auto &path : paths) {
        if (QDir::toNativeSeparators(path).trimmed().compare(normalizedPath, Qt::CaseInsensitive) == 0) {
            exists = true;
            break;
        }
    }

    if (!exists) {
        paths.append(normalizedPath);
        values.yxsmDir = paths.join(QLatin1Char('|'));
        AppConfig::Save(values);
        FileMonitorService::Instance().Initialize(values);
    }
}
}

bool InstallScriptProcessor::Process(const QJsonObject &dataObject, QString *errorMessage)
{
    const QJsonObject partitionObject = ReadObjectField(dataObject, {QStringLiteral("Partition"), QStringLiteral("partition")});
    const QJsonObject templateObject = ReadObjectField(partitionObject, {QStringLiteral("Template"), QStringLiteral("template")});
    const InstallContext installContext = ResolveInstallContext(dataObject, partitionObject, templateObject);
    const QJsonArray circuits = ReadArrayField(dataObject, {QStringLiteral("Circuits"), QStringLiteral("circuits")});
    const QJsonArray products = ReadArrayField(dataObject, {QStringLiteral("Products"), QStringLiteral("products")});
    const QJsonObject scanObject = ReadObjectField(dataObject, {QStringLiteral("scan"), QStringLiteral("Scan")});
    const QJsonObject wxmbTemplateObject = ReadObjectField(dataObject, {QStringLiteral("wxmbTemplate"), QStringLiteral("WxmbTemplate")});

    const QString scriptPath = ReadStringField(partitionObject, {QStringLiteral("ScriptPath"), QStringLiteral("scriptPath")});
    const QString partitionId = ReadStringField(partitionObject, {QStringLiteral("Id"), QStringLiteral("id")});
    const QString payDir = ReadStringField(templateObject, {QStringLiteral("PayDir"), QStringLiteral("payDir")});
    const int isScan = ReadIntField(templateObject, {QStringLiteral("IsScan"), QStringLiteral("isScan")});
    const int isWxmb = ReadIntField(templateObject, {QStringLiteral("IsWxmb"), QStringLiteral("isWxmb")});

    if (scriptPath.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("InstallScript 缺少 ScriptPath");
        }
        return false;
    }

    const QString rootPath = QDir(scriptPath).rootPath();
    const QString basePath = QDir(rootPath).filePath(QStringLiteral("平台验证"));
    const QString qrPath = QDir(basePath).filePath(QStringLiteral("充值二维码"));
    const QString wxmbPath = QDir(basePath).filePath(QStringLiteral("微信密保"));
    const QString transferPath = QDir(basePath).filePath(QStringLiteral("微信转区"));

    if ((isScan != 0 || isWxmb != 0) && !EnsureDirectoryExists(basePath, errorMessage)) {
        return false;
    }

    if (isScan != 0 || isWxmb != 0) {
        const QString submitFilePath = QDir(QDir(qrPath).filePath(QStringLiteral("充值提交"))).filePath(QStringLiteral("%1_提交数据.txt").arg(partitionId));
        const QStringList directories = {
            qrPath,
            QDir(qrPath).filePath(QStringLiteral("充值结果")),
            QDir(qrPath).filePath(QStringLiteral("充值记录")),
            QDir(qrPath).filePath(QStringLiteral("充值提交")),
            QDir(qrPath).filePath(QStringLiteral("充值图片")),
            QDir(wxmbPath).filePath(QStringLiteral("认证数据")),
            QDir(wxmbPath).filePath(QStringLiteral("提交数据")),
            QDir(transferPath).filePath(QStringLiteral("提交转区数据")),
            QDir(transferPath).filePath(QStringLiteral("转区点查询")),
            QDir(transferPath).filePath(QStringLiteral("转区点扣除"))
        };

        for (const auto &directory : directories) {
            if (!EnsureDirectoryExists(directory, errorMessage)) {
                return false;
            }
        }

        const QStringList files = {
            submitFilePath,
            QDir(QDir(wxmbPath).filePath(QStringLiteral("提交数据"))).filePath(QStringLiteral("%1_提交认证.txt").arg(partitionId)),
            QDir(wxmbPath).filePath(QStringLiteral("本服公众号.txt")),
            QDir(wxmbPath).filePath(QStringLiteral("主动解绑.txt")),
            QDir(QDir(transferPath).filePath(QStringLiteral("提交转区数据"))).filePath(QStringLiteral("%1_查询.txt").arg(partitionId)),
            QDir(QDir(transferPath).filePath(QStringLiteral("提交转区数据"))).filePath(QStringLiteral("%1_扣除.txt").arg(partitionId)),
            QDir(QDir(transferPath).filePath(QStringLiteral("提交转区数据"))).filePath(QStringLiteral("%1_申请.txt").arg(partitionId)),
            QDir(transferPath).filePath(QStringLiteral("本服公众号.txt")),
            QDir(transferPath).filePath(QStringLiteral("申请转区失败.txt"))
        };

        for (const auto &filePath : files) {
            if (!EnsureEmptyFileExists(filePath, errorMessage)) {
                return false;
            }
        }

        RegisterScanMonitorPath(submitFilePath);
    }

    const QString envirPath = QDir(scriptPath).filePath(QStringLiteral("Mir200/Envir"));
    if (!EnsureDirectoryExists(envirPath, errorMessage)) {
        return false;
    }
    if (!EnsureDirectoryExists(QDir(envirPath).filePath(QStringLiteral("Market_Def")), errorMessage)) {
        return false;
    }
    if (!EnsureEmptyFileExists(QDir(envirPath).filePath(QStringLiteral("Merchant.txt")), errorMessage)) {
        return false;
    }
    if (!UpdateMerchantInfo(envirPath, templateObject, wxmbTemplateObject, errorMessage)) {
        return false;
    }
    if (!UpdateMarketDefScripts(envirPath, partitionObject, templateObject, installContext, scanObject, circuits, products, wxmbTemplateObject, errorMessage)) {
        return false;
    }

    if (!payDir.isEmpty()) {
        const QString rewardScriptRoot = ResolveTongQuScriptRoot(scriptPath, installContext);
        const QString rewardEnvirPath = QDir(rewardScriptRoot).filePath(QStringLiteral("Mir200/Envir"));
        const QString questDiaryPath = QDir(rewardEnvirPath).filePath(QStringLiteral("QuestDiary/%1").arg(payDir));
        if (!EnsureDirectoryExists(questDiaryPath, errorMessage)) {
            return false;
        }

        if (!CreateRechargeScriptSkeleton(rewardEnvirPath, questDiaryPath, templateObject, installContext, circuits, wxmbTemplateObject, partitionObject, errorMessage)) {
            return false;
        }

        if (isWxmb != 0) {
            if (!CreateWxMbScriptSkeleton(envirPath, questDiaryPath, partitionObject, templateObject, wxmbTemplateObject, scanObject, errorMessage)) {
                return false;
            }
        }
    }

    return true;
}
