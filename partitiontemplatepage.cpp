#include "partitiontemplatepage.h"
#include "ui_partitiontemplatepage.h"

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
}

PartitionTemplatePage::PartitionTemplatePage(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::PartitionTemplatePage)
{
    ui->setupUi(this);

    ui->addTemplateButton->setProperty("partitionAction", true);
    ui->addTemplateButton->setFixedSize(100, 30);
    ui->refreshTemplateButton->setProperty("partitionAction", true);
    ui->refreshTemplateButton->setFixedSize(100, 30);
    connect(ui->refreshTemplateButton, &QPushButton::clicked, this, &PartitionTemplatePage::LoadTemplates);

    ui->templateTable->setColumnCount(4);
    ui->templateTable->setHorizontalHeaderLabels({
        QStringLiteral("编号"),
        QStringLiteral("模板名称"),
        QStringLiteral("游戏币"),
        QStringLiteral("比例")
    });
    ui->templateTable->setRowCount(0);

    UiHelpers::ConfigureReadonlyTable(ui->templateTable);
    ui->templateTable->horizontalHeader()->setStretchLastSection(false);
    ui->templateTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->templateTable->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    UiHelpers::CenterTableItems(ui->templateTable);

    LoadTemplates();
}

PartitionTemplatePage::~PartitionTemplatePage()
{
    delete ui;
}

void PartitionTemplatePage::LoadTemplates()
{
    ui->refreshTemplateButton->setEnabled(false);
    UiHelpers::SetPageLoading(this, true);

    GatewayApiClient client;
    QString errorMessage;
    const QJsonArray templates = client.GetTemplates(&errorMessage);
    ui->templateTable->setRowCount(0);

    if (templates.isEmpty()) {
        if (!errorMessage.isEmpty()) {
            AppLogger::WriteLog(QStringLiteral("加载模板列表失败：%1").arg(errorMessage));
        }
        ui->refreshTemplateButton->setEnabled(true);
        UiHelpers::SetPageLoading(this, false);
        return;
    }

    ui->templateTable->setRowCount(templates.size());
    for (int row = 0; row < templates.size(); ++row) {
        const QJsonObject templateObject = templates.at(row).toObject();
        ui->templateTable->setItem(row, 0, new QTableWidgetItem(ReadStringField(templateObject, {QStringLiteral("Id"), QStringLiteral("id")})));
        ui->templateTable->setItem(row, 1, new QTableWidgetItem(ReadStringField(templateObject, {QStringLiteral("Name"), QStringLiteral("name")})));
        ui->templateTable->setItem(row, 2, new QTableWidgetItem(ReadStringField(templateObject, {QStringLiteral("CurrencyName"), QStringLiteral("currencyName")})));
        ui->templateTable->setItem(row, 3, new QTableWidgetItem(ReadStringField(templateObject, {QStringLiteral("Ratio"), QStringLiteral("ratio")})));
    }

    UiHelpers::CenterTableItems(ui->templateTable);
    ui->refreshTemplateButton->setEnabled(true);
    UiHelpers::SetPageLoading(this, false);
}
