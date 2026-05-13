#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLabel>
#include <QProcess>
#include <QScreen>
#include <QVBoxLayout>
#include <QWidget>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>

#include <string>

#if defined(Q_OS_WIN) && QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
#include <QtPlugin>
Q_IMPORT_PLUGIN(QWindowsIntegrationPlugin)
#endif

#include <QCoreApplication>
#include <QProcessEnvironment>

static bool shellExecuteBatHiddenWait(const QString &batPath, DWORD *exitOut)
{
    if (!exitOut) {
        return false;
    }
    *exitOut = static_cast<DWORD>(-1);

    const QString nativeBat = QDir::toNativeSeparators(batPath);
    const QString workDir = QFileInfo(batPath).absolutePath();
    const std::wstring batW = nativeBat.toStdWString();
    const std::wstring dirW = QDir::toNativeSeparators(workDir).toStdWString();

    SHELLEXECUTEINFOW sei = {};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
    sei.hwnd = nullptr;
    sei.lpVerb = nullptr;
    sei.lpFile = batW.c_str();
    sei.lpParameters = nullptr;
    sei.lpDirectory = dirW.empty() ? nullptr : dirW.c_str();
    sei.nShow = SW_HIDE;

    if (!ShellExecuteExW(&sei)) {
        return false;
    }
    HANDLE h = sei.hProcess;
    if (!h) {
        return false;
    }
    constexpr DWORD kWaitMs = 600000;
    const DWORD w = WaitForSingleObject(h, kWaitMs);
    DWORD code = static_cast<DWORD>(-1);
    if (w == WAIT_OBJECT_0) {
        GetExitCodeProcess(h, &code);
    }
    CloseHandle(h);
    *exitOut = code;
    return w == WAIT_OBJECT_0;
}

static bool shellExecuteViaUtf8LauncherBat(const QString &batPath, DWORD *exitOut)
{
    if (!exitOut) {
        return false;
    }
    *exitOut = static_cast<DWORD>(-1);

    const QString launcher = QDir::temp().filePath(
        QStringLiteral("gateway_UPDATE_launch_%1.bat").arg(QCoreApplication::applicationPid()));
    QFile::remove(launcher);
    QFile lf(launcher);
    if (!lf.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    static const char kBom[] = "\xEF\xBB\xBF";
    lf.write(kBom, 3);
    const QString inner = QStringLiteral("@echo off\r\nchcp 65001>nul\r\ncall \"%1\"\r\n")
                              .arg(QDir::toNativeSeparators(batPath));
    lf.write(inner.toUtf8());
    lf.close();

    const bool ok = shellExecuteBatHiddenWait(launcher, exitOut);
    QFile::remove(launcher);
    return ok;
}

/// ShellExecute 对 .bat 偶发不返回 hProcess；用 cmd.exe + Unicode lpParameters 再试。
static bool shellExecuteCmdExeCallBat(const QString &batPath, DWORD *exitOut)
{
    if (!exitOut) {
        return false;
    }
    *exitOut = static_cast<DWORD>(-1);

    const QString nativeBat = QDir::toNativeSeparators(batPath);
    const QString workDir = QFileInfo(batPath).absolutePath();
    // cmd：含空格/中文路径时用 /s /c ""path""（见 cmd /?）
    const QString params = QStringLiteral("/d /s /c \"\"%1\"\"").arg(nativeBat);
    const QString cmdExe = QProcessEnvironment::systemEnvironment().value(
        QStringLiteral("COMSPEC"), QStringLiteral("C:\\Windows\\System32\\cmd.exe"));
    const std::wstring cmdFile = QDir::toNativeSeparators(cmdExe).toStdWString();
    const std::wstring paramW = params.toStdWString();
    const std::wstring dirW = QDir::toNativeSeparators(workDir).toStdWString();

    SHELLEXECUTEINFOW sei = {};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
    sei.hwnd = nullptr;
    sei.lpVerb = nullptr;
    sei.lpFile = cmdFile.c_str();
    sei.lpParameters = paramW.c_str();
    sei.lpDirectory = dirW.empty() ? nullptr : dirW.c_str();
    sei.nShow = SW_HIDE;

    if (!ShellExecuteExW(&sei)) {
        return false;
    }
    HANDLE h = sei.hProcess;
    if (!h) {
        return false;
    }
    constexpr DWORD kWaitMs = 600000;
    const DWORD w = WaitForSingleObject(h, kWaitMs);
    DWORD code = static_cast<DWORD>(-1);
    if (w == WAIT_OBJECT_0) {
        GetExitCodeProcess(h, &code);
    }
    CloseHandle(h);
    *exitOut = code;
    return w == WAIT_OBJECT_0;
}

namespace {

void centerOnPrimary(QWidget *w)
{
    if (!w) {
        return;
    }
    w->adjustSize();
    QScreen *scr = QGuiApplication::primaryScreen();
    if (!scr) {
        return;
    }
    const QRect ag = scr->availableGeometry();
    QRect fr = w->frameGeometry();
    fr.moveCenter(ag.center());
    if (fr.left() < ag.left()) {
        fr.moveLeft(ag.left());
    }
    if (fr.top() < ag.top()) {
        fr.moveTop(ag.top());
    }
    if (fr.right() > ag.right()) {
        fr.moveRight(ag.right());
    }
    if (fr.bottom() > ag.bottom()) {
        fr.moveBottom(ag.bottom());
    }
    w->move(fr.topLeft());
}

void scheduleDeleteSelfExecutable()
{
    const QString self = QDir::toNativeSeparators(QFileInfo(QCoreApplication::applicationFilePath()).absoluteFilePath());
    // 进程退出后再删自身；路径须引号防空格
    const QString inner = QStringLiteral("ping 127.0.0.1 -n 2 >nul & del /f \"%1\"").arg(self);
    QProcess::startDetached(QStringLiteral("cmd.exe"), {QStringLiteral("/c"), inner});
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("GatewayUpdate"));
    QApplication::setApplicationDisplayName(QStringLiteral("网关更新"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("网关在线更新助手"));
    const QCommandLineOption batFileOpt(QStringList() << QStringLiteral("bat-file"),
                                        QStringLiteral("UTF-8 文本文件，首行为 bat 完整路径（避免中文路径在子进程命令行损坏）"),
                                        QStringLiteral("file"));
    const QCommandLineOption batOpt(QStringList() << QStringLiteral("bat"),
                                    QStringLiteral("gateway_apply_update.bat 的完整路径"),
                                    QStringLiteral("path"));
    parser.addOption(batFileOpt);
    parser.addOption(batOpt);
    parser.addHelpOption();
    parser.process(app);

    QString batPath;
    if (parser.isSet(batFileOpt)) {
        const QString marker = parser.value(batFileOpt);
        QFile mf(marker);
        if (!mf.open(QIODevice::ReadOnly | QIODevice::Text)) {
            return 1;
        }
        const QByteArray line = mf.readLine();
        mf.close();
        QFile::remove(marker);
        batPath = QString::fromUtf8(line).trimmed();
    } else if (parser.isSet(batOpt)) {
        batPath = parser.value(batOpt).trimmed();
    } else {
        return 1;
    }
    if (batPath.isEmpty() || !QFileInfo::exists(batPath)) {
        return 2;
    }

    QWidget window;
    // 勿用 Qt::Tool：Windows 上易出现窄系统标题栏，「网关更新」等字顶被裁切。
    window.setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    window.setWindowTitle(QString());
    window.setAttribute(Qt::WA_ShowWithoutActivating, false);
    window.setObjectName(QStringLiteral("GWUpdateHelperRoot"));
    window.setStyleSheet(QStringLiteral(
        "QWidget#GWUpdateHelperRoot { background-color: rgb(255,252,253); border: 1px solid rgb(200,180,185); border-radius: 6px; }"
        "QLabel#GWUpdateHelperBody { color: rgb(74,61,63); font-size: 12px; }"));
    window.setFixedSize(460, 208);
    window.setCursor(Qt::BusyCursor);
    QApplication::setOverrideCursor(Qt::BusyCursor);

    auto *lay = new QVBoxLayout(&window);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    auto *header = new QWidget(&window);
    header->setFixedHeight(50);
    header->setStyleSheet(QStringLiteral("background-color: rgb(139, 74, 83);"));
    auto *ht = new QLabel(QStringLiteral("网关更新"), header);
    ht->setAlignment(Qt::AlignCenter);
    ht->setStyleSheet(QStringLiteral("color: white; font-weight: 600; font-size: 11pt; padding: 8px 4px 6px 4px;"));
    auto *hl = new QVBoxLayout(header);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->addWidget(ht);

    auto *body = new QLabel(
        QStringLiteral("正在更新网关，请稍候。\n\n完成后将自动启动新版本。\n请勿关机或强制结束任务。"));
    body->setObjectName(QStringLiteral("GWUpdateHelperBody"));
    body->setAlignment(Qt::AlignCenter);
    body->setWordWrap(true);
    body->setCursor(Qt::BusyCursor);
    body->setStyleSheet(QStringLiteral("padding: 18px 20px;"));

    lay->addWidget(header);
    lay->addWidget(body, 1);

    centerOnPrimary(&window);
    window.show();
    window.raise();
    window.activateWindow();
    app.processEvents();

    DWORD batExit = static_cast<DWORD>(-1);
    bool ran = shellExecuteBatHiddenWait(batPath, &batExit);
    if (!ran) {
        ran = shellExecuteViaUtf8LauncherBat(batPath, &batExit);
    }
    if (!ran) {
        ran = shellExecuteCmdExeCallBat(batPath, &batExit);
    }
    QApplication::restoreOverrideCursor();
    if (!ran) {
        return 3;
    }
    const bool ok = (batExit == 0);
    scheduleDeleteSelfExecutable();
    return ok ? 0 : 4;
}
