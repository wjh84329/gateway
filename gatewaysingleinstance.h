#ifndef GATEWAYSINGLEINSTANCE_H
#define GATEWAYSINGLEINSTANCE_H

#include <QObject>
#include <memory>

class MainWindow;
class QLocalServer;
class QLockFile;

/// 同部署目录仅保留一个「主」进程：第二进程启动时唤醒已运行的主窗口并退出（QLockFile 在系统 Temp，不在安装目录）。
class GatewaySingleInstance final : public QObject
{
    Q_OBJECT
public:
    static GatewaySingleInstance &instance();

    /// 尝试成为主进程并监听本地套接字；若同目录已有主进程则向对方发送唤醒并返回 false（应直接 exit）。
    bool tryAcquirePrimaryOrNotifyExisting();

    void setMainWindow(MainWindow *mainWindow);

    /// 在 QApplication 仍存活时调用（如 aboutToQuit）。静态单例不得作为 qApp 的子对象，否则退出时与静态析构双重释放堆损坏（如 0xc0000374）。
    void shutdownForAppExit();

private:
    explicit GatewaySingleInstance();
    ~GatewaySingleInstance() override;

    void onNewConnection();
    void activateMainWindow();

    QLocalServer *m_server = nullptr;
    std::unique_ptr<QLockFile> m_lockFile;
    MainWindow *m_mainWindow = nullptr;
    bool m_pendingActivate = false;
};

#endif // GATEWAYSINGLEINSTANCE_H
