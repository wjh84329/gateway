#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QCloseEvent>
#include <QEvent>
#include <QIcon>
#include <QMainWindow>
#include <QPoint>
#include <QSystemTrayIcon>

QT_BEGIN_NAMESPACE
class QFileSystemWatcher;
class QLabel;
class QMenu;
class QTimer;
class QToolButton;
class QWidget;
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class GatewayPresenceController;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    /// 从程序目录加载 .ico/.png（cs.ico、app.ico 等），供窗口与托盘使用
    static QIcon ApplicationIcon();

    /// 显示并激活主窗口（含从托盘恢复），供单实例二次启动唤醒
    void ShowFromTray();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void changeEvent(QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    void ApplyMainWindowStyleSheet();
    void SyncWindowMaximizeGlyph();
    void UpdateGatewayListenDisplay();
    void RefreshUpdateStatusLineAsync();
    void SetupTrayIcon();
    void HideToTray();
    void QuitApplication();
    void ShowCloseChoiceDialog();
    void OnTrayActivated(QSystemTrayIcon::ActivationReason reason);
    /// 顶栏整块可拖移窗口；导航按钮与标题栏按钮仍正常点击
    void InstallNavBarDragFilters(QWidget *navBar);

    Ui::MainWindow *ui;
    QString m_styleSheetBase;
    QFileSystemWatcher *m_styleSheetWatcher = nullptr;
    QToolButton *m_windowMaximizeButton = nullptr;
    QLabel *m_updateStatusLabel = nullptr;
    QTimer *m_updateStatusPollTimer = nullptr;
    int m_updateStatusRequestSeq = 0;
    QLabel *m_gatewayListenStatusLabel = nullptr;
    QSystemTrayIcon *m_trayIcon = nullptr;
    QMenu *m_trayMenu = nullptr;
    QWidget *m_navBarFrame = nullptr;
    bool m_dragging = false;
    QPoint m_dragPosition;
    bool m_allowClose = false;
    GatewayPresenceController *m_presence = nullptr;
};
#endif // MAINWINDOW_H
