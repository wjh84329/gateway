#include "partitiontemplatepage.h"
#include "ui_partitiontemplatepage.h"

#include "applogger.h"
#include "gatewayapiclient.h"
#include "pageutils.h"
#include "templatedialog.h"

#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
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
}

PartitionTemplatePage::PartitionTemplatePage(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::PartitionTemplatePage)
{
    ui->setupUi(this);

    ui->addTemplateButton->setProperty("partitionAction", true);
    ui->addTemplateButton->setFixedSize(100, 30);
    ui->editTemplateButton->setProperty("partitionAction", true);
    ui->editTemplateButton->setFixedSize(100, 30);
    ui->deleteTemplateButton->setProperty("partitionAction", true);
    ui->deleteTemplateButton->setFixedSize(100, 30);
    ui->refreshTemplateButton->setProperty("partitionAction", true);
    ui->refreshTemplateButton->setFixedSize(100, 30);
    connect(ui->refreshTemplateButton, &QPushButton::clicked, this, &PartitionTemplatePage::LoadTemplates);
    connect(ui->addTemplateButton, &QPushButton::clicked, this, &PartitionTemplatePage::OnAddTemplate);
    connect(ui->editTemplateButton, &QPushButton::clicked, this, &PartitionTemplatePage::OnEditTemplate);
    connect(ui->deleteTemplateButton, &QPushButton::clicked, this, &PartitionTemplatePage::OnDeleteTemplate);

    ui->templateTable->setObjectName(QStringLiteral("partitionTemplateTable"));
    ui->templateTable->setColumnCount(4);
    ui->templateTable->setHorizontalHeaderLabels({
        QStringLiteral("编号"),
        QStringLiteral("模板名称"),
        QStringLiteral("游戏币"),
        QStringLiteral("比例")
    });
    ui->templateTable->setRowCount(0);

    UiHelpers::ConfigureReadonlyTable(ui->templateTable);
    {
        QHeaderView *h = ui->templateTable->horizontalHeader();
        h->setMinimumSectionSize(48);
        h->setStretchLastSection(false);
        // 勿使用 setSectionResizeMode(Stretch) 作用于全部列，否则在全局 QSS 下易出现列宽塌缩、绘制异常
        h->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        h->setSectionResizeMode(1, QHeaderView::Stretch);
        h->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        h->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    }
    ui->templateTable->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    UiHelpers::CenterTableItems(ui->templateTable);
    ui->templateTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->templateTable->setSelectionMode(QAbstractItemView::SingleSelection);

    connect(ui->templateTable, &QTableWidget::cellDoubleClicked, this, &PartitionTemplatePage::OnEditTemplate);
    connect(ui->templateTable, &QTableWidget::itemSelectionChanged, this, &PartitionTemplatePage::UpdateTemplateActionButtons);
    ui->templateTable->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->templateTable, &QWidget::customContextMenuRequested, this, &PartitionTemplatePage::OnTemplateContextMenu);
    UpdateTemplateActionButtons();

    LoadTemplates();
}

PartitionTemplatePage::~PartitionTemplatePage()
{
    delete ui;
}

void PartitionTemplatePage::UpdateTemplateActionButtons()
{
    const bool hasRow = ui->templateTable->currentRow() >= 0;
    ui->editTemplateButton->setEnabled(hasRow);
    ui->deleteTemplateButton->setEnabled(hasRow);
}

int PartitionTemplatePage::SelectedTemplateId() const
{
    const int row = ui->templateTable->currentRow();
    if (row < 0)
        return 0;
    if (auto *item = ui->templateTable->item(row, 0)) {
        const QVariant v = item->data(Qt::UserRole);
        if (v.isValid())
            return v.toInt();
    }
    return 0;
}

void PartitionTemplatePage::LoadTemplates()
{
    ui->refreshTemplateButton->setEnabled(false);
    UiHelpers::SetPageLoading(this, true);

    GatewayApiClient client;
    QString errorMessage;
    m_templatesCache = client.GetTemplates(&errorMessage);
    ui->templateTable->setRowCount(0);

    if (m_templatesCache.isEmpty()) {
        if (!errorMessage.isEmpty()) {
            AppLogger::WriteLog(QStringLiteral("加载模板列表失败：%1").arg(errorMessage));
            emit statusMessageRequested(QStringLiteral("加载模板列表失败"), 3000);
        }
        ui->refreshTemplateButton->setEnabled(true);
        UpdateTemplateActionButtons();
        UiHelpers::SetPageLoading(this, false);
        return;
    }

    ui->templateTable->setRowCount(m_templatesCache.size());
    for (int row = 0; row < m_templatesCache.size(); ++row) {
        const QJsonObject templateObject = m_templatesCache.at(row).toObject();
        const QString idStr = ReadStringField(templateObject, {QStringLiteral("Id"), QStringLiteral("id")});
        auto *idItem = new QTableWidgetItem(idStr);
        idItem->setData(Qt::UserRole, idStr.toInt());
        ui->templateTable->setItem(row, 0, idItem);
        ui->templateTable->setItem(row, 1, new QTableWidgetItem(ReadStringField(templateObject, {QStringLiteral("Name"), QStringLiteral("name")})));
        ui->templateTable->setItem(row, 2, new QTableWidgetItem(ReadStringField(templateObject, {QStringLiteral("CurrencyName"), QStringLiteral("currencyName")})));
        ui->templateTable->setItem(row, 3, new QTableWidgetItem(ReadStringField(templateObject, {QStringLiteral("Ratio"), QStringLiteral("ratio")})));
    }

    UiHelpers::CenterTableItems(ui->templateTable);
    // 勿用 resizeColumnsToContents()：会把「模板名称」列（Stretch）也收成内容宽，刷新后整表挤在左侧留白。
    ui->templateTable->resizeColumnToContents(0);
    ui->templateTable->resizeColumnToContents(2);
    ui->templateTable->resizeColumnToContents(3);
    emit statusMessageRequested(QStringLiteral("模板列表已刷新"), 3000);
    ui->refreshTemplateButton->setEnabled(true);
    UpdateTemplateActionButtons();
    UiHelpers::SetPageLoading(this, false);
}

void PartitionTemplatePage::OnAddTemplate()
{
    TemplateDialog dialog(0, QJsonObject(), this);
    UiHelpers::CenterDialogOnWindow(&dialog, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    LoadTemplates();
}

void PartitionTemplatePage::OnEditTemplate()
{
    const int tid = SelectedTemplateId();
    if (tid <= 0) {
        UiHelpers::ShowStyledMessageBox(this, QMessageBox::Warning, QStringLiteral("提示"), QStringLiteral("请先选择一个模板"));
        return;
    }

    QString err;
    UiHelpers::SetPageLoading(this, true, QStringLiteral("加载中..."), QStringLiteral("正在加载模板详情"));
    const QJsonObject full = GatewayApiClient().GetClientSignalTemplate(tid, &err);
    UiHelpers::SetPageLoading(this, false);

    if (full.isEmpty()) {
        UiHelpers::ShowStyledMessageBox(this, QMessageBox::Warning, QStringLiteral("加载失败"),
                                        err.isEmpty() ? QStringLiteral("无法获取模板详情（接口无数据或解析失败）") : err);
        return;
    }

    TemplateDialog dialog(tid, full, this);
    UiHelpers::CenterDialogOnWindow(&dialog, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    LoadTemplates();
}

void PartitionTemplatePage::OnDeleteTemplate()
{
    const int tid = SelectedTemplateId();
    if (tid <= 0) {
        UiHelpers::ShowStyledMessageBox(this, QMessageBox::Warning, QStringLiteral("提示"), QStringLiteral("请先选择一个模板"));
        return;
    }
    const int row = ui->templateTable->currentRow();
    const QString name = (row >= 0 && ui->templateTable->item(row, 1))
                             ? ui->templateTable->item(row, 1)->text()
                             : QString();

    const QMessageBox::StandardButton reply = QMessageBox::question(
        this,
        QStringLiteral("确认删除"),
        QStringLiteral("确定要删除模板 \"%1\"（编号 %2）吗？\n若仍有分区引用该模板，服务端可能拒绝删除。")
            .arg(name.isEmpty() ? QStringLiteral("(未命名)") : name)
            .arg(tid),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (reply != QMessageBox::Yes)
        return;

    QString err;
    UiHelpers::SetPageLoading(this, true, QStringLiteral("删除中..."), QStringLiteral("正在删除模板"));
    const bool ok = GatewayApiClient().DeleteClientTemplate(tid, &err);
    UiHelpers::SetPageLoading(this, false);

    if (!ok) {
        UiHelpers::ShowStyledMessageBox(this, QMessageBox::Warning, QStringLiteral("删除失败"),
                                        err.isEmpty() ? QStringLiteral("删除模板失败") : err);
        return;
    }
    UiHelpers::ShowOverlayMessage(this, QMessageBox::Information, QStringLiteral("模板已删除"));
    emit statusMessageRequested(QStringLiteral("模板已删除"), 3000);
    LoadTemplates();
}

void PartitionTemplatePage::OnTemplateContextMenu(const QPoint &pos)
{
    const int row = ui->templateTable->rowAt(pos.y());
    QMenu menu(this);
    menu.setStyleSheet(QStringLiteral(
        "QMenu { background: #ffffff; border: 1px solid #e5d2d5; border-radius: 8px; padding: 6px; }"
        "QMenu::item { padding: 8px 24px; border-radius: 4px; color: #4a3d3f; font-size: 13px; }"
        "QMenu::item:selected { background: #f0e4e6; color: #4a3d3f; }"
        "QMenu::separator { height: 1px; background: #e5d2d5; margin: 4px 12px; }"));

    if (row < 0) {
        menu.addAction(QStringLiteral("🔄 刷新列表"), this, &PartitionTemplatePage::LoadTemplates);
        menu.addSeparator();
        menu.addAction(QStringLiteral("➕ 添加模板"), this, &PartitionTemplatePage::OnAddTemplate);
    } else {
        ui->templateTable->selectRow(row);
        menu.addAction(QStringLiteral("🔄 刷新列表"), this, &PartitionTemplatePage::LoadTemplates);
        menu.addSeparator();
        menu.addAction(QStringLiteral("✏️ 编辑模板"), this, &PartitionTemplatePage::OnEditTemplate);
        menu.addSeparator();
        menu.addAction(QStringLiteral("🗑️ 删除模板"), this, &PartitionTemplatePage::OnDeleteTemplate);
    }
    menu.exec(ui->templateTable->mapToGlobal(pos));
}
