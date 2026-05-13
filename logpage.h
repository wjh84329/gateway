#ifndef LOGPAGE_H
#define LOGPAGE_H

#include "applogger.h"

#include <QList>
#include <QTableWidget>
#include <QWidget>

class QTimer;

QT_BEGIN_NAMESPACE
class QEvent;
class QModelIndex;
class QMouseEvent;
QT_END_NAMESPACE

QT_BEGIN_NAMESPACE
namespace Ui {
class LogPage;
}
QT_END_NAMESPACE

/// 系统日志只读展示；Qt 6 下单击仍可能进内联编辑，需重写三参数 edit（与分组管理表一致）。
class LogTableWidget : public QTableWidget
{
public:
    explicit LogTableWidget(QWidget *parent = nullptr);

protected:
    bool edit(const QModelIndex &index, EditTrigger trigger, QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
};

class LogPage : public QWidget
{
    Q_OBJECT

public:
    explicit LogPage(QWidget *parent = nullptr);
    ~LogPage() override;

    void ReloadTodayLogs();
    void ReloadCurrentSessionLogs();

private slots:
    void OnLogScrollValueChanged(int value);

private:
    void RefreshVisibleLogs();
    void ClearVisibleLogs();
    void PopulateLogTable(const QList<LogEntry> &logs);

    Ui::LogPage *ui;
    QTimer *m_refreshTimer = nullptr;
    bool m_showTodayLogs = false;
    bool m_refreshPaused = false;
    int m_clearedTodayLogCount = 0;
    int m_clearedSessionLogCount = 0;
    /// 用户停在列表底部附近时，刷新后自动跟到底部；向上翻阅时保持视口
    bool m_stickToBottom = true;
    bool m_blockingScrollSignal = false;
};

#endif // LOGPAGE_H
