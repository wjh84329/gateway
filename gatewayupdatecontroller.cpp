#include "gatewayupdatecontroller.h"

#include "appconfig.h"
#include "appversion.h"
#include "applogger.h"
#include "gatewayapiclient.h"
#include "pageutils.h"

#include <QApplication>
#include <QScopeGuard>
#include <QByteArray>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QDialog>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QShortcut>
#include <QToolButton>
#include <QTimer>
#include <QVariant>
#include <QUrl>
#include <QWidget>
#include <QDateTime>
#include <QStringList>
#include <QElapsedTimer>
#include <QProgressDialog>
#include <QGuiApplication>
#include <QScreen>
#include <QVBoxLayout>

#ifdef Q_OS_WIN
#include <QProcess>
#include <QProcessEnvironment>
#endif

namespace {

constexpr int kNetworkTimeoutMs = 30000;
constexpr qint64 kMaxManifestBytes = 512 * 1024;
constexpr qint64 kMaxPackageBytes = 250LL * 1024 * 1024;
/// 主进程在启动更新助手后延迟退出：内嵌 UPDATE 从 qrc 拷到 %TEMP% 可能较慢；子进程静态 Qt 冷启动到首帧也有间隔。
/// 略延长以保持「正在应用在线更新」窗体与 UPDATE 窗口之间的视觉衔接（仍早于 bat 内 ping 后的 taskkill）。
constexpr int kQuitDelayMsAfterStartingUpdateHelper = 2000;

struct Manifest {
    QString latestVersion;
    QString downloadUrl;
    QString releaseNotes;
    QString sha256Hex;
};

QString NormalizeVersionToken(QString s)
{
    s = s.trimmed();
    if (s.startsWith(QLatin1Char('v'), Qt::CaseInsensitive) && s.size() > 1) {
        s.remove(0, 1);
    }
    return s.trimmed();
}

/// 返回值：a<b 为负，相等为 0，a>b 为正
int CompareSemverLike(const QString &aIn, const QString &bIn)
{
    const QString a = NormalizeVersionToken(aIn);
    const QString b = NormalizeVersionToken(bIn);
    const QStringList pa = a.split(QLatin1Char('.'), Qt::SkipEmptyParts);
    const QStringList pb = b.split(QLatin1Char('.'), Qt::SkipEmptyParts);
    const int n = qMax(pa.size(), pb.size());
    for (int i = 0; i < n; ++i) {
        const int va = (i < pa.size()) ? pa.at(i).toInt() : 0;
        const int vb = (i < pb.size()) ? pb.at(i).toInt() : 0;
        if (va != vb) {
            return va - vb;
        }
    }
    return 0;
}

bool ParseManifestJson(const QByteArray &json, Manifest *out, QString *errorMessage)
{
    if (!out) {
        return false;
    }
    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("清单 JSON 解析失败：%1").arg(pe.errorString());
        }
        return false;
    }
    const QJsonObject o = doc.object();
    out->latestVersion = o.value(QStringLiteral("latestVersion")).toString().trimmed();
    out->downloadUrl = o.value(QStringLiteral("downloadUrl")).toString().trimmed();
    out->releaseNotes = o.value(QStringLiteral("releaseNotes")).toString().trimmed();
    out->sha256Hex = o.value(QStringLiteral("sha256")).toString().trimmed();
    if (out->latestVersion.isEmpty() || out->downloadUrl.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("清单缺少 latestVersion 或 downloadUrl 字段");
        }
        return false;
    }
    if (QUrl(out->downloadUrl).scheme().isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("downloadUrl 不是合法 URL");
        }
        return false;
    }
    return true;
}

bool HttpGetBlocking(QWidget *parentForModal, const QUrl &url, qint64 maxBytes, QByteArray *outBody, QString *errorMessage)
{
    if (!outBody) {
        return false;
    }
    QNetworkAccessManager nam;
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("7xGateway/%1").arg(GatewayApp::versionString()));
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QVariant::fromValue(QNetworkRequest::NoLessSafeRedirectPolicy));
#endif
#if QT_VERSION >= QT_VERSION_CHECK(6, 4, 0)
    req.setTransferTimeout(kNetworkTimeoutMs);
#endif
    QNetworkReply *reply = nam.get(req);

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start(kNetworkTimeoutMs);
    // 勿用 ExcludeUserInputEvents：在 GUI 线程上会长时间不派发用户输入，界面像卡死，易被强制结束或误判为异常退出。
    loop.exec();

    if (reply->isRunning()) {
        reply->abort();
        reply->deleteLater();
        if (errorMessage) {
            *errorMessage = QStringLiteral("请求超时");
        }
        return false;
    }

    const auto sg = qScopeGuard([reply] { reply->deleteLater(); });
    Q_UNUSED(parentForModal);

    if (reply->error() != QNetworkReply::NoError) {
        if (errorMessage) {
            *errorMessage = reply->errorString();
        }
        return false;
    }

    *outBody = reply->readAll();
    if (outBody->size() > maxBytes) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("响应体积超过上限，已中止");
        }
        return false;
    }
    return true;
}

QString FormatDownloadedAmount(qint64 bytes)
{
    if (bytes <= 0) {
        return QStringLiteral("0 B");
    }
    if (bytes < 1024) {
        return QStringLiteral("%1 B").arg(bytes);
    }
    if (bytes < 1024LL * 1024LL) {
        return QStringLiteral("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    }
    return QStringLiteral("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 2);
}

bool DownloadToFileBlocking(QWidget *parent, const QUrl &url, const QString &destPath, QString *errorMessage)
{
    QFile f(destPath);
    if (!f.open(QIODevice::WriteOnly)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("无法写入：%1").arg(destPath);
        }
        return false;
    }

    QNetworkAccessManager nam;
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("7xGateway/%1").arg(GatewayApp::versionString()));
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QVariant::fromValue(QNetworkRequest::NoLessSafeRedirectPolicy));
#endif
#if QT_VERSION >= QT_VERSION_CHECK(6, 4, 0)
    req.setTransferTimeout(0);
#endif

    QNetworkReply *reply = nam.get(req);

    QDialog progressDlg(parent);
    QProgressBar *bar = nullptr;
    QLabel *pct = nullptr;
    QPushButton *cancelBtn = nullptr;
    QToolButton *closeBtn = nullptr;
    UiHelpers::SetupThemedDownloadProgressDialog(&progressDlg,
                                                   parent,
                                                   QStringLiteral("在线更新"),
                                                   QStringLiteral("在线更新"),
                                                   QStringLiteral("正在下载更新包…"),
                                                   bar,
                                                   pct,
                                                   cancelBtn,
                                                   closeBtn);
    progressDlg.setWindowModality(Qt::WindowModal);

    bool canceled = false;
    auto requestAbort = [&]() {
        if (!canceled) {
            canceled = true;
            reply->abort();
        }
    };
    QObject::connect(cancelBtn, &QPushButton::clicked, &progressDlg, requestAbort);
    QObject::connect(closeBtn, &QToolButton::clicked, &progressDlg, requestAbort);
    auto *escProg = new QShortcut(QKeySequence(Qt::Key_Escape), &progressDlg);
    QObject::connect(escProg, &QShortcut::activated, &progressDlg, requestAbort);

    QObject::connect(reply, &QNetworkReply::downloadProgress, &progressDlg, [&](qint64 received, qint64 total) {
        if (!bar || !pct) {
            return;
        }
        bool clOk = false;
        const qint64 cl = reply->header(QNetworkRequest::ContentLengthHeader).toLongLong(&clOk);
        qint64 effTotal = 0;
        if (total > 0 && clOk && cl > 0) {
            // 常见：信号里的 total 偏大（代理/压缩层），Content-Length 更接近真实 zip 体积
            effTotal = qMin(total, cl);
        } else if (clOk && cl > 0) {
            effTotal = cl;
        } else if (total > 0) {
            effTotal = total;
        }
        // 已下载超过声称总长时，以实际接收为准，避免长期卡在 90%+ 不动
        if (effTotal > 0 && received > effTotal) {
            effTotal = received;
        }

        if (effTotal > 0) {
            bar->setRange(0, 1000);
            bar->setMinimum(0);
            const double frac = qMin(1.0, static_cast<double>(received) / static_cast<double>(effTotal));
            bar->setMaximum(1000);
            bar->setValue(static_cast<int>(qRound(frac * 1000.0)));
            pct->setText(QStringLiteral("%1%").arg(QString::number(frac * 100.0, 'f', 1)));
        } else {
            // 无可靠总长：不要用 250MB 上限伪算百分比（小文件会严重偏低）
            bar->setRange(0, 0);
            bar->setMinimum(0);
            bar->setMaximum(0);
            if (received <= 0) {
                pct->setText(QStringLiteral("连接中…"));
            } else {
                pct->setText(QStringLiteral("已下载 %1（总大小未知）").arg(FormatDownloadedAmount(received)));
            }
        }
    });

    qint64 receivedTotal = 0;
    bool writeFailed = false;
    bool oversizeAbort = false;
    progressDlg.show();
    QApplication::processEvents();
    progressDlg.adjustSize();
    UiHelpers::CenterDialogOnWindow(&progressDlg, parent);

    auto appendChunk = [&](const QByteArray &chunk) -> bool {
        if (chunk.isEmpty()) {
            return true;
        }
        const qint64 nextTotal = receivedTotal + chunk.size();
        if (nextTotal > kMaxPackageBytes) {
            oversizeAbort = true;
            return false;
        }
        const qint64 w = f.write(chunk);
        if (w != chunk.size()) {
            writeFailed = true;
            return false;
        }
        receivedTotal = nextTotal;
        return true;
    };

    QObject::connect(reply, &QNetworkReply::readyRead, &progressDlg, [&]() {
        const QByteArray chunk = reply->readAll();
        if (!appendChunk(chunk)) {
            reply->abort();
        }
    });

    QEventLoop loop;
    bool finishHandled = false;
    auto onFinished = [&]() {
        if (finishHandled) {
            return;
        }
        finishHandled = true;
        const QByteArray tail = reply->readAll();
        if (!appendChunk(tail)) {
            reply->abort();
        }
        loop.quit();
    };
    QObject::connect(reply, &QNetworkReply::finished, &progressDlg, onFinished);
    if (reply->isFinished()) {
        onFinished();
    }
    if (!finishHandled) {
        loop.exec();
    }

    if (!canceled && reply->error() == QNetworkReply::NoError && !writeFailed && bar && pct) {
        bar->setRange(0, 100);
        bar->setValue(100);
        pct->setText(QStringLiteral("100%"));
        QApplication::processEvents();
    }

    progressDlg.hide();

    f.flush();
    f.close();

    if (writeFailed) {
        QFile::remove(destPath);
        if (errorMessage) {
            *errorMessage = QStringLiteral("写入安装包失败（磁盘空间不足、被占用或无写入权限）");
        }
        reply->deleteLater();
        return false;
    }

    if (canceled) {
        QFile::remove(destPath);
        if (errorMessage) {
            *errorMessage = QStringLiteral("已取消下载");
        }
        reply->deleteLater();
        return false;
    }
    if (oversizeAbort || receivedTotal > kMaxPackageBytes) {
        QFile::remove(destPath);
        if (errorMessage) {
            *errorMessage = QStringLiteral("安装包超过大小上限");
        }
        reply->deleteLater();
        return false;
    }

    if (reply->error() != QNetworkReply::NoError) {
        QFile::remove(destPath);
        if (errorMessage) {
            if (reply->error() == QNetworkReply::OperationCanceledError) {
                *errorMessage = QStringLiteral("下载被中断或连接已断开，请检查网络后重试。");
            } else {
                *errorMessage = reply->errorString();
            }
        }
        reply->deleteLater();
        return false;
    }

    reply->deleteLater();
    if (!QFileInfo::exists(destPath) || QFileInfo(destPath).size() <= 0) {
        QFile::remove(destPath);
        if (errorMessage) {
            *errorMessage = QStringLiteral("下载未完成或安装包为空");
        }
        return false;
    }
    return true;
}

bool IsZipLocalFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QByteArray h = f.read(4);
    return h.size() >= 4 && static_cast<unsigned char>(h[0]) == 0x50 && static_cast<unsigned char>(h[1]) == 0x4B;
}

bool IsPeExecutableLocalFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QByteArray h = f.read(2);
    return h.size() >= 2 && static_cast<unsigned char>(h[0]) == 0x4D && static_cast<unsigned char>(h[1]) == 0x5A;
}

#ifdef Q_OS_WIN
/// QProcess::waitForFinished 在 GUI 线程会长时间不处理事件，易被判定为「未响应」；解压可能达数分钟，须用事件循环等待。
/// 子进程若大量写 stdout/stderr 而父进程不读，管道会塞满导致子进程永久阻塞；因此在等待期间周期性排空（可选写入 mergedStdOutErr 供失败时展示）。
bool WaitForQProcessFinishedKeepUiAlive(QProcess &proc, int timeoutMs, QByteArray *mergedStdOutErr = nullptr)
{
    const auto drainOnce = [&proc, mergedStdOutErr]() {
        const QByteArray out = proc.readAllStandardOutput();
        const QByteArray err = proc.readAllStandardError();
        if (mergedStdOutErr) {
            mergedStdOutErr->append(out);
            mergedStdOutErr->append(err);
        }
    };

    if (proc.state() == QProcess::NotRunning) {
        drainOnce();
        return true;
    }

    bool timedOut = false;
    QEventLoop loop;
    QTimer timer;
    QTimer pipeDrainTimer;
    pipeDrainTimer.setInterval(50);
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, [&proc, &timedOut]() {
        timedOut = true;
        if (proc.state() != QProcess::NotRunning) {
            proc.kill();
        }
    });
    QObject::connect(&pipeDrainTimer, &QTimer::timeout, &loop, drainOnce);
    QObject::connect(&proc,
                     static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
                     &loop,
                     &QEventLoop::quit);
#if QT_VERSION >= QT_VERSION_CHECK(5, 6, 0)
    QObject::connect(&proc, &QProcess::errorOccurred, &loop, &QEventLoop::quit);
#else
    QObject::connect(&proc,
                     static_cast<void (QProcess::*)(QProcess::ProcessError)>(&QProcess::error),
                     &loop,
                     &QEventLoop::quit);
#endif

    pipeDrainTimer.start();
    timer.start(timeoutMs);
    loop.exec();
    pipeDrainTimer.stop();
    timer.stop();
    drainOnce();

    if (proc.state() != QProcess::NotRunning) {
        proc.kill();
        QElapsedTimer drain;
        drain.start();
        while (proc.state() != QProcess::NotRunning && drain.elapsed() < 3000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            drainOnce();
        }
    }
    return !timedOut;
}

bool WriteUtf8FileWithBom(const QString &path, const QByteArray &utf8Body, QString *errorMessage)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("无法写入：%1").arg(path);
        }
        return false;
    }
    static const char kBom[] = "\xEF\xBB\xBF";
    f.write(kBom, 3);
    f.write(utf8Body);
    f.close();
    return true;
}

QString ResolveWindowsPowerShellExePath()
{
    const QString sysRoot = QProcessEnvironment::systemEnvironment().value(QStringLiteral("SystemRoot"),
                                                                             QStringLiteral("C:\\Windows"));
    const QStringList cands = {
        QDir(sysRoot).filePath(QStringLiteral("Sysnative/WindowsPowerShell/v1.0/powershell.exe")),
        QDir(sysRoot).filePath(QStringLiteral("System32/WindowsPowerShell/v1.0/powershell.exe")),
    };
    for (const QString &p : cands) {
        if (QFileInfo::exists(p)) {
            return QDir::toNativeSeparators(p);
        }
    }
    return QStringLiteral("powershell.exe");
}

/// 主进程退出前短暂显示，填补从内嵌资源释放 UPDATE、子进程冷启动时的「空白期」。失败时调用方应 deleteLater。
QWidget *ShowGatewayUpdateHandoffSplash(QWidget *anchorWidget)
{
    auto *w = new QWidget(nullptr);
    // 勿用 Qt::Tool：在 Windows 上仍可能显示窄标题栏并套用应用显示名，字顶易被裁切。
    w->setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    w->setWindowTitle(QString());
    w->setAttribute(Qt::WA_ShowWithoutActivating, false);
    w->setObjectName(QStringLiteral("GWUpdateHandoff"));
    w->setStyleSheet(QStringLiteral(
        "QWidget#GWUpdateHandoff { background-color: rgb(255,252,253); border: 1px solid rgb(200,180,185); border-radius: 6px; }"
        "QLabel#GWUpdateHandoffBody { color: rgb(74,61,63); font-size: 12px; }"));
    w->setFixedSize(460, 158);
    w->setCursor(Qt::BusyCursor);

    auto *lay = new QVBoxLayout(w);
    lay->setContentsMargins(18, 16, 18, 16);
    lay->setSpacing(10);
    auto *title = new QLabel(QStringLiteral("正在应用在线更新"));
    title->setStyleSheet(QStringLiteral("font-weight: 600; font-size: 14px; color: rgb(139,74,83); padding-top: 2px;"));
    auto *body = new QLabel(
        QStringLiteral("本程序即将关闭以完成文件替换。若有一两秒桌面似乎没有反应，属于正常现象。\n\n"
                       "随后将出现「网关更新」窗口，请勿关机或强制结束任务。"));
    body->setObjectName(QStringLiteral("GWUpdateHandoffBody"));
    body->setWordWrap(true);
    lay->addWidget(title);
    lay->addWidget(body);
    title->setCursor(Qt::BusyCursor);
    body->setCursor(Qt::BusyCursor);

    QScreen *scr = nullptr;
    if (anchorWidget) {
        const QRect g = anchorWidget->frameGeometry();
        scr = QGuiApplication::screenAt(g.center());
    }
    if (!scr) {
        scr = QGuiApplication::primaryScreen();
    }
    if (scr) {
        const QRect ag = scr->availableGeometry();
        w->adjustSize();
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

    w->show();
    w->raise();
    w->activateWindow();
    QApplication::processEvents(QEventLoop::AllEvents);
    return w;
}

/// 独立进程：居中「更新中」提示，并在无控制台窗口的情况下执行 gateway_apply_update.bat。
bool WriteGatewayUpdateSplashScript(const QString &ps1Path, QString *errorMessage)
{
    const QByteArray body = QStringLiteral(
                               "param(\r\n"
                               "  [Parameter(Mandatory=$true)][string]$BatPath\r\n"
                               ")\r\n"
                               "$ErrorActionPreference = 'SilentlyContinue'\r\n"
                               "if (-not (Test-Path -LiteralPath $BatPath)) { exit 1 }\r\n"
                               "$script:started = $false\r\n"
                               "$timer = $null\r\n"
                               "function Start-BatHidden {\r\n"
                               "  $psi2 = New-Object System.Diagnostics.ProcessStartInfo\r\n"
                               "  $psi2.FileName = 'cmd.exe'\r\n"
                               "  $q2 = [char]34\r\n"
                               "  $psi2.Arguments = '/c ' + $q2 + $BatPath + $q2\r\n"
                               "  $psi2.UseShellExecute = $false\r\n"
                               "  $psi2.CreateNoWindow = $true\r\n"
                               "  $psi2.WindowStyle = [System.Diagnostics.ProcessWindowStyle]::Hidden\r\n"
                               "  $p2 = New-Object System.Diagnostics.Process\r\n"
                               "  $p2.StartInfo = $psi2\r\n"
                               "  [void]$p2.Start()\r\n"
                               "}\r\n"
                               "try {\r\n"
                               "  Add-Type -AssemblyName System.Drawing\r\n"
                               "  Add-Type -AssemblyName System.Windows.Forms\r\n"
                               "  [System.Windows.Forms.Application]::EnableVisualStyles()\r\n"
                               "  $form = New-Object System.Windows.Forms.Form\r\n"
                               "  $form.Text = '网关更新'\r\n"
                               "  $form.FormBorderStyle = [System.Windows.Forms.FormBorderStyle]::None\r\n"
                               "  $form.StartPosition = [System.Windows.Forms.FormStartPosition]::CenterScreen\r\n"
                               "  $form.TopMost = $true\r\n"
                               "  $form.Size = New-Object System.Drawing.Size(460, 200)\r\n"
                               "  $form.BackColor = [System.Drawing.Color]::FromArgb(255, 255, 252, 253)\r\n"
                               "  $form.ShowInTaskbar = $true\r\n"
                               "  $header = New-Object System.Windows.Forms.Panel\r\n"
                               "  $header.Height = 46\r\n"
                               "  $header.Dock = [System.Windows.Forms.DockStyle]::Top\r\n"
                               "  $header.BackColor = [System.Drawing.Color]::FromArgb(255, 139, 74, 83)\r\n"
                               "  $ht = New-Object System.Windows.Forms.Label\r\n"
                               "  $ht.Text = '网关更新'\r\n"
                               "  $ht.ForeColor = [System.Drawing.Color]::White\r\n"
                               "  try {\r\n"
                               "    $ht.Font = New-Object System.Drawing.Font('Tahoma', 11, [System.Drawing.FontStyle]::Bold)\r\n"
                               "  } catch {\r\n"
                               "    $ht.Font = New-Object System.Drawing.Font('Microsoft Sans Serif', 11, [System.Drawing.FontStyle]::Bold)\r\n"
                               "  }\r\n"
                               "  $ht.Dock = [System.Windows.Forms.DockStyle]::Fill\r\n"
                               "  $ht.TextAlign = [System.Drawing.ContentAlignment]::MiddleCenter\r\n"
                               "  $header.Controls.Add($ht)\r\n"
                               "  $label = New-Object System.Windows.Forms.Label\r\n"
                               "  $label.Dock = [System.Windows.Forms.DockStyle]::Fill\r\n"
                               "  $label.TextAlign = [System.Drawing.ContentAlignment]::MiddleCenter\r\n"
                               "  try {\r\n"
                               "    $label.Font = New-Object System.Drawing.Font('Tahoma', 10)\r\n"
                               "  } catch {\r\n"
                               "    $label.Font = New-Object System.Drawing.Font('Microsoft Sans Serif', 10)\r\n"
                               "  }\r\n"
                               "  $label.ForeColor = [System.Drawing.Color]::FromArgb(255, 74, 61, 63)\r\n"
                               "  $label.Padding = New-Object System.Windows.Forms.Padding(20, 18, 20, 18)\r\n"
                               "  $label.Text = \"正在更新网关，请稍候。`r`n`r`n完成后将自动启动新版本。`r`n请勿关机或强制结束任务。\"\r\n"
                               "  $form.Controls.Add($label)\r\n"
                               "  $form.Controls.Add($header)\r\n"
                               "  $form.Show()\r\n"
                               "  [System.Windows.Forms.Application]::DoEvents()\r\n"
                               "  $psi = New-Object System.Diagnostics.ProcessStartInfo\r\n"
                               "  $psi.FileName = 'cmd.exe'\r\n"
                               "  $q = [char]34\r\n"
                               "  $psi.Arguments = '/c ' + $q + $BatPath + $q\r\n"
                               "  $psi.UseShellExecute = $false\r\n"
                               "  $psi.CreateNoWindow = $true\r\n"
                               "  $psi.WindowStyle = [System.Diagnostics.ProcessWindowStyle]::Hidden\r\n"
                               "  $proc = New-Object System.Diagnostics.Process\r\n"
                               "  $proc.StartInfo = $psi\r\n"
                               "  $timer = New-Object System.Windows.Forms.Timer\r\n"
                               "  $timer.Interval = 40\r\n"
                               "  $timer.Add_Tick({\r\n"
                               "    if (-not $script:started) {\r\n"
                               "      $script:started = $true\r\n"
                               "      [void]$proc.Start()\r\n"
                               "      return\r\n"
                               "    }\r\n"
                               "    if ($proc.HasExited) {\r\n"
                               "      $timer.Stop()\r\n"
                               "      $form.Close()\r\n"
                               "    }\r\n"
                               "  })\r\n"
                               "  $timer.Start()\r\n"
                               "  [System.Windows.Forms.Application]::Run($form)\r\n"
                               "} catch {\r\n"
                               "  if (-not $script:started) { Start-BatHidden }\r\n"
                               "}\r\n"
                               "finally {\r\n"
                               "  if ($null -ne $timer) { $timer.Dispose() }\r\n"
                               "  Remove-Item -LiteralPath $MyInvocation.MyCommand.Path -Force -ErrorAction SilentlyContinue\r\n"
                               "}\r\n")
                               .toUtf8();
    return WriteUtf8FileWithBom(ps1Path, body, errorMessage);
}

/// 使用系统 tar 解压 zip（在 7za/7z 之后作为回退；老系统上常不存在 tar.exe）。
bool TryExpandZipWithSystemTar(const QString &zipPath, const QString &destDir, QString *errorMessage)
{
    static const char *const kTarPaths[] = {
        "C:/Windows/System32/tar.exe",
        "C:/Windows/Sysnative/tar.exe",
    };
    QString tarExe;
    for (const char *p : kTarPaths) {
        const QString cand = QString::fromUtf8(p);
        if (QFileInfo::exists(cand)) {
            tarExe = cand;
            break;
        }
    }
    if (tarExe.isEmpty()) {
        tarExe = QStandardPaths::findExecutable(QStringLiteral("tar.exe"));
    }
    if (tarExe.isEmpty()) {
        return false;
    }

    QProcess p;
    p.setProgram(tarExe);
    p.setArguments({QStringLiteral("-xf"),
                    QDir::toNativeSeparators(zipPath),
                    QStringLiteral("-C"),
                    QDir::toNativeSeparators(destDir)});
    p.setProcessChannelMode(QProcess::MergedChannels);
    QByteArray procLog;
    p.start();
    if (!WaitForQProcessFinishedKeepUiAlive(p, 180000, &procLog)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("tar 解压超时");
        }
        return false;
    }
    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("tar：%1").arg(QString::fromUtf8(procLog).trimmed());
        }
        return false;
    }
    return true;
}

/// 使用 7-Zip 控制台（7za/7z/7zr）解压 zip。优先 7za（精简 7zr 常不含 zip 解码器）。
/// 磁盘：程序目录 → tools 子目录 → PATH → 常见安装路径；再尝试资源 :/tools/7za.exe 或 :/tools/7zr.exe 释放到临时目录。
bool TryExpandZipWith7ZipConsole(const QString &zipPath, const QString &destDir, QString *materializedTemp7zrOut, QString *errorMessage)
{
    if (materializedTemp7zrOut) {
        materializedTemp7zrOut->clear();
    }

    const QString appDir = QCoreApplication::applicationDirPath();
    QStringList diskCandidates;
    diskCandidates << QDir(appDir).filePath(QStringLiteral("7za.exe"))
                   << QDir(appDir).filePath(QStringLiteral("tools/7za.exe"))
                   << QDir(appDir).filePath(QStringLiteral("7z.exe"))
                   << QDir(appDir).filePath(QStringLiteral("tools/7z.exe"))
                   << QStringLiteral("C:/Program Files/7-Zip/7z.exe")
                   << QStringLiteral("C:/Program Files (x86)/7-Zip/7z.exe")
                   << QDir(appDir).filePath(QStringLiteral("7zr.exe"))
                   << QDir(appDir).filePath(QStringLiteral("tools/7zr.exe"));

    QString sevenExe;
    for (const QString &c : diskCandidates) {
        if (QFileInfo::exists(c)) {
            sevenExe = QDir::toNativeSeparators(c);
            break;
        }
    }
    if (sevenExe.isEmpty()) {
        const QString fromPath = QStandardPaths::findExecutable(QStringLiteral("7za.exe"));
        if (!fromPath.isEmpty()) {
            sevenExe = QDir::toNativeSeparators(fromPath);
        }
    }
    if (sevenExe.isEmpty()) {
        const QString fromPath = QStandardPaths::findExecutable(QStringLiteral("7z.exe"));
        if (!fromPath.isEmpty()) {
            sevenExe = QDir::toNativeSeparators(fromPath);
        }
    }
    if (sevenExe.isEmpty()) {
        const QString fromPath = QStandardPaths::findExecutable(QStringLiteral("7zr.exe"));
        if (!fromPath.isEmpty()) {
            sevenExe = QDir::toNativeSeparators(fromPath);
        }
    }

    static const struct {
        const char *qrcPath;
        const char *tempFilePattern;
    } kEmbedded[] = {
        {":/tools/7za.exe", "gateway_7za_%1.exe"},
        {":/tools/7zr.exe", "gateway_7zr_%1.exe"},
    };

    if (sevenExe.isEmpty()) {
        for (const auto &emb : kEmbedded) {
            const QString qrc = QString::fromUtf8(emb.qrcPath);
            if (!QFile::exists(qrc)) {
                continue;
            }
            const QString tmp = QDir(QDir::tempPath()).filePath(
                QString::fromUtf8(emb.tempFilePattern).arg(QCoreApplication::applicationPid()));
            QFile::remove(tmp);
            if (!QFile::copy(qrc, tmp)) {
                if (errorMessage) {
                    *errorMessage = QStringLiteral("无法释放内置 %1").arg(QFileInfo(qrc).fileName());
                }
                return false;
            }
            QFile::setPermissions(tmp, QFile::ReadUser | QFile::WriteUser | QFile::ExeUser);
            sevenExe = QDir::toNativeSeparators(tmp);
            if (materializedTemp7zrOut) {
                *materializedTemp7zrOut = tmp;
            }
            break;
        }
    }

    if (sevenExe.isEmpty()) {
        return false;
    }

    const QString exeLabel = QFileInfo(sevenExe).fileName();

    QString outRoot = QDir::toNativeSeparators(QFileInfo(destDir).absoluteFilePath());
    if (!outRoot.endsWith(QLatin1Char('\\'))) {
        outRoot.append(QLatin1Char('\\'));
    }
    const QString outArg = QStringLiteral("-o") + outRoot;

    QProcess p;
    p.setProgram(sevenExe);
    // -bb0：尽量少打日志，降低管道阻塞风险；7-Zip 退出码 1 表示有警告但常仍解压成功。
    p.setArguments({QStringLiteral("x"),
                    QStringLiteral("-bd"),
                    QStringLiteral("-bb0"),
                    QStringLiteral("-y"),
                    outArg,
                    QDir::toNativeSeparators(zipPath)});
    p.setProcessChannelMode(QProcess::MergedChannels);
    QByteArray procLog;
    p.start();
    if (!WaitForQProcessFinishedKeepUiAlive(p, 180000, &procLog)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("%1 解压超时").arg(exeLabel);
        }
        return false;
    }
    if (p.exitStatus() != QProcess::NormalExit) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("%1：%2").arg(exeLabel, QString::fromUtf8(procLog).trimmed());
        }
        return false;
    }
    const int exitCode = p.exitCode();
    if (exitCode != 0 && exitCode != 1) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("%1（exit=%2）：%3")
                                .arg(exeLabel)
                                .arg(exitCode)
                                .arg(QString::fromUtf8(procLog).trimmed());
        }
        return false;
    }
    return true;
}

void LogUpdateExtractDirectorySnapshot(const QString &extractRoot)
{
    QDir d(extractRoot);
    if (!d.exists()) {
        AppLogger::WriteLog(QStringLiteral("在线更新诊断：解压目录不存在：%1").arg(extractRoot));
        return;
    }
    const QStringList all = d.entryList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden);
    QStringList show = all;
    const int cap = 48;
    if (show.size() > cap) {
        show = show.mid(0, cap);
        show.append(QStringLiteral("…"));
    }
    AppLogger::WriteLog(QStringLiteral("在线更新诊断：%1 顶层项数量=%2，前列：%3")
                            .arg(extractRoot)
                            .arg(all.size())
                            .arg(show.join(QLatin1String(", "))));
}

/// 解压 zip（管理端 wg 包为 zip，内含主程序 exe 与 Libs）。
/// 顺序：7za/7z/7zr（同目录、PATH、常见安装路径或资源内嵌）→ 系统 tar → PowerShell。Qt5 静态下避免再链 libzip/zlib 与 Qt 内置 zlib 冲突。
bool ExpandUpdateZipWindows(const QString &zipPath, const QString &destDir, QString *errorMessage, QWidget *busyParent)
{
    QString mat7zTemp;
    const QScopeGuard removeMaterialized7zr([&mat7zTemp]() {
        if (!mat7zTemp.isEmpty()) {
            QFile::remove(mat7zTemp);
        }
    });

    auto resetDest = [&]() {
        if (QDir(destDir).exists()) {
            QDir(destDir).removeRecursively();
        }
        QDir().mkpath(destDir);
    };

    QScopedPointer<QProgressDialog> unzipBusy;
    if (busyParent) {
        unzipBusy.reset(new QProgressDialog(QStringLiteral("正在解压安装包，请稍候…"),
                                            QString(),
                                            0,
                                            0,
                                            busyParent));
        unzipBusy->setWindowTitle(QStringLiteral("在线更新"));
        unzipBusy->setWindowModality(Qt::WindowModal);
        unzipBusy->setMinimumDuration(0);
        unzipBusy->setCancelButton(nullptr);
        unzipBusy->show();
        QApplication::processEvents();
    }

    QString stepErr;
    resetDest();
    if (TryExpandZipWith7ZipConsole(zipPath, destDir, &mat7zTemp, &stepErr)) {
        return true;
    }
    if (!stepErr.isEmpty() && errorMessage) {
        *errorMessage = stepErr;
    }

    resetDest();
    stepErr.clear();
    if (TryExpandZipWithSystemTar(zipPath, destDir, &stepErr)) {
        return true;
    }
    if (!stepErr.isEmpty() && errorMessage) {
        *errorMessage = stepErr;
    }

    resetDest();
    QApplication::processEvents();

    const QString ps1 = QDir(QCoreApplication::applicationDirPath())
                            .filePath(QStringLiteral("_gateway_expand_update_temp.ps1"));
    {
        QFile wf(ps1);
        if (!wf.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("无法写入解压脚本");
            }
            return false;
        }
        wf.write(QStringLiteral(
                     "param(\r\n"
                     "  [Parameter(Mandatory=$true)][string]$ZipPath,\r\n"
                     "  [Parameter(Mandatory=$true)][string]$DestPath\r\n"
                     ")\r\n"
                     "$ErrorActionPreference = 'Stop'\r\n"
                     "if (Test-Path -LiteralPath $DestPath) { Remove-Item -LiteralPath $DestPath -Recurse -Force }\r\n"
                     "New-Item -ItemType Directory -Path $DestPath -Force | Out-Null\r\n"
                     "$zipFull = (Get-Item -LiteralPath $ZipPath).FullName\r\n"
                     "$destFull = (Get-Item -LiteralPath $DestPath).FullName\r\n"
                     "$psMaj = 2\r\n"
                     "if ($PSVersionTable -and $PSVersionTable.PSVersion) { $psMaj = $PSVersionTable.PSVersion.Major }\r\n"
                     "if ($psMaj -ge 5) {\r\n"
                     "  Expand-Archive -LiteralPath $zipFull -DestinationPath $destFull -Force\r\n"
                     "} else {\r\n"
                     "  $expanded = $false\r\n"
                     "  $ErrorActionPreference = 'SilentlyContinue'\r\n"
                     "  try {\r\n"
                     "    Add-Type -AssemblyName System.IO.Compression.FileSystem -ErrorAction Stop | Out-Null\r\n"
                     "    [System.IO.Compression.ZipFile]::ExtractToDirectory($zipFull, $destFull)\r\n"
                     "    $expanded = $true\r\n"
                     "  } catch { }\r\n"
                     "  if (-not $expanded) {\r\n"
                     "    $shell = New-Object -ComObject Shell.Application\r\n"
                     "    $z = $shell.Namespace($zipFull)\r\n"
                     "    if ($null -eq $z) { throw 'Shell.Application: cannot open zip' }\r\n"
                     "    $d = $shell.Namespace($destFull)\r\n"
                     "    if ($null -eq $d) { throw 'Shell.Application: cannot open dest' }\r\n"
                     "    foreach ($it in @($z.Items())) { $d.CopyHere($it, 20) }\r\n"
                     "  }\r\n"
                     "  $ErrorActionPreference = 'Stop'\r\n"
                     "}\r\n")
                     .toUtf8());
        wf.close();
    }

    QProcess p;
    p.setProgram(ResolveWindowsPowerShellExePath());
    p.setArguments({QStringLiteral("-NoProfile"),
                    QStringLiteral("-ExecutionPolicy"),
                    QStringLiteral("Bypass"),
                    QStringLiteral("-File"),
                    ps1,
                    QStringLiteral("-ZipPath"),
                    zipPath,
                    QStringLiteral("-DestPath"),
                    destDir});
    p.setProcessChannelMode(QProcess::MergedChannels);
    QByteArray procLog;
    p.start();
    if (!WaitForQProcessFinishedKeepUiAlive(p, 180000, &procLog)) {
        QFile::remove(ps1);
        if (errorMessage) {
            *errorMessage = QStringLiteral("解压超时");
        }
        return false;
    }
    QFile::remove(ps1);
    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("解压失败：%1").arg(QString::fromUtf8(procLog).trimmed());
        }
        return false;
    }
    return true;
}

QString FindLibsDirWithSettingRecursive(const QString &root, int maxDepth)
{
    if (maxDepth < 0) {
        return {};
    }
    const QDir rootDir(root);
    if (!rootDir.exists()) {
        return {};
    }
    const QDir libs(rootDir.filePath(QStringLiteral("Libs")));
    if (libs.exists() && QFileInfo::exists(libs.filePath(QStringLiteral("setting.config")))) {
        return QDir::cleanPath(libs.absolutePath());
    }
    const auto entries = rootDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden);
    for (const QFileInfo &fi : entries) {
        const QString hit = FindLibsDirWithSettingRecursive(fi.absoluteFilePath(), maxDepth - 1);
        if (!hit.isEmpty()) {
            return hit;
        }
    }
    return {};
}

/// 按目录名查找 Libs（大小写不敏感）；发布 zip 常含 Libs 但无 setting.config，不能仅靠 FindLibsDirWithSettingRecursive。
QString FindLibsDirByNameRecursive(const QString &root, int maxDepth)
{
    if (maxDepth < 0) {
        return {};
    }
    const QDir rootDir(root);
    if (!rootDir.exists()) {
        return {};
    }
    const QFileInfoList entries = rootDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden);
    for (const QFileInfo &fi : entries) {
        if (fi.fileName().compare(QStringLiteral("Libs"), Qt::CaseInsensitive) == 0) {
            return QDir::cleanPath(fi.absoluteFilePath());
        }
    }
    for (const QFileInfo &fi : entries) {
        const QString hit = FindLibsDirByNameRecursive(fi.absoluteFilePath(), maxDepth - 1);
        if (!hit.isEmpty()) {
            return hit;
        }
    }
    return {};
}

int RelativePathSegmentCount(const QString &extractRootClean, const QString &filePathClean)
{
    QString rel = QDir(extractRootClean).relativeFilePath(filePathClean);
    rel.replace(QLatin1Char('\\'), QLatin1Char('/'));
    if (rel.startsWith(QLatin1String("../"))) {
        return 9999;
    }
    const QStringList parts = rel.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    return parts.isEmpty() ? 9999 : parts.size();
}

void CollectExesRecursive(const QString &dir, int maxDepth, QStringList *out)
{
    if (maxDepth < 0 || out == nullptr) {
        return;
    }
    QDir d(dir);
    if (!d.exists()) {
        return;
    }
    const QFileInfoList files = d.entryInfoList(QDir::Files | QDir::NoSymLinks | QDir::Hidden);
    for (const QFileInfo &fi : files) {
        if (!fi.fileName().endsWith(QLatin1String(".exe"), Qt::CaseInsensitive)) {
            continue;
        }
        out->append(QDir::cleanPath(fi.absoluteFilePath()));
    }
    if (maxDepth == 0) {
        return;
    }
    const QFileInfoList subs = d.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden);
    for (const QFileInfo &fi : subs) {
        CollectExesRecursive(fi.absoluteFilePath(), maxDepth - 1, out);
    }
}

/// 多候选时：优先 cs.exe / 支付网关.exe，其次与当前进程同名，否则取体积最大者（通常为主程序）。
QString PickBestExeAmong(const QStringList &candidates)
{
    if (candidates.isEmpty()) {
        return {};
    }
    if (candidates.size() == 1) {
        return candidates.constFirst();
    }
    const QString runningLower = QFileInfo(QCoreApplication::applicationFilePath()).fileName().toLower();
    for (const QString &p : candidates) {
        if (QFileInfo(p).fileName().compare(QStringLiteral("cs.exe"), Qt::CaseInsensitive) == 0) {
            return p;
        }
    }
    for (const QString &p : candidates) {
        if (QFileInfo(p).fileName().compare(QStringLiteral("支付网关.exe"), Qt::CaseInsensitive) == 0) {
            return p;
        }
    }
    for (const QString &p : candidates) {
        if (QFileInfo(p).fileName().compare(runningLower, Qt::CaseInsensitive) == 0) {
            return p;
        }
    }
    QString best;
    qint64 bestSize = -1;
    for (const QString &p : candidates) {
        const qint64 sz = QFileInfo(p).size();
        if (sz > bestSize) {
            bestSize = sz;
            best = p;
        }
    }
    return best;
}

/// 不强制包内主程序名：优先「与 Libs 同目录」的 exe；否则取目录树中最浅层 exe，多候选时由 PickBestExeAmong 决定。
QString FindMainExeInUpdatePackage(const QString &extractRoot, int maxDepth)
{
    const QString rootClean = QDir::cleanPath(extractRoot);

    QString libsDir = FindLibsDirWithSettingRecursive(extractRoot, maxDepth);
    if (libsDir.isEmpty()) {
        libsDir = FindLibsDirByNameRecursive(extractRoot, maxDepth);
    }
    if (libsDir.isEmpty()) {
        const QDir tryLibs(QDir(extractRoot).filePath(QStringLiteral("Libs")));
        if (tryLibs.exists()) {
            libsDir = QDir::cleanPath(tryLibs.absolutePath());
        }
    }
    // QFileInfo(libsDir).absolutePath() 已是「与 Libs 同级」的安装根目录，不可再 cdUp（否则会到上一级找 exe，漏掉主程序+Libs 标准结构）
    if (!libsDir.isEmpty()) {
        const QString installRootPath = QFileInfo(libsDir).absolutePath();
        QDir installRoot(installRootPath);
        if (installRoot.exists()) {
            QStringList beside;
            const QFileInfoList exes = installRoot.entryInfoList(
                QStringList() << QStringLiteral("*.exe"), QDir::Files | QDir::NoSymLinks | QDir::Hidden);
            for (const QFileInfo &fi : exes) {
                beside.append(QDir::cleanPath(fi.absoluteFilePath()));
            }
            const QString picked = PickBestExeAmong(beside);
            if (!picked.isEmpty()) {
                return picked;
            }
        }
    }

    QStringList all;
    CollectExesRecursive(extractRoot, maxDepth, &all);
    if (all.isEmpty()) {
        return {};
    }
    int minSeg = 9999;
    for (const QString &p : all) {
        const int seg = RelativePathSegmentCount(rootClean, QDir::cleanPath(p));
        if (seg < minSeg) {
            minSeg = seg;
        }
    }
    QStringList shallow;
    for (const QString &p : all) {
        if (RelativePathSegmentCount(rootClean, QDir::cleanPath(p)) == minSeg) {
            shallow.append(p);
        }
    }
    return PickBestExeAmong(shallow);
}

#endif // Q_OS_WIN

bool VerifyFileSha256(const QString &path, const QString &expectHexLowerOrMixed, QString *errorMessage)
{
    if (expectHexLowerOrMixed.trimmed().isEmpty()) {
        return true;
    }
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("无法读取已下载文件以校验");
        }
        return false;
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    int chunkIdx = 0;
    while (!f.atEnd()) {
        const QByteArray chunk = f.read(1024 * 1024);
        hash.addData(chunk);
        ++chunkIdx;
        if ((chunkIdx & 7) == 0 || f.atEnd()) {
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 50);
        }
    }
    f.close();
    const QString got = QString::fromLatin1(hash.result().toHex());
    const QString want = expectHexLowerOrMixed.trimmed().toLower();
    if (got.toLower() != want) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("SHA256 校验不一致，文件可能损坏或被篡改");
        }
        return false;
    }
    return true;
}

#ifdef Q_OS_WIN
/// @param libsSourceDir 非空则在本机退出后 xcopy 覆盖 Libs（与平台 wg zip 内 Libs 一致）；setting.config 在退出前已合并（用户键保留 + 包内新增键）
/// @param backupTag 用于 gateway_update_backup_<tag> 目录名（结束进程后备份旧 exe 与整份 Libs）
bool WriteAndLaunchSwapBat(const QString &currentExePath,
                           const QString &newExePath,
                           const QString &libsSourceDir,
                           const QString &backupTag,
                           QString *errorMessage)
{
    const qint64 pid = QCoreApplication::applicationPid();
    const QString batPath = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("gateway_apply_update.bat"));
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString appDirEsc = QDir::toNativeSeparators(appDir);

    const QString oldEsc = QDir::toNativeSeparators(currentExePath);
    const QString newEsc = QDir::toNativeSeparators(newExePath);
    const QString libsEsc = libsSourceDir.trimmed().isEmpty() ? QString() : QDir::toNativeSeparators(libsSourceDir);
    const QString bakDirEsc = QDir::toNativeSeparators(
        QDir(appDir).filePath(QStringLiteral("gateway_update_backup_%1").arg(backupTag)));

    QString content = QStringLiteral(
                          "@echo off\r\n"
                          "chcp 65001 >nul\r\n"
                          "setlocal EnableExtensions\r\n"
                          "ping 127.0.0.1 -n 3 >nul\r\n"
                          "taskkill /F /PID %1 >nul 2>&1\r\n"
                          "ping 127.0.0.1 -n 2 >nul\r\n"
                          "mkdir \"%4\" 2>nul\r\n"
                          "for %%I in (\"%2\") do copy /y \"%2\" \"%4\\%%~nxI\"\r\n"
                          "if exist \"%3\\Libs\\\" xcopy /e /i /y /q \"%3\\Libs\\*\" \"%4\\Libs\\\"\r\n"
                          "del /f /q \"%2\" 2>nul\r\n"
                          "move /y \"%5\" \"%2\"\r\n")
                          .arg(QString::number(pid))
                          .arg(oldEsc)
                          .arg(appDirEsc)
                          .arg(bakDirEsc)
                          .arg(newEsc);

    if (!libsEsc.isEmpty()) {
        content += QStringLiteral(
            "if not exist \"%1\\Libs\" mkdir \"%1\\Libs\"\r\n"
            "xcopy /y /e /i /q \"%2\\*\" \"%1\\Libs\\\"\r\n")
                       .arg(appDirEsc, libsEsc);
    }

    content += QStringLiteral("start \"\" \"%1\"\r\n"
                              "rd /s /q \"%2\" 2>nul\r\n"
                              "del /f /q \"%3\" 2>nul\r\n"
                              "del \"%~f0\"\r\n")
                   .arg(oldEsc,
                        QDir::toNativeSeparators(QDir(appDir).filePath(QStringLiteral("gateway_update_extract"))),
                        QDir::toNativeSeparators(QDir(appDir).filePath(QStringLiteral("cs_update_download.pkg"))));

    QFile bat(batPath);
    if (!bat.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("无法写入更新脚本");
        }
        return false;
    }
    bat.write(content.toUtf8());
    bat.close();

    // 将 bat 路径写入 UTF-8 旁路文件，UPDATE 用 --bat-file 读取；避免安装目录含中文时 Win32 子进程命令行编码导致 --bat 丢失。
    const QString batListPath = QDir::temp().filePath(
        QStringLiteral("gateway_UPDATE_batpath_%1.txt").arg(pid));
    QFile::remove(batListPath);
    bool wroteBatListPath = false;
    {
        QFile tf(batListPath);
        if (tf.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            tf.write(QDir::toNativeSeparators(batPath).toUtf8());
            tf.putChar('\n');
            tf.close();
            wroteBatListPath = true;
        } else {
            AppLogger::WriteLog(QStringLiteral("在线更新：无法写入 bat 路径旁路文件 %1，将尝试命令行 --bat。").arg(batListPath));
        }
    }

    // 优先：内嵌的 UPDATE.exe（小助手，启动快）；否则回退 PowerShell + gateway_update_wait.ps1
    const QString updateExeTmp = QDir::temp().filePath(
        QStringLiteral("gateway_UPDATE_%1.exe").arg(QCoreApplication::applicationPid()));
    QFile::remove(updateExeTmp);
    if (QFile::exists(QStringLiteral(":/tools/UPDATE.exe"))) {
        if (QFile::copy(QStringLiteral(":/tools/UPDATE.exe"), updateExeTmp)) {
            QFile::setPermissions(updateExeTmp, QFile::ReadUser | QFile::WriteUser | QFile::ExeUser);
            const QStringList updateArgs = wroteBatListPath
                ? QStringList{QStringLiteral("--bat-file"), QDir::toNativeSeparators(batListPath)}
                : QStringList{QStringLiteral("--bat"), QDir::toNativeSeparators(batPath)};
            if (QProcess::startDetached(updateExeTmp, updateArgs, appDir)) {
                return true;
            }
            AppLogger::WriteLog(QStringLiteral("在线更新：startDetached(UPDATE) 返回 false，路径 %1").arg(updateExeTmp));
        } else {
            AppLogger::WriteLog(QStringLiteral("在线更新：内嵌 UPDATE.exe 复制到临时目录失败：%1").arg(updateExeTmp));
        }
        QFile::remove(updateExeTmp);
        AppLogger::WriteLog(QStringLiteral("在线更新：内嵌 UPDATE.exe 未能启动，回退 PowerShell 脚本。"));
    } else {
        AppLogger::WriteLog(QStringLiteral("在线更新：当前主程序无内嵌资源 :/tools/UPDATE.exe（需用含 gateway_update_helper 的 CMake 重编），回退 PowerShell。"));
    }

    if (wroteBatListPath) {
        QFile::remove(batListPath);
    }

    const QString ps1Path = QDir(appDir).filePath(QStringLiteral("gateway_update_wait.ps1"));
    if (!WriteGatewayUpdateSplashScript(ps1Path, errorMessage)) {
        return false;
    }

    const QString psExe = ResolveWindowsPowerShellExePath();
    const QStringList psArgs{QStringLiteral("-NoLogo"),
                               QStringLiteral("-NoProfile"),
                               QStringLiteral("-Sta"),
                               QStringLiteral("-WindowStyle"),
                               QStringLiteral("Hidden"),
                               QStringLiteral("-ExecutionPolicy"),
                               QStringLiteral("Bypass"),
                               QStringLiteral("-File"),
                               QDir::toNativeSeparators(ps1Path),
                               QStringLiteral("-BatPath"),
                               QDir::toNativeSeparators(batPath)};

    const bool started = QProcess::startDetached(QDir::toNativeSeparators(psExe), psArgs, appDir);
    if (!started) {
        QFile::remove(ps1Path);
        if (errorMessage) {
            *errorMessage = QStringLiteral("无法启动更新助手（PowerShell）");
        }
        return false;
    }
    return true;
}
#endif

} // namespace

GatewayUpdateController::GatewayUpdateController(QObject *parent)
    : QObject(parent)
{
}

void GatewayUpdateController::checkAndOfferInstall(QWidget *dialogParent, const QString &manifestUrl)
{
    Manifest m;
    QString err;
    const QString trimmedManifest = manifestUrl.trimmed();

    if (!trimmedManifest.isEmpty()) {
        const QUrl url = QUrl::fromUserInput(trimmedManifest);
        if (!url.isValid() || url.scheme().isEmpty()) {
            UiHelpers::ShowThemedAlert(dialogParent, QMessageBox::Warning, QStringLiteral("在线更新"),
                                       QStringLiteral("自定义清单地址无效。"));
            return;
        }

        QByteArray body;
        AppLogger::WriteLog(QStringLiteral("正在检查在线更新（自定义清单）：%1").arg(url.toString()));
        if (!HttpGetBlocking(dialogParent, url, kMaxManifestBytes, &body, &err)) {
            AppLogger::WriteLog(QStringLiteral("拉取更新清单失败：%1").arg(err));
            UiHelpers::ShowThemedAlert(dialogParent, QMessageBox::Warning, QStringLiteral("检查更新失败"),
                                       QStringLiteral("无法获取版本清单：%1").arg(err));
            return;
        }

        if (!ParseManifestJson(body, &m, &err)) {
            AppLogger::WriteLog(QStringLiteral("更新清单无效：%1").arg(err));
            UiHelpers::ShowThemedAlert(dialogParent, QMessageBox::Warning, QStringLiteral("检查更新失败"), err);
            return;
        }
    } else {
        const AppConfigValues cfg = AppConfig::Load();
        if (cfg.restUrl.trimmed().isEmpty() || cfg.secretKey.trimmed().isEmpty()) {
            UiHelpers::ShowThemedAlert(dialogParent, QMessageBox::Information, QStringLiteral("在线更新"),
                                       QStringLiteral("请先在设置中填写「网站地址（RestUrl）」与通讯密钥并保存。\n\n"
                                                      "检查更新将通过租户接口获取平台发布的版本与下载地址（与商户端「下载网关」同源）。"));
            return;
        }

        GatewayApiClient api(cfg);
        // AppLogger::WriteLog(QStringLiteral("正在从租户接口检查网关更新（GetGatewayInstallerUpdateInfo）…"));
        const QJsonObject o = api.GetGatewayInstallerUpdateInfo(&err);
        if (o.isEmpty()) {
            AppLogger::WriteLog(QStringLiteral("获取网关更新信息失败：%1").arg(err));
            UiHelpers::ShowThemedAlert(dialogParent, QMessageBox::Warning, QStringLiteral("检查更新失败"),
                                       QStringLiteral("无法从租户接口获取更新信息。请确认 RestUrl、通讯密钥正确，且服务端已部署 Client/GetGatewayInstallerUpdateInfo。\n\n%1")
                                           .arg(err));
            return;
        }

        const QString latest = o.value(QStringLiteral("latestVersion")).toString().trimmed();
        const QString download = o.value(QStringLiteral("downloadUrl")).toString().trimmed();
        if (latest.isEmpty()) {
            UiHelpers::ShowThemedAlert(dialogParent, QMessageBox::Information, QStringLiteral("在线更新"),
                                       QStringLiteral("平台尚未在「系统设置」中配置网关版本号，或版本字段为空。\n"
                                                      "请在管理后台维护后再检查更新。"));
            return;
        }
        if (download.isEmpty()) {
            UiHelpers::ShowThemedAlert(dialogParent, QMessageBox::Information, QStringLiteral("在线更新"),
                                       QStringLiteral("当前无可用下载地址：请确认管理后台已上传「传奇网关」安装包（资源 key：wg），"
                                                      "且线路表中已配置「总后台域名」。"));
            return;
        }

        const QByteArray body = QJsonDocument(o).toJson(QJsonDocument::Compact);
        if (!ParseManifestJson(body, &m, &err)) {
            AppLogger::WriteLog(QStringLiteral("解析平台返回的更新信息失败：%1").arg(err));
            UiHelpers::ShowThemedAlert(dialogParent, QMessageBox::Warning, QStringLiteral("检查更新失败"), err);
            return;
        }
    }

    const QString cur = GatewayApp::versionString();
    if (CompareSemverLike(m.latestVersion, cur) <= 0) {
        // AppLogger::WriteLog(QStringLiteral("当前已是最新版本（当前 %1，清单 %2）").arg(cur, m.latestVersion));
        UiHelpers::ShowThemedAlert(dialogParent, QMessageBox::Information, QStringLiteral("在线更新"),
                                   QStringLiteral("当前版本 %1 已是最新（清单最新为 %2）。").arg(cur, m.latestVersion));
        return;
    }

    QString notes = m.releaseNotes.isEmpty() ? QStringLiteral("（无说明）") : m.releaseNotes;
    if (!UiHelpers::ShowThemedQuestion(
            dialogParent,
            QStringLiteral("发现新版本"),
            QStringLiteral("新版本：%1\n当前版本：%2\n\n更新说明：\n%3\n\n"
                           "是否下载并安装？将先备份当前主程序与 Libs 目录到 gateway_update_backup_时间戳，再关闭本程序完成替换。\n"
                           "说明：Libs\\setting.config 将保留您已保存的项；安装包中新增的配置项会自动写入，不会用包内默认值覆盖您已有的键。")
                .arg(m.latestVersion, cur, notes))) {
        return;
    }

    const QString appDir = QCoreApplication::applicationDirPath();
    const QString pkgPath = QDir(appDir).filePath(QStringLiteral("cs_update_download.pkg"));
    const QString extractRoot = QDir(appDir).filePath(QStringLiteral("gateway_update_extract"));
    QFile::remove(pkgPath);
    if (QDir(extractRoot).exists()) {
        QDir(extractRoot).removeRecursively();
    }

    const QUrl dlu = QUrl::fromUserInput(m.downloadUrl);
    if (!dlu.isValid() || dlu.scheme().isEmpty()) {
        UiHelpers::ShowThemedAlert(dialogParent, QMessageBox::Warning, QStringLiteral("在线更新"),
                                   QStringLiteral("下载地址无效。"));
        return;
    }

    AppLogger::WriteLog(QStringLiteral("开始下载更新包：%1").arg(dlu.toString()));
    if (!DownloadToFileBlocking(dialogParent, dlu, pkgPath, &err)) {
        AppLogger::WriteLog(QStringLiteral("下载更新包失败：%1").arg(err));
        UiHelpers::ShowThemedAlert(dialogParent, QMessageBox::Warning, QStringLiteral("下载失败"), err);
        return;
    }

    QScopedPointer<QProgressDialog> shaBusy;
    if (!m.sha256Hex.trimmed().isEmpty()) {
        shaBusy.reset(new QProgressDialog(QStringLiteral("正在校验安装包 SHA256，请稍候…"),
                                          QString(),
                                          0,
                                          0,
                                          dialogParent));
        shaBusy->setWindowTitle(QStringLiteral("在线更新"));
        shaBusy->setWindowModality(Qt::WindowModal);
        shaBusy->setMinimumDuration(0);
        shaBusy->setCancelButton(nullptr);
        shaBusy->show();
        QApplication::processEvents();
    }
    if (!VerifyFileSha256(pkgPath, m.sha256Hex, &err)) {
        shaBusy.reset();
        QFile::remove(pkgPath);
        AppLogger::WriteLog(QStringLiteral("更新包校验失败：%1").arg(err));
        UiHelpers::ShowThemedAlert(dialogParent, QMessageBox::Warning, QStringLiteral("校验失败"), err);
        return;
    }
    shaBusy.reset();

#ifdef Q_OS_WIN
    const QString exePath = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    QString newExePath = pkgPath;
    QString libsFrom;
    if (IsZipLocalFile(pkgPath)) {
        if (!ExpandUpdateZipWindows(pkgPath, extractRoot, &err, dialogParent)) {
            AppLogger::WriteLog(QStringLiteral("解压更新包失败：%1").arg(err));
            UiHelpers::ShowThemedAlert(dialogParent, QMessageBox::Warning, QStringLiteral("安装失败"), err);
            QFile::remove(pkgPath);
            QDir(extractRoot).removeRecursively();
            return;
        }
        newExePath = FindMainExeInUpdatePackage(extractRoot, 12);
        if (newExePath.isEmpty()) {
            LogUpdateExtractDirectorySnapshot(extractRoot);
            err = QStringLiteral(
                "压缩包内未找到 .exe。请确认发布 zip 内含网关主程序（可为 cs.exe 或 支付网关.exe），且与 Libs 目录为常见发布结构（同级或浅层目录）；"
                "目录嵌套过深时可能超出扫描深度。若日志中解压目录为空或异常，请重新上传 wg（服务端已改为 UTF-8 zip 条目标记）后重试。");
            AppLogger::WriteLog(err);
            UiHelpers::ShowThemedAlert(dialogParent, QMessageBox::Warning, QStringLiteral("安装失败"), err);
            QFile::remove(pkgPath);
            QDir(extractRoot).removeRecursively();
            return;
        }
        libsFrom = FindLibsDirWithSettingRecursive(extractRoot, 12);
        if (libsFrom.isEmpty()) {
            libsFrom = FindLibsDirByNameRecursive(extractRoot, 12);
        }
        if (libsFrom.isEmpty()) {
            const QDir tryLibs(QDir(extractRoot).filePath(QStringLiteral("Libs")));
            if (tryLibs.exists()) {
                libsFrom = QDir::cleanPath(tryLibs.absolutePath());
            }
        }
        if (!libsFrom.isEmpty()) {
            const QString pkgIni = QDir(libsFrom).filePath(QStringLiteral("setting.config"));
            const QString userIni = AppConfig::ConfigFilePath();
            if (QFileInfo::exists(pkgIni) && QFileInfo::exists(userIni)) {
                const QString mergedTmp = pkgIni + QStringLiteral(".merged");
                QString mergeErr;
                if (AppConfig::MergeIniAddMissingKeys(userIni, pkgIni, mergedTmp, &mergeErr)) {
                    QFile::remove(pkgIni);
                    bool wrotePkgIni = false;
                    if (QFile::rename(mergedTmp, pkgIni)) {
                        wrotePkgIni = true;
                    } else if (QFile::copy(mergedTmp, pkgIni)) {
                        QFile::remove(mergedTmp);
                        wrotePkgIni = true;
                    } else {
                        QFile::remove(mergedTmp);
                    }
                    if (wrotePkgIni) {
                        AppLogger::WriteLog(QStringLiteral("在线更新：已合并 setting.config（本机项保留 + 包内新增键），再覆盖 Libs（源：%1）").arg(libsFrom));
                    } else {
                        AppLogger::WriteLog(QStringLiteral("在线更新：setting.config 合并后无法写回安装包目录，将使用包内原 setting.config。"));
                    }
                } else {
                    AppLogger::WriteLog(QStringLiteral("在线更新：setting.config 合并失败，将使用包内原文件：%1").arg(mergeErr));
                }
            } else {
                AppLogger::WriteLog(QStringLiteral("在线更新：将用包内 Libs 覆盖本机 Libs（源：%1）").arg(libsFrom));
            }
        } else {
            AppLogger::WriteLog(QStringLiteral("在线更新：包内无 Libs 目录，仅替换主程序 exe"));
        }
    } else if (!IsPeExecutableLocalFile(pkgPath)) {
        QFile::remove(pkgPath);
        UiHelpers::ShowThemedAlert(dialogParent, QMessageBox::Warning, QStringLiteral("安装失败"),
                                   QStringLiteral("下载内容既不是 zip 安装包也不是 Windows 可执行文件，无法自动安装。"));
        return;
    }

    const QString backupTag = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
    const QString backupDir = QDir(appDir).filePath(QStringLiteral("gateway_update_backup_%1").arg(backupTag));
    AppLogger::WriteLog(QStringLiteral("在线更新：进程退出后将备份至目录 %1").arg(backupDir));

    QWidget *handoffSplash = ShowGatewayUpdateHandoffSplash(dialogParent);
    QApplication::setOverrideCursor(Qt::BusyCursor);
    QApplication::processEvents(QEventLoop::AllEvents);

    if (!WriteAndLaunchSwapBat(exePath, newExePath, libsFrom, backupTag, &err)) {
        AppLogger::WriteLog(QStringLiteral("启动更新脚本失败：%1").arg(err));
        QApplication::restoreOverrideCursor();
        if (handoffSplash) {
            handoffSplash->close();
            handoffSplash->deleteLater();
        }
        UiHelpers::ShowThemedAlert(dialogParent, QMessageBox::Warning, QStringLiteral("安装失败"), err);
        return;
    }
    AppLogger::WriteLog(QStringLiteral("已启动静默更新（含「更新中」提示窗口），主进程即将退出。"));
    QApplication::processEvents(QEventLoop::AllEvents);
    QTimer::singleShot(kQuitDelayMsAfterStartingUpdateHelper, qApp, []() {
        QApplication::restoreOverrideCursor();
        QCoreApplication::quit();
    });
#else
    Q_UNUSED(pkgPath);
    UiHelpers::ShowThemedAlert(dialogParent, QMessageBox::Information, QStringLiteral("在线更新"),
                               QStringLiteral("已下载更新包到程序目录（cs_update_download.pkg），但自动安装仅支持 Windows。"
                                              "若为 zip，请解压后手动替换主程序（支付网关.exe）与 Libs。"));
#endif
}

QString GatewayUpdateController::ComputeMainWindowUpdateStatusLineText()
{
    const AppConfigValues cfg = AppConfig::Load();
    if (cfg.restUrl.trimmed().isEmpty() || cfg.secretKey.trimmed().isEmpty()) {
        return QStringLiteral("↑ 保存站点地址与通讯密钥后可检测版本");
    }

    QString err;
    GatewayApiClient api(cfg);
    const QJsonObject o = api.GetGatewayInstallerUpdateInfo(&err);
    if (o.isEmpty()) {
        AppLogger::WriteLog(QStringLiteral("主窗口版本状态：获取更新信息失败 %1").arg(err));
        return QStringLiteral("↑ 无法获取线上版本（请检查网络与设置）");
    }

    const QString latest = o.value(QStringLiteral("latestVersion")).toString().trimmed();
    const QString download = o.value(QStringLiteral("downloadUrl")).toString().trimmed();
    if (latest.isEmpty()) {
        return QStringLiteral("↑ 平台尚未配置网关版本号");
    }

    const QString cur = GatewayApp::versionString();
    if (CompareSemverLike(latest, cur) <= 0) {
        return QStringLiteral("↑ 已是最新版本");
    }
    if (download.isEmpty()) {
        return QStringLiteral("↑ 发现新版本 %1（暂无下载地址）").arg(latest);
    }
    return QStringLiteral("↑ 发现新版本 %1").arg(latest);
}
