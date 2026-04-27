#include "partitionmanagepage.h"
#include "ui_partitionmanagepage.h"

#include "applogger.h"
#include "gatewayapiclient.h"
#include "pageutils.h"

#include <QHeaderView>
#include <QJsonArray>
#include <QJsonObject>
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

    ui->addPartitionButton->setProperty("partitionAction", true);
    ui->addPartitionButton->setFixedSize(100, 30);
    ui->refreshPartitionButton->setProperty("partitionAction", true);
    ui->refreshPartitionButton->setFixedSize(100, 30);
    connect(ui->refreshPartitionButton, &QPushButton::clicked, this, &PartitionManagePage::LoadPartitions);

    ui->partitionTable->setColumnCount(5);
    ui->partitionTable->setHorizontalHeaderLabels({
        QStringLiteral("编号"),
        QStringLiteral("分区名称"),
		QStringLiteral("分区模板"),
        QStringLiteral("游戏币名称"),
        QStringLiteral("安装路径")
    });
    ui->partitionTable->setRowCount(0);

    UiHelpers::ConfigureReadonlyTable(ui->partitionTable);
    ui->partitionTable->horizontalHeader()->setStretchLastSection(true);
    ui->partitionTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    ui->partitionTable->setColumnWidth(0, 95);
    ui->partitionTable->setColumnWidth(1, 150);
    ui->partitionTable->setColumnWidth(2, 100);
    ui->partitionTable->setColumnWidth(3, 120);
    UiHelpers::CenterTableItems(ui->partitionTable);

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
    const QJsonArray partitions = client.GetPartitions(&errorMessage);
    ui->partitionTable->setRowCount(0);

    if (partitions.isEmpty()) {
        if (!errorMessage.isEmpty()) {
            AppLogger::WriteLog(QStringLiteral("加载分区列表失败：%1").arg(errorMessage));
        }
        ui->refreshPartitionButton->setEnabled(true);
        UiHelpers::SetPageLoading(this, false);
        return;
    }

    ui->partitionTable->setRowCount(partitions.size());
    for (int row = 0; row < partitions.size(); ++row) {
        const QJsonObject partitionObject = partitions.at(row).toObject();
        const QJsonObject templateObject = ReadObjectField(partitionObject, {QStringLiteral("Template"), QStringLiteral("template")});
        ui->partitionTable->setItem(row, 0, new QTableWidgetItem(ReadStringField(partitionObject, {QStringLiteral("Id"), QStringLiteral("id")})));
        ui->partitionTable->setItem(row, 1, new QTableWidgetItem(ReadStringField(partitionObject, {QStringLiteral("Name"), QStringLiteral("name"), QStringLiteral("PartitionName")})));
        ui->partitionTable->setItem(row, 2, new QTableWidgetItem(ReadStringField(partitionObject, {QStringLiteral("TemplateName"), QStringLiteral("templateName")}).isEmpty()
                                                                      ? ReadStringField(templateObject, {QStringLiteral("Name"), QStringLiteral("name")})
                                                                      : ReadStringField(partitionObject, {QStringLiteral("TemplateName"), QStringLiteral("templateName")})));
        ui->partitionTable->setItem(row, 3, new QTableWidgetItem(ReadStringField(partitionObject, {QStringLiteral("CurrencyName"), QStringLiteral("currencyName")}).isEmpty()
                                                                      ? ReadStringField(templateObject, {QStringLiteral("CurrencyName"), QStringLiteral("currencyName")})
                                                                      : ReadStringField(partitionObject, {QStringLiteral("CurrencyName"), QStringLiteral("currencyName")})));
        ui->partitionTable->setItem(row, 4, new QTableWidgetItem(ReadStringField(partitionObject, {QStringLiteral("ScriptPath"), QStringLiteral("scriptPath"), QStringLiteral("PartitionPath"), QStringLiteral("partitionPath")})));
    }

    UiHelpers::CenterTableItems(ui->partitionTable);
    ui->refreshPartitionButton->setEnabled(true);
    UiHelpers::SetPageLoading(this, false);
}
