#include "gatewaysingleinstance.h"

#include "applogger.h"
#include "mainwindow.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QLockFile>
#include <QLocalServer>
#include <QLocalSocket>
#include <QStandardPaths>
#include <QTimer>

namespace {

QString InstallPathDigestHex32()
{
    const QString path = QDir::cleanPath(QCoreApplication::applicationDirPath());
    return QString::fromLatin1(QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha256).toHex().left(32));
}

QString InstanceServerName()
{
    return QStringLiteral("cs7x_gw_") + InstallPathDigestHex32();
}

/// 锁文件放在系统临时目录，避免安装目录旁多出可见文件；仍按部署路径哈希区分不同拷贝。
QString InstanceLockFilePath()
{
    const QString name = QStringLiteral("cs7x_gw_inst_") + InstallPathDigestHex32() + QStringLiteral(".lock");
    return QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation)).filePath(name);
}

} // namespace

GatewaySingleInstance &GatewaySingleInstance::instance()
{
    static GatewaySingleInstance inst;
    return inst;
}

GatewaySingleInstance::GatewaySingleInstance()
    : QObject(nullptr)
{
    m_server = new QLocalServer(this);
    connect(m_server, &QLocalServer::newConnection, this, &GatewaySingleInstance::onNewConnection);
}

GatewaySingleInstance::~GatewaySingleInstance()
{
    shutdownForAppExit();
}

void GatewaySingleInstance::shutdownForAppExit()
{
    if (m_server) {
        m_server->close();
        QLocalServer::removeServer(InstanceServerName());
        delete m_server;
        m_server = nullptr;
    }
    m_lockFile.reset();
}

bool GatewaySingleInstance::tryAcquirePrimaryOrNotifyExisting()
{
    const QString lockPath = InstanceLockFilePath();

    // Qt 6：QLockFile 须在构造时传入路径，无默认构造 / setFileName。
    m_lockFile = std::make_unique<QLockFile>(lockPath);
    m_lockFile->setStaleLockTime(15000);

    bool haveLock = m_lockFile->tryLock(300);
    const QString name = InstanceServerName();

    if (!haveLock) {
        QLocalSocket probe;
        probe.connectToServer(name);
        if (probe.waitForConnected(3000)) {
            probe.write("ACTIVATE\n");
            probe.flush();
            probe.waitForBytesWritten(1000);
            probe.disconnectFromServer();
            m_lockFile.reset();
            // AppLogger::WriteLog(QStringLiteral("检测到同目录已有网关进程在运行，已请求唤醒主窗口；本进程退出。"));
            return false;
        }

        if (m_lockFile->removeStaleLockFile()) {
            haveLock = m_lockFile->tryLock(300);
        }
        if (!haveLock) {
            m_lockFile.reset();
            AppLogger::WriteLog(QStringLiteral("同目录已有网关在运行，且未能连接唤醒服务；本进程退出。"));
            return false;
        }
    }

    // 仅当已持有目录锁时才清理管道名，避免误删仍在服务中的主进程管道（否则会再开一份实例）。
    if (!m_server->listen(name)) {
        QLocalServer::removeServer(name);
        if (!m_server->listen(name)) {
            AppLogger::WriteLog(QStringLiteral("网关单实例唤醒服务绑定失败：%1").arg(m_server->errorString()));
        }
    }
    return true;
}

void GatewaySingleInstance::setMainWindow(MainWindow *mainWindow)
{
    m_mainWindow = mainWindow;
    if (m_pendingActivate && m_mainWindow) {
        m_pendingActivate = false;
        activateMainWindow();
    }
}

void GatewaySingleInstance::onNewConnection()
{
    QLocalSocket *socket = m_server->nextPendingConnection();
    if (!socket) {
        return;
    }
    connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
    if (m_mainWindow) {
        activateMainWindow();
    } else {
        m_pendingActivate = true;
    }
    socket->disconnectFromServer();
}

void GatewaySingleInstance::activateMainWindow()
{
    if (!m_mainWindow) {
        return;
    }
    MainWindow *mw = m_mainWindow;
    QTimer::singleShot(0, mw, [mw]() { mw->ShowFromTray(); });
}
