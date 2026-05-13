#include "logpage.h"
#include "ui_logpage.h"

#include "applogger.h"
#include "pageutils.h"

#include <QAbstractItemDelegate>
#include <QAbstractItemView>
#include <QEvent>
#include <QHeaderView>
#include <QModelIndex>
#include <QMouseEvent>
#include <QScrollBar>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QStyledItemDelegate>
#include <QTableWidgetItem>
#include <QTimer>

namespace {

class ReadOnlyLogTableItemDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QWidget *createEditor(QWidget *parent,
                          const QStyleOptionViewItem &option,
                          const QModelIndex &index) const override
    {
        Q_UNUSED(parent);
        Q_UNUSED(option);
        Q_UNUSED(index);
        return nullptr;
    }

    void initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const override
    {
        QStyledItemDelegate::initStyleOption(option, index);
        if (option) {
            // 避免绘制成“可输入”的焦点框/文本光标状（只读日志仍会出现）
            option->state &= ~QStyle::State_HasFocus;
        }
    }
};

} // namespace

LogTableWidget::LogTableWidget(QWidget *parent)
    : QTableWidget(parent)
{
    setAttribute(Qt::WA_InputMethodEnabled, false);
    if (viewport()) {
        viewport()->setAttribute(Qt::WA_InputMethodEnabled, false);
    }
}

bool LogTableWidget::edit(const QModelIndex &index, EditTrigger trigger, QEvent *event)
{
    Q_UNUSED(index);
    Q_UNUSED(trigger);
    Q_UNUSED(event);
    return false;
}

void LogTableWidget::mousePressEvent(QMouseEvent *event)
{
    QTableWidget::mousePressEvent(event);
    if (state() == QAbstractItemView::EditingState) {
        const QModelIndex idx = currentIndex();
        if (idx.isValid()) {
            if (QWidget *w = indexWidget(idx)) {
                closeEditor(w, QAbstractItemDelegate::NoHint);
            }
        }
    }
    const int r = currentRow();
    if (r >= 0) {
        for (int c = 0; c < columnCount(); ++c) {
            if (QTableWidgetItem *it = item(r, c)) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
                if (isPersistentEditorOpen(it)) {
                    closePersistentEditor(it);
                }
#else
                const QModelIndex idx = model()->index(r, c);
                if (idx.isValid() && isPersistentEditorOpen(idx)) {
                    closePersistentEditor(it);
                }
#endif
            }
        }
    }
}

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
    ui->logTable->setItemDelegate(new ReadOnlyLogTableItemDelegate(ui->logTable));
    // 其它页用 NoSelection 防误触；日志需要选中、Ctrl+C 复制
    ui->logTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    ui->logTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->logTable->setFocusPolicy(Qt::StrongFocus);
    ui->logTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->logTable->setTabKeyNavigation(false);
    ui->logTable->setShowGrid(false);
    ui->logTable->horizontalHeader()->setStretchLastSection(true);
    ui->logTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    ui->logTable->horizontalHeader()->setMinimumSectionSize(120);

    connect(ui->logTable->verticalScrollBar(), &QScrollBar::valueChanged,
            this, &LogPage::OnLogScrollValueChanged);

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

void LogPage::OnLogScrollValueChanged(int value)
{
    if (m_blockingScrollSignal) {
        return;
    }
    QScrollBar *sb = ui->logTable->verticalScrollBar();
    const int maxV = sb->maximum();
    m_stickToBottom = (maxV <= 0 || value >= maxV - 4);
}

void LogPage::PopulateLogTable(const QList<LogEntry> &logs)
{
    int topRow = -1;
    if (!m_stickToBottom && ui->logTable->rowCount() > 0) {
        topRow = ui->logTable->rowAt(ui->logTable->viewport()->rect().top() + 2);
    }

    m_blockingScrollSignal = true;
    // 定时全表刷新时若保留 currentIndex，易残留“编辑/光标”绘制
    ui->logTable->setCurrentItem(nullptr);
    ui->logTable->setRowCount(0);
    ui->logTable->setRowCount(logs.size());

    const Qt::ItemFlags logItemFlags = Qt::ItemIsSelectable | Qt::ItemIsEnabled;
    for (int row = 0; row < logs.size(); ++row) {
        auto *timeItem = new QTableWidgetItem(logs.at(row).time);
        timeItem->setFlags(logItemFlags);
        ui->logTable->setItem(row, 0, timeItem);
        auto *msgItem = new QTableWidgetItem(logs.at(row).message);
        msgItem->setFlags(logItemFlags);
        ui->logTable->setItem(row, 1, msgItem);
    }

    UiHelpers::CenterTableItems(ui->logTable);
    for (int row = 0; row < ui->logTable->rowCount(); ++row) {
        if (auto *logItem = ui->logTable->item(row, 1)) {
            logItem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        }
    }

    if (m_stickToBottom) {
        ui->logTable->scrollToBottom();
    } else if (topRow >= 0 && topRow < ui->logTable->rowCount()) {
        if (QTableWidgetItem *it = ui->logTable->item(topRow, 0)) {
            ui->logTable->scrollToItem(it, QAbstractItemView::PositionAtTop);
        }
    }
    m_blockingScrollSignal = false;
}

void LogPage::ReloadTodayLogs()
{
    m_refreshPaused = false;
    m_showTodayLogs = true;
    auto logs = AppLogger::LoadTodayLogs();
    if (m_clearedTodayLogCount > 0) {
        logs = logs.mid(qMin(m_clearedTodayLogCount, logs.size()));
    }
    PopulateLogTable(logs);
}

void LogPage::ReloadCurrentSessionLogs()
{
    m_refreshPaused = false;
    m_showTodayLogs = false;
    auto logs = AppLogger::LoadCurrentSessionLogs();
    if (m_clearedSessionLogCount > 0) {
        logs = logs.mid(qMin(m_clearedSessionLogCount, logs.size()));
    }
    PopulateLogTable(logs);
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
