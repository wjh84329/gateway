#include "scriptpage.h"
#include "ui_scriptpage.h"

#include "gatewayapiclient.h"

#include <QComboBox>
#include <QFrame>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QShowEvent>

namespace {

QJsonObject ReadObjectField(const QJsonObject &object, const QStringList &names)
{
    for (const auto &name : names) {
        if (object.value(name).isObject()) {
            return object.value(name).toObject();
        }
    }
    return {};
}

QString ReadStringField(const QJsonObject &object, const QStringList &names)
{
    for (const auto &name : names) {
        const QString value = object.value(name).toVariant().toString().trimmed();
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
            const int parsed = value.toString().toInt(&ok);
            if (ok) {
                return parsed;
            }
        }
    }
    return defaultValue;
}

bool ReadBoolLoose(const QJsonObject &object, const QStringList &names, bool defaultValue = false)
{
    for (const auto &name : names) {
        const QJsonValue value = object.value(name);
        if (value.isBool()) {
            return value.toBool();
        }
        if (value.isDouble()) {
            return value.toInt() != 0;
        }
        if (value.isString()) {
            const QString t = value.toString().trimmed().toLower();
            if (t == QLatin1String("true") || t == QLatin1String("1")) {
                return true;
            }
            if (t == QLatin1String("false") || t == QLatin1String("0")) {
                return false;
            }
        }
    }
    return defaultValue;
}

QString RechargeTemplateFileName(bool isTongQu)
{
    return isTongQu ? QStringLiteral("通区测试充值.txt") : QStringLiteral("充值.txt");
}

QString FileNameForScriptMenuRow(int row, bool isTongQu)
{
    switch (row) {
    case 0:
        return QStringLiteral("NPC.txt");
    case 1:
        return RechargeTemplateFileName(isTongQu);
    case 2:
        return QStringLiteral("附加赠送.txt");
    case 3:
        return QStringLiteral("积分.txt");
    case 4:
        return QStringLiteral("装备.txt");
    default:
        return {};
    }
}

} // namespace

ScriptPage::ScriptPage(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::ScriptPage)
{
    ui->setupUi(this);

    ui->scriptMenuList->setFixedWidth(100);
    ui->scriptMenuList->addItems({
        QStringLiteral("NPC"),
        QStringLiteral("充值"),
        QStringLiteral("附加赠送"),
        QStringLiteral("积分"),
        QStringLiteral("装备"),
    });
    for (int i = 0; i < ui->scriptMenuList->count(); ++i) {
        ui->scriptMenuList->item(i)->setTextAlignment(Qt::AlignCenter);
    }
    ui->scriptMenuList->setCurrentRow(0);
    ui->scriptMenuList->setFocusPolicy(Qt::NoFocus);

    ui->partitionComboBox->setFixedHeight(28);
    ui->scriptEditor->setLineWrapMode(QPlainTextEdit::NoWrap);
    ui->scriptEditor->setFrameShape(QFrame::NoFrame);
    ui->saveScriptButton->setProperty("scriptSave", true);
    ui->saveScriptButton->setFixedSize(150, 32);

    connect(ui->partitionComboBox, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        Q_UNUSED(index);
        LoadMergedScriptsForCurrentPartition();
    });
    connect(ui->scriptMenuList, &QListWidget::currentRowChanged, this, [this](int row) {
        Q_UNUSED(row);
        ApplyEditorFromCurrentSelection();
    });
    connect(ui->saveScriptButton, &QPushButton::clicked, this, [this]() {
        const int pi = ui->partitionComboBox->currentIndex();
        if (pi < 0 || pi >= m_partitionContexts.size()) {
            QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("请先选择分区。"));
            return;
        }
        const PartitionScriptContext ctx = m_partitionContexts.at(pi);
        if (ctx.templateId <= 0 || ctx.gameEngine.trimmed().isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("当前分区缺少模板编号或游戏引擎，无法保存。"));
            return;
        }
        const QString fileName = CurrentMenuFileName();
        if (fileName.isEmpty()) {
            return;
        }
        GatewayApiClient client;
        QString err;
        if (!client.SaveClientInstallScriptFile(ctx.templateId, ctx.gameEngine, fileName, ui->scriptEditor->toPlainText(), &err)) {
            QMessageBox::warning(this, QStringLiteral("保存失败"), err.isEmpty() ? QStringLiteral("未知错误") : err);
            return;
        }
        QMessageBox::information(this, QStringLiteral("成功"), QStringLiteral("已保存到平台（商户覆盖脚本）。"));
        LoadMergedScriptsForCurrentPartition();
    });
}

ScriptPage::~ScriptPage()
{
    delete ui;
}

void ScriptPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    RefreshPartitionCombo();
}

void ScriptPage::RefreshPartitionCombo()
{
    ui->partitionComboBox->blockSignals(true);
    ui->partitionComboBox->clear();
    m_partitionContexts.clear();

    GatewayApiClient client;
    QString err;
    const QJsonArray partitions = client.GetPartitions(&err);
    if (partitions.isEmpty()) {
        ui->partitionComboBox->addItem(err.isEmpty() ? QStringLiteral("（暂无分区）") : QStringLiteral("（加载失败）"));
        ui->partitionComboBox->blockSignals(false);
        ui->scriptEditor->setPlainText(err.isEmpty()
                                           ? QStringLiteral("; 请先在分区管理中添加分区。")
                                           : QStringLiteral("; 加载分区列表失败：%1").arg(err));
        m_mergedFiles.clear();
        return;
    }

    for (const QJsonValue &v : partitions) {
        if (!v.isObject()) {
            continue;
        }
        const QJsonObject p = v.toObject();
        const QJsonObject tpl = ReadObjectField(p, {QStringLiteral("Template"), QStringLiteral("template")});

        int templateId = ReadIntField(p, {QStringLiteral("TemplateId"), QStringLiteral("templateId")}, 0);
        if (templateId <= 0) {
            templateId = ReadIntField(tpl, {QStringLiteral("Id"), QStringLiteral("id")}, 0);
        }
        QString gameEngine = ReadStringField(p, {QStringLiteral("GameEngine"), QStringLiteral("gameEngine")});
        if (gameEngine.isEmpty()) {
            gameEngine = ReadStringField(tpl, {QStringLiteral("GameEngine"), QStringLiteral("gameEngine")});
        }
        bool tongQu = ReadBoolLoose(p, {QStringLiteral("IsTongQu"), QStringLiteral("isTongQu")});
        if (!tongQu && !tpl.isEmpty()) {
            tongQu = ReadBoolLoose(tpl, {QStringLiteral("IsTongQu"), QStringLiteral("isTongQu")});
        }

        const QString name = ReadStringField(p, {QStringLiteral("Name"), QStringLiteral("name"), QStringLiteral("PartitionName")});
        const int partitionId = ReadIntField(p, {QStringLiteral("Id"), QStringLiteral("id")}, 0);
        if (templateId <= 0 || gameEngine.isEmpty()) {
            continue;
        }

        const QString label = QStringLiteral("%1  |  %2%3")
                                  .arg(name.isEmpty() ? QStringLiteral("未命名") : name,
                                       gameEngine,
                                       tongQu ? QStringLiteral("  · 通区") : QString());

        m_partitionContexts.append(PartitionScriptContext{templateId, partitionId, gameEngine, tongQu});
        ui->partitionComboBox->addItem(label);
    }

    if (m_partitionContexts.isEmpty()) {
        ui->partitionComboBox->addItem(QStringLiteral("（无可用分区：缺少模板或引擎）"));
    }

    ui->partitionComboBox->blockSignals(false);

    if (!m_partitionContexts.isEmpty()) {
        LoadMergedScriptsForCurrentPartition();
    } else {
        m_mergedFiles.clear();
        ui->scriptEditor->setPlainText(QStringLiteral("; 当前没有带「游戏引擎」的分区，请先在分区管理中绑定模板。"));
    }
}

void ScriptPage::LoadMergedScriptsForCurrentPartition()
{
    const int pi = ui->partitionComboBox->currentIndex();
    if (pi < 0 || pi >= m_partitionContexts.size()) {
        m_mergedFiles.clear();
        return;
    }
    const PartitionScriptContext ctx = m_partitionContexts.at(pi);

    m_loadingScripts = true;
    m_mergedFiles.clear();

    GatewayApiClient client;
    QString err;
    const QJsonDocument doc =
        client.GetClientInstallScriptFiles(ctx.templateId, ctx.gameEngine, &err, ctx.partitionId);
    m_loadingScripts = false;

    QString parseErr;
    if (!GatewayApiClient::ParseGetClientInstallScriptFilesResponse(doc, &m_mergedFiles, nullptr, &parseErr)) {
        ui->scriptEditor->setPlainText(
            QStringLiteral("; 拉取脚本失败：%1").arg(!err.isEmpty() ? err : parseErr));
        return;
    }

    ApplyEditorFromCurrentSelection();
}

QString ScriptPage::CurrentMenuFileName() const
{
    const int pi = ui->partitionComboBox->currentIndex();
    if (pi < 0 || pi >= m_partitionContexts.size()) {
        return {};
    }
    const bool tongQu = m_partitionContexts.at(pi).isTongQu;
    return FileNameForScriptMenuRow(ui->scriptMenuList->currentRow(), tongQu);
}

void ScriptPage::ApplyEditorFromCurrentSelection()
{
    if (m_loadingScripts || m_partitionContexts.isEmpty()) {
        return;
    }
    const QString fileName = CurrentMenuFileName();
    if (fileName.isEmpty()) {
        return;
    }
    bool found = false;
    QString text;
    for (auto it = m_mergedFiles.constBegin(); it != m_mergedFiles.constEnd(); ++it) {
        if (it.key().compare(fileName, Qt::CaseInsensitive) == 0) {
            text = it.value();
            found = true;
            break;
        }
    }
    if (found) {
        ui->scriptEditor->setPlainText(text);
        return;
    }
    if (!m_mergedFiles.isEmpty()) {
        ui->scriptEditor->setPlainText(
            QStringLiteral("; 合并结果中未找到文件「%1」，可能平台未配置该引擎默认脚本。").arg(fileName));
    }
}
