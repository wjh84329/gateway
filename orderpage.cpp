#include "orderpage.h"
#include "ui_orderpage.h"

#include "pageutils.h"

#include <QDate>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTableWidgetItem>
#include <QTimer>
#include <QtConcurrent/QtConcurrent>

OrderPage::OrderPage(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::OrderPage)
{
    ui->setupUi(this);

    ui->startDateEdit->setDate(QDate::currentDate());
    ui->startDateEdit->setDisplayFormat(QStringLiteral("yyyy/M/d"));
    ui->startDateEdit->setCalendarPopup(true);
    ui->startDateEdit->setFixedSize(102, 28);

    ui->endDateEdit->setDate(QDate::currentDate());
    ui->endDateEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    ui->endDateEdit->setCalendarPopup(true);
    ui->endDateEdit->setFixedSize(100, 28);

    ui->accountEdit->setFixedSize(96, 28);
    ui->exchangeTypeComboBox->addItems({
        QStringLiteral("充值"),
        QStringLiteral("全区补发"),
        QStringLiteral("手动补发")
    });
    ui->exchangeTypeComboBox->setFixedSize(90, 28);
    ui->partitionComboBox->setFixedSize(120, 28);
    ui->searchOrderButton->setProperty("partitionAction", true);
    ui->searchOrderButton->setFixedSize(100, 30);

    ui->orderTable->setColumnCount(5);
    ui->orderTable->setHorizontalHeaderLabels({
        QStringLiteral("兑换分区"),
        QStringLiteral("兑换账号"),
        QStringLiteral("金额"),
        QStringLiteral("兑换方式"),
        QStringLiteral("赠送金额")
    });

    UiHelpers::ConfigureReadonlyTable(ui->orderTable);
    ui->orderTable->horizontalHeader()->setStretchLastSection(false);
    ui->orderTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->orderTable->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    connect(ui->searchOrderButton, &QPushButton::clicked, this, &OrderPage::onSearchClicked);

    QTimer::singleShot(0, this, &OrderPage::loadPartitions);
}

OrderPage::~OrderPage()
{
    delete ui;
}

void OrderPage::loadPartitions()
{
    QtConcurrent::run([this] {
        QString error;
        const QJsonArray partitions = m_api.GetPartitions(&error);
        QMetaObject::invokeMethod(this, [this, partitions] {
            ui->partitionComboBox->clear();
            ui->partitionComboBox->addItem(QStringLiteral("全部分区"), 0);
            for (const QJsonValue &val : partitions) {
                const QJsonObject obj = val.toObject();
                const QString name = obj.value(QStringLiteral("PartitionName")).toString(
                    obj.value(QStringLiteral("partitionName")).toString(
                        obj.value(QStringLiteral("Name")).toString(
                            obj.value(QStringLiteral("name")).toString())));
                const int id = obj.value(QStringLiteral("PartitionId")).toInt(
                    obj.value(QStringLiteral("partitionId")).toInt(
                        obj.value(QStringLiteral("Id")).toInt(
                            obj.value(QStringLiteral("id")).toInt())));
                if (!name.isEmpty()) {
                    ui->partitionComboBox->addItem(name, id);
                }
            }
        }, Qt::QueuedConnection);
    });
}

void OrderPage::onSearchClicked()
{
    UiHelpers::SetPageLoading(this, true, QStringLiteral("查询中..."));
    ui->searchOrderButton->setEnabled(false);

    QJsonObject query;
    query[QStringLiteral("StartTime")] = ui->startDateEdit->date().toString(QStringLiteral("yyyy-MM-dd"));
    query[QStringLiteral("EndTime")]   = ui->endDateEdit->date().toString(QStringLiteral("yyyy-MM-dd"));

    const QString account = ui->accountEdit->text().trimmed();
    if (!account.isEmpty()) {
        query[QStringLiteral("Account")] = account;
    }

    query[QStringLiteral("ExchangeType")] = ui->exchangeTypeComboBox->currentText();

    const int partitionId = ui->partitionComboBox->currentData().toInt();
    if (partitionId > 0) {
        query[QStringLiteral("PartitionId")] = partitionId;
    }

    QtConcurrent::run([this, query] {
        QString error;
        const QJsonDocument doc = m_api.GetOrders(query, &error);

        QJsonArray orders;
        if (doc.isArray()) {
            orders = doc.array();
        } else if (doc.isObject()) {
            const QJsonObject obj = doc.object();
            for (const QString &key : {QStringLiteral("data"), QStringLiteral("Data"),
                                        QStringLiteral("list"), QStringLiteral("List"),
                                        QStringLiteral("rows"), QStringLiteral("Rows")}) {
                if (obj.contains(key) && obj.value(key).isArray()) {
                    orders = obj.value(key).toArray();
                    break;
                }
            }
        }

        QMetaObject::invokeMethod(this, [this, error, orders] {
            UiHelpers::SetPageLoading(this, false);
            ui->searchOrderButton->setEnabled(true);
            if (!error.isEmpty()) {
                UiHelpers::ShowOverlayMessage(this, QMessageBox::Critical, error);
                return;
            }
            populateTable(orders);
        }, Qt::QueuedConnection);
    });
}

void OrderPage::populateTable(const QJsonArray &orders)
{
    ui->orderTable->setRowCount(0);

    for (const QJsonValue &val : orders) {
        const QJsonObject obj = val.toObject();
        const int row = ui->orderTable->rowCount();
        ui->orderTable->insertRow(row);

        auto cell = [&](int col, const QString &text) {
            auto *item = new QTableWidgetItem(text);
            item->setTextAlignment(Qt::AlignCenter);
            ui->orderTable->setItem(row, col, item);
        };

        cell(0, obj.value(QStringLiteral("partitionName")).toString());
        cell(1, obj.value(QStringLiteral("playerAccount")).toString());
        cell(2, QString::number(obj.value(QStringLiteral("amount")).toDouble()));
        cell(3, obj.value(QStringLiteral("rechargeMethod")).toString());
        cell(4, QString::number(obj.value(QStringLiteral("giveAmount")).toDouble()));
    }

    ui->orderCountLabel->setText(QStringLiteral("共 %1 条记录").arg(orders.size()));
}
