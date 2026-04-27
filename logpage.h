#ifndef LOGPAGE_H
#define LOGPAGE_H

#include <QWidget>

class QTimer;

QT_BEGIN_NAMESPACE
namespace Ui {
class LogPage;
}
QT_END_NAMESPACE

class LogPage : public QWidget
{
    Q_OBJECT

public:
    explicit LogPage(QWidget *parent = nullptr);
    ~LogPage() override;

    void ReloadTodayLogs();
    void ReloadCurrentSessionLogs();

private:
    void RefreshVisibleLogs();
    void ClearVisibleLogs();

    Ui::LogPage *ui;
    QTimer *m_refreshTimer = nullptr;
    bool m_showTodayLogs = false;
    bool m_refreshPaused = false;
    int m_clearedTodayLogCount = 0;
    int m_clearedSessionLogCount = 0;
};

#endif // LOGPAGE_H
