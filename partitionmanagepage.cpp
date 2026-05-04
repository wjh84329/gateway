#include "partitionmanagepage.h"
#include "ui_partitionmanagepage.h"

#include "applogger.h"
#include "gatewayapiclient.h"
#include "pageutils.h"
#include "partitiondialog.h"

#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QTableWidgetItem>

namespace {
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

QJsonObject ReadObjectField(const QJsonObject &object, const QStringList &names)
{
    for (const auto &name : names) {
        if (object.value(name).isObject()) {
            return object.value(name).toObject();
        }
    }
    return {};
}
}

PartitionManagePage::PartitionManagePage(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::PartitionManagePage)
{
    ui->setupUi(this);

    // ----- 左侧按钮 -----
    ui->addPartitionButton->setProperty("partitionAction", true);
    ui->addPartitionButton->setFixedSize(100, 30);
    ui->refreshPartitionButton->setProperty("partitionAction", true);
    ui->refreshPartitionButton->setFixedSize(100, 30);
    connect(ui->refreshPartitionButton, &QPushButton::clicked, this, &PartitionManagePage::LoadPartitions);
    connect(ui->addPartitionButton, &QPushButton::clicked, this, &PartitionManagePage::OnAddPartition);

    // ----- 右侧操作按钮 -----
    const QString rightBtnStyle = QStringLiteral(
        "QPushButton { background: #f5edef; color: #8b4a53; border: 1px solid #dccbce; border-radius: 6px; "
        "font-size: 13px; font-weight: 600; padding: 8px 16px; min-height: 36px; }"
        "QPushButton:hover { background: #efe4e7; }"
        "QPushButton:disabled { background: #f8f3f3; color: #c0b0b0; border-color: #e5dbdb; }");

    const QString deleteBtnStyle = QStringLiteral(
        "QPushButton { color: #ffffff; background: #cf5b5b; border: none; border-radius: 6px; "
        "font-size: 13px; font-weight: 600; padding: 8px 16px; min-height: 36px; }"
        "QPushButton:hover { background: #c14848; }"
        "QPushButton:disabled { background: #e8bbbb; color: #f5e0e0; }");

    // ui->editPartitionButton->setStyleSheet(rightBtnStyle);
    // ui->loadPartitionButton->setStyleSheet(rightBtnStyle);
    // ui->deletePartitionButton->setStyleSheet(deleteBtnStyle);

    // connect(ui->editPartitionButton, &QPushButton::clicked, this, &PartitionManagePage::OnEditPartition);
    // connect(ui->loadPartitionButton, &QPushButton::clicked, this, &PartitionManagePage::OnLoadPartition);
    // connect(ui->deletePartitionButton, &QPushButton::clicked, this, &PartitionManagePage::OnDeletePartition);

    // 右侧面板标题
    // ui->rightPanelTitle->setStyleSheet(QStringLiteral(
    //     "color: #5f5053; font-size: 15px; font-weight: 700; padding-bottom: 8px;"));

    // // 右侧面板背景
    // ui->rightPanel->setStyleSheet(QStringLiteral("background: #ffffff; border-left: 1px solid #e0d0d0;"));

    // ----- 表格配置 -----
    ui->partitionTable->setColumnCount(7);
    ui->partitionTable->setHorizontalHeaderLabels({
        QStringLiteral("编号"),
        QStringLiteral("分区名称"),
        QStringLiteral("分区模板"),
        QStringLiteral("游戏币名称"),
        QStringLiteral("安装路径"),
        QStringLiteral("操作"),
        QStringLiteral("")  // 隐藏列存完整 JSON
    });
    ui->partitionTable->setColumnHidden(5, true);  // 隐藏旧操作列
    ui->partitionTable->setColumnHidden(6, true);  // 隐藏 JSON 列
    ui->partitionTable->setRowCount(0);

    // // 表格选中变化时启用/禁用右侧按钮
    // connect(ui->partitionTable, &QTableWidget::itemSelectionChanged, this, [this]() {
    //     const bool hasSelection = ui->partitionTable->currentRow() >= 0;
    //     ui->editPartitionButton->setEnabled(hasSelection);
    //     ui->loadPartitionButton->setEnabled(hasSelection);
    //     ui->deletePartitionButton->setEnabled(hasSelection);
    // });

    // 右键菜单
    ui->partitionTable->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->partitionTable, &QWidget::customContextMenuRequested, this, &PartitionManagePage::OnContextMenu);

    // 双击编辑
    connect(ui->partitionTable, &QTableWidget::cellDoubleClicked, this, &PartitionManagePage::OnEditPartition);

    // 表格视觉样式
    UiHelpers::ConfigureReadonlyTable(ui->partitionTable);
    ui->partitionTable->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->partitionTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->partitionTable->horizontalHeader()->setStretchLastSection(false);
    ui->partitionTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    ui->partitionTable->setColumnWidth(0, 65);
    ui->partitionTable->setColumnWidth(1, 140);
    ui->partitionTable->setColumnWidth(2, 100);
    ui->partitionTable->setColumnWidth(3, 100);
    ui->partitionTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    ui->partitionTable->setAlternatingRowColors(true);
    ui->partitionTable->setStyleSheet(QStringLiteral(
        "QTableWidget { background: #ffffff; border: 1px solid #e0d0d0; border-radius: 6px; "
        "gridline-color: #f0e8ea; font-size: 13px; }"
        "QTableWidget::item { padding: 4px 8px; }"
        "QHeaderView::section { background: #f5edef; color: #5f5053; font-weight: 600; border: none; padding: 8px; font-size: 13px; }"
        "QTableWidget::item:selected { background: #f0e4e6; color: #4a3d3f; }"));

    // 分割器
    ui->splitter->setStretchFactor(0, 4);
    ui->splitter->setStretchFactor(1, 1);
    ui->splitter->setSizes({450, 160});
    ui->splitter->setHandleWidth(1);

    LoadPartitions();
}

PartitionManagePage::~PartitionManagePage()
{
    delete ui;
}

void PartitionManagePage::LoadPartitions()
{
    ui->refreshPartitionButton->setEnabled(false);
    UiHelpers::SetPageLoading(this, true);

    GatewayApiClient client;
    QString errorMessage;
    m_partitions = client.GetPartitions(&errorMessage);
    ui->partitionTable->setRowCount(0);

    if (m_partitions.isEmpty()) {
        if (!errorMessage.isEmpty()) {
            AppLogger::WriteLog(QStringLiteral("加载分区列表失败：%1").arg(errorMessage));
            emit statusMessageRequested(QStringLiteral("加载分区列表失败"), 3000);
        }
        ui->refreshPartitionButton->setEnabled(true);
        UiHelpers::SetPageLoading(this, false);
        return;
    }

    ui->partitionTable->setRowCount(m_partitions.size());
    for (int row = 0; row < m_partitions.size(); ++row) {
        const QJsonObject p = m_partitions.at(row).toObject();
        const QJsonObject tpl = ReadObjectField(p, {QStringLiteral("Template"), QStringLiteral("template")});

        const QString id = ReadStringField(p, {QStringLiteral("Id"), QStringLiteral("id")});
        const QString name = ReadStringField(p, {QStringLiteral("Name"), QStringLiteral("name"), QStringLiteral("PartitionName")});
        const QString tplName = ReadStringField(p, {QStringLiteral("TemplateName"), QStringLiteral("templateName")}).isEmpty()
                                    ? ReadStringField(tpl, {QStringLiteral("Name"), QStringLiteral("name")})
                                    : ReadStringField(p, {QStringLiteral("TemplateName"), QStringLiteral("templateName")});
        const QString curName = ReadStringField(p, {QStringLiteral("CurrencyName"), QStringLiteral("currencyName")}).isEmpty()
                                    ? ReadStringField(tpl, {QStringLiteral("CurrencyName"), QStringLiteral("currencyName")})
                                    : ReadStringField(p, {QStringLiteral("CurrencyName"), QStringLiteral("currencyName")});
        const QString path = ReadStringField(p, {QStringLiteral("ScriptPath"), QStringLiteral("scriptPath"), QStringLiteral("PartitionPath"), QStringLiteral("partitionPath")});

        ui->partitionTable->setItem(row, 0, new QTableWidgetItem(id));
        ui->partitionTable->setItem(row, 1, new QTableWidgetItem(name));
        ui->partitionTable->setItem(row, 2, new QTableWidgetItem(tplName));
        ui->partitionTable->setItem(row, 3, new QTableWidgetItem(curName));
        ui->partitionTable->setItem(row, 4, new QTableWidgetItem(path));

        // 隐藏列：操作(5) 和 JSON(6)
        auto *jsonItem = new QTableWidgetItem(QString::fromUtf8(QJsonDocument(p).toJson(QJsonDocument::Compact)));
        ui->partitionTable->setItem(row, 6, jsonItem);
    }

    UiHelpers::CenterTableItems(ui->partitionTable);
    emit statusMessageRequested(QStringLiteral("分区列表已刷新"), 3000);
    ui->refreshPartitionButton->setEnabled(true);
    UiHelpers::SetPageLoading(this, false);
}

void PartitionManagePage::LoadTemplatesAndGroups()
{
    if (!m_templates.isEmpty() && !m_groups.isEmpty()) return;

    GatewayApiClient client;
    QString errorMessage;
    m_templates = client.GetTemplates(&errorMessage);
    if (m_templates.isEmpty() && !errorMessage.isEmpty()) {
        AppLogger::WriteLog(QStringLiteral("加载模板列表失败：%1").arg(errorMessage));
    }

    m_groups = client.GetGroups(&errorMessage);
    if (m_groups.isEmpty() && !errorMessage.isEmpty()) {
        AppLogger::WriteLog(QStringLiteral("加载分组列表失败：%1").arg(errorMessage));
    }
}

int PartitionManagePage::GetSelectedPartitionId() const
{
    const int row = ui->partitionTable->currentRow();
    if (row < 0) return -1;
    bool ok = false;
    const int id = ui->partitionTable->item(row, 0)->text().toInt(&ok);
    return ok ? id : -1;
}

QJsonObject PartitionManagePage::GetSelectedPartitionObject() const
{
    const int row = ui->partitionTable->currentRow();
    if (row < 0) return {};

    QTableWidgetItem *jsonItem = ui->partitionTable->item(row, 6);
    if (!jsonItem) return {};
    return QJsonDocument::fromJson(jsonItem->text().toUtf8()).object();
}

void PartitionManagePage::OnAddPartition()
{
    LoadTemplatesAndGroups();

    PartitionDialog dialog(QJsonObject(), m_templates, m_groups, this);
    if (dialog.exec() != QDialog::Accepted) return;

    GatewayApiClient client;
    QString errorMessage;
    UiHelpers::SetPageLoading(this, true,
                              QStringLiteral("添加中..."),
                              QStringLiteral("正在安装分区"));

    const QJsonObject body = dialog.GetRequestBody();
    const QString result = client.InstallClientPartition(body, &errorMessage);
    UiHelpers::SetPageLoading(this, false);

    if (!result.isEmpty()
        && !errorMessage.isEmpty()
        && result == errorMessage) {
        UiHelpers::ShowStyledMessageBox(this, QMessageBox::Warning,
                                        QStringLiteral("添加失败"), errorMessage);
        emit statusMessageRequested(QStringLiteral("添加分区失败"), 3000);
    } else if (result.contains(QStringLiteral("成功"))
               || result.contains(QStringLiteral("安装成功"))) {
        UiHelpers::ShowOverlayMessage(this, QMessageBox::Information,
                                      QStringLiteral("添加分区成功"));
        emit statusMessageRequested(QStringLiteral("分区添加成功"), 3000);
        LoadPartitions();
    } else {
        UiHelpers::ShowStyledMessageBox(this,
                                         result.contains(QStringLiteral("成功")) ? QMessageBox::Information : QMessageBox::Warning,
                                         QStringLiteral("添加分区"), result);
        emit statusMessageRequested(QStringLiteral("添加分区：%1").arg(result), 3000);
        if (result.contains(QStringLiteral("成功")) || result.contains(QStringLiteral("安装成功"))) {
            LoadPartitions();
        }
    }
}

void PartitionManagePage::OnEditPartition()
{
    const QJsonObject partitionObj = GetSelectedPartitionObject();
    if (partitionObj.isEmpty()) {
        UiHelpers::ShowStyledMessageBox(this, QMessageBox::Warning,
                                        QStringLiteral("提示"), QStringLiteral("请先选择一个分区"));
        return;
    }

    LoadTemplatesAndGroups();

    PartitionDialog dialog(partitionObj, m_templates, m_groups, this);
    if (dialog.exec() != QDialog::Accepted) return;

    GatewayApiClient client;
    QString errorMessage;
    UiHelpers::SetPageLoading(this, true,
                              QStringLiteral("保存中..."),
                              QStringLiteral("正在更新分区"));

    const QJsonObject body = dialog.GetRequestBody();
    const bool success = client.UpdateClientPartition(body, &errorMessage);
    UiHelpers::SetPageLoading(this, false);

    if (success) {
        UiHelpers::ShowOverlayMessage(this, QMessageBox::Information, QStringLiteral("更新分区成功"));
        emit statusMessageRequested(QStringLiteral("分区已更新"), 3000);
        LoadPartitions();
    } else {
        const QString msg = errorMessage.isEmpty() ? QStringLiteral("更新分区失败") : errorMessage;
        UiHelpers::ShowStyledMessageBox(this, QMessageBox::Warning,
                                        QStringLiteral("更新失败"), msg);
        emit statusMessageRequested(QStringLiteral("更新分区失败"), 3000);
    }
}

void PartitionManagePage::OnDeletePartition()
{
    const int partitionId = GetSelectedPartitionId();
    if (partitionId < 0) {
        UiHelpers::ShowStyledMessageBox(this, QMessageBox::Warning,
                                        QStringLiteral("提示"), QStringLiteral("请先选择一个分区"));
        return;
    }

    const QString partitionName = ui->partitionTable->item(ui->partitionTable->currentRow(), 1)->text();

    QMessageBox::StandardButton reply = QMessageBox::question(
        this,
        QStringLiteral("确认删除"),
        QStringLiteral("确定要移除分区 \"%1\"（ID: %2）吗？\n此操作不可撤销。")
            .arg(partitionName).arg(partitionId),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);

    if (reply != QMessageBox::Yes) return;

    GatewayApiClient client;
    QString errorMessage;
    UiHelpers::SetPageLoading(this, true,
                              QStringLiteral("删除中..."),
                              QStringLiteral("正在删除分区"));

    const bool success = client.DeletePartition(partitionId, &errorMessage);
    UiHelpers::SetPageLoading(this, false);

    if (success) {
        UiHelpers::ShowOverlayMessage(this, QMessageBox::Information, QStringLiteral("分区已删除"));
        emit statusMessageRequested(QStringLiteral("分区已删除"), 3000);
        LoadPartitions();
    } else {
        const QString msg = errorMessage.isEmpty() ? QStringLiteral("删除分区失败") : errorMessage;
        UiHelpers::ShowStyledMessageBox(this, QMessageBox::Critical,
                                        QStringLiteral("删除失败"), msg);
        emit statusMessageRequested(QStringLiteral("删除分区失败"), 3000);
    }
}

void PartitionManagePage::OnLoadPartition()
{
    const int partitionId = GetSelectedPartitionId();
    if (partitionId < 0) {
        UiHelpers::ShowStyledMessageBox(this, QMessageBox::Warning,
                                        QStringLiteral("提示"), QStringLiteral("请先选择一个分区"));
        return;
    }

    QJsonObject model;
    model.insert(QStringLiteral("Id"), partitionId);
    model.insert(QStringLiteral("PartitionCmdType"), 2);

    GatewayApiClient client;
    QString errorMessage;
    UiHelpers::SetPageLoading(this, true,
                              QStringLiteral("加载中..."),
                              QStringLiteral("正在发送加载分区指令"));

    const bool success = client.LoadClientPartition(model, &errorMessage);
    UiHelpers::SetPageLoading(this, false);

    if (success) {
        UiHelpers::ShowOverlayMessage(this, QMessageBox::Information, QStringLiteral("加载分区指令已发送"));
        emit statusMessageRequested(QStringLiteral("加载分区指令发送成功"), 3000);
    } else {
        const QString msg = errorMessage.isEmpty() ? QStringLiteral("加载分区失败") : errorMessage;
        UiHelpers::ShowStyledMessageBox(this, QMessageBox::Warning,
                                        QStringLiteral("加载失败"), msg);
        emit statusMessageRequested(QStringLiteral("加载分区失败"), 3000);
    }
}

void PartitionManagePage::OnContextMenu(const QPoint &pos)
{
    const int row = ui->partitionTable->rowAt(pos.y());
    QMenu menu(this);
    menu.setStyleSheet(QStringLiteral(
        "QMenu { background: #ffffff; border: 1px solid #e5d2d5; border-radius: 8px; padding: 6px; }"
        "QMenu::item { padding: 8px 24px; border-radius: 4px; color: #4a3d3f; font-size: 13px; }"
        "QMenu::item:selected { background: #f0e4e6; color: #4a3d3f; }"
        "QMenu::separator { height: 1px; background: #e5d2d5; margin: 4px 12px; }"));

    if (row < 0) {
        menu.addAction(QStringLiteral("🔄 刷新分区"), this, &PartitionManagePage::LoadPartitions);
        menu.addSeparator();
        menu.addAction(QStringLiteral("➕ 添加分区"), this, &PartitionManagePage::OnAddPartition);
    } else {
        ui->partitionTable->selectRow(row);
        menu.addAction(QStringLiteral("🔄 刷新分区"), this, &PartitionManagePage::LoadPartitions);
        menu.addSeparator();
        menu.addAction(QStringLiteral("✏️ 编辑分区"), this, &PartitionManagePage::OnEditPartition);
        menu.addAction(QStringLiteral("📦 加载分区"), this, &PartitionManagePage::OnLoadPartition);
        menu.addSeparator();
        menu.addAction(QStringLiteral("🗑️ 删除分区"), this, &PartitionManagePage::OnDeletePartition);
    }

    menu.exec(ui->partitionTable->mapToGlobal(pos));
}
