#include "orderpage.h"
#include "ui_orderpage.h"

#include "pageutils.h"

#include <QDate>
#include <QHeaderView>
#include <QHideEvent>
#include <QMessageBox>
#include <QShowEvent>
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

void OrderPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    requestAutoQueryOnShow();
}

void OrderPage::hideEvent(QHideEvent *event)
{
    m_deferredQueryAfterPartitions = false;
    QWidget::hideEvent(event);
}

void OrderPage::requestAutoQueryOnShow()
{
    if (ui->partitionComboBox->count() == 0) {
        m_deferredQueryAfterPartitions = true;
        return;
    }
    m_deferredQueryAfterPartitions = false;
    onSearchClicked();
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
            if (m_deferredQueryAfterPartitions && isVisible()) {
                m_deferredQueryAfterPartitions = false;
                onSearchClicked();
            }
        }, Qt::QueuedConnection);
    });
}

void OrderPage::onSearchClicked()
{
    const QDate startD = ui->startDateEdit->date();
    const QDate endD = ui->endDateEdit->date();
    if (startD > endD) {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("开始日期不能晚于结束日期。"));
        return;
    }

    UiHelpers::SetPageLoading(this, true, QStringLiteral("查询中..."));
    ui->searchOrderButton->setEnabled(false);

    QJsonObject query;
    // 与 TenantServer GetClientOrders 一致：区间为 [StartTime, EndTime]，且要求 StartTime < EndTime。
    // 仅传日期时两端都会变成当日 00:00:00，同一天查询会相等触发 400「开始时间不能大于等于结束时间」。
    // 对齐老网关 UserControlOrder：结束日为当日 23:59:59（原 AddSeconds(86399)）。
    query[QStringLiteral("StartTime")] =
        QStringLiteral("%1 00:00:00").arg(startD.toString(QStringLiteral("yyyy-MM-dd")));
    query[QStringLiteral("EndTime")] =
        QStringLiteral("%1 23:59:59").arg(endD.toString(QStringLiteral("yyyy-MM-dd")));

    const QString account = ui->accountEdit->text().trimmed();
    if (!account.isEmpty()) {
        // TenantServer OrderClientQueryModel 字段为 PlayerAccount（与旧网关 HttpClients 一致）
        query[QStringLiteral("PlayerAccount")] = account;
    }

    query[QStringLiteral("ExchangeType")] = ui->exchangeTypeComboBox->currentText();

    const int partitionId = ui->partitionComboBox->currentData().toInt();
    if (partitionId > 0) {
        query[QStringLiteral("PartitionId")] = partitionId;
    }

    query[QStringLiteral("PageNumber")] = 1;
    query[QStringLiteral("PageCount")] = 500;

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

        auto str = [](const QJsonObject &o, const QStringList &keys) -> QString {
            for (const QString &k : keys) {
                const QJsonValue v = o.value(k);
                if (v.isString() && !v.toString().isEmpty()) {
                    return v.toString();
                }
                if (v.isDouble()) {
                    return QString::number(v.toDouble());
                }
            }
            return {};
        };
        cell(0, str(obj, {QStringLiteral("partitionName"), QStringLiteral("PartitionName")}));
        cell(1, str(obj, {QStringLiteral("playerAccount"), QStringLiteral("PlayerAccount")}));
        {
            const QJsonValue amountV = obj.contains(QStringLiteral("amount")) ? obj.value(QStringLiteral("amount"))
                                                                            : obj.value(QStringLiteral("Amount"));
            cell(2, QString::number(amountV.toDouble()));
        }
        cell(3, str(obj, {QStringLiteral("rechargeMethod"), QStringLiteral("RechargeMethod")}));
        {
            const QJsonValue giveV = obj.contains(QStringLiteral("giveAmount")) ? obj.value(QStringLiteral("giveAmount"))
                                                                               : obj.value(QStringLiteral("GiveAmount"));
            cell(4, QString::number(giveV.toDouble()));
        }
    }

    ui->orderCountLabel->setText(QStringLiteral("共 %1 条记录").arg(orders.size()));
}
