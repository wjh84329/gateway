#include "logpage.h"
#include "ui_logpage.h"

#include "applogger.h"
#include "pageutils.h"

#include <QHeaderView>
#include <QTableWidgetItem>
#include <QTimer>

LogPage::LogPage(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::LogPage)
    , m_refreshTimer(new QTimer(this))
{
    ui->setupUi(this);

    ui->loadTodayLogButton->setProperty("partitionAction", true);
    ui->loadTodayLogButton->setFixedSize(100, 30);
    ui->clearLogButton->setProperty("partitionAction", true);
    ui->clearLogButton->setFixedSize(100, 30);

    ui->logTable->setColumnCount(2);
    ui->logTable->setHorizontalHeaderLabels({QStringLiteral("时间"), QStringLiteral("日志")});
    ui->logTable->setRowCount(0);

    UiHelpers::ConfigureReadonlyTable(ui->logTable);
    ui->logTable->setShowGrid(false);
    ui->logTable->horizontalHeader()->setStretchLastSection(true);
    ui->logTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    ui->logTable->horizontalHeader()->setMinimumSectionSize(120);

    connect(ui->loadTodayLogButton, &QPushButton::clicked, this, &LogPage::ReloadTodayLogs);
    connect(ui->clearLogButton, &QPushButton::clicked, this, &LogPage::ClearVisibleLogs);
    connect(m_refreshTimer, &QTimer::timeout, this, &LogPage::RefreshVisibleLogs);
    m_refreshTimer->start(1000);

    ReloadCurrentSessionLogs();
}

LogPage::~LogPage()
{
    delete ui;
}

void LogPage::ReloadTodayLogs()
{
    m_refreshPaused = false;
    m_showTodayLogs = true;
    auto logs = AppLogger::LoadTodayLogs();
    if (m_clearedTodayLogCount > 0) {
        logs = logs.mid(qMin(m_clearedTodayLogCount, logs.size()));
    }
    ui->logTable->setRowCount(0);
    ui->logTable->setRowCount(logs.size());

    for (int row = 0; row < logs.size(); ++row) {
        ui->logTable->setItem(row, 0, new QTableWidgetItem(logs.at(row).time));
        ui->logTable->setItem(row, 1, new QTableWidgetItem(logs.at(row).message));
    }

    UiHelpers::CenterTableItems(ui->logTable);
    for (int row = 0; row < ui->logTable->rowCount(); ++row) {
        if (auto *logItem = ui->logTable->item(row, 1)) {
            logItem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        }
    }

    ui->logTable->scrollToBottom();
}

void LogPage::ReloadCurrentSessionLogs()
{
    m_refreshPaused = false;
    m_showTodayLogs = false;
    auto logs = AppLogger::LoadCurrentSessionLogs();
    if (m_clearedSessionLogCount > 0) {
        logs = logs.mid(qMin(m_clearedSessionLogCount, logs.size()));
    }
    ui->logTable->setRowCount(0);
    ui->logTable->setRowCount(logs.size());

    for (int row = 0; row < logs.size(); ++row) {
        ui->logTable->setItem(row, 0, new QTableWidgetItem(logs.at(row).time));
        ui->logTable->setItem(row, 1, new QTableWidgetItem(logs.at(row).message));
    }

    UiHelpers::CenterTableItems(ui->logTable);
    for (int row = 0; row < ui->logTable->rowCount(); ++row) {
        if (auto *logItem = ui->logTable->item(row, 1)) {
            logItem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        }
    }

    ui->logTable->scrollToBottom();
}

void LogPage::RefreshVisibleLogs()
{
    if (m_refreshPaused) {
        return;
    }

    if (m_showTodayLogs) {
        ReloadTodayLogs();
    } else {
        ReloadCurrentSessionLogs();
    }
}

void LogPage::ClearVisibleLogs()
{
    m_refreshPaused = false;
    if (m_showTodayLogs) {
        m_clearedTodayLogCount = AppLogger::LoadTodayLogs().size();
    } else {
        m_clearedSessionLogCount = AppLogger::LoadCurrentSessionLogs().size();
    }
    ui->logTable->setRowCount(0);
}
