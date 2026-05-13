#include "mainwindow.h"
#include "./ui_mainwindow.h"

#include "appconfig.h"
#include "appversion.h"
#include "gatewayhttpserver.h"
#include "gatewaypresencecontroller.h"
#include "gatewayupdatecontroller.h"
#include "logpage.h"
#include "orderpage.h"
#include "partitionpage.h"
#include "scriptpage.h"
#include "settingspage.h"
#include "startupservice.h"
#include "pageutils.h"
#include "checkboxstyle.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDialog>
#include <QDir>
#include <QFutureWatcher>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QIODevice>
#include <QIcon>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QSizePolicy>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QShortcut>
#include <QStackedWidget>
#include <QStringList>
#include <QTimer>
#include <QtConcurrent/QtConcurrent>

namespace {
constexpr int kGatewayUpdateStatusPollIntervalMs = 30 * 60 * 1000; // 每 30 分钟与线上一致性检查
}
#include <QStyle>
#include <QSystemTrayIcon>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

/// 导航图标：优先已编入 exe 的资源（CMake 生成 nav_icons_embed.qrc，前缀 :/nav/），
/// 其次「与主程序 exe 同级的 win/」同名文件（便于本地覆盖调试）。扩展名 .png、.ico。
QIcon LoadNavIcon(const QStringList &baseNames)
{
    static const QString kResPrefix = QStringLiteral(":/nav/");
    static const QStringList kExts{QStringLiteral("png"), QStringLiteral("ico")};
    const QString winDir = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("win"));

    auto tryPath = [](const QString &path) -> QIcon {
        if (!QFileInfo::exists(path)) {
            return {};
        }
        const QPixmap pm(path);
        if (pm.isNull()) {
            return {};
        }
        return QIcon(pm);
    };

    for (const QString &baseName : baseNames) {
        if (baseName.isEmpty()) {
            continue;
        }
        for (const QString &ext : kExts) {
            const QString resPath = kResPrefix + baseName + QLatin1Char('.') + ext;
            const QIcon fromRes = tryPath(resPath);
            if (!fromRes.isNull()) {
                return fromRes;
            }
            const QString diskPath = QDir(winDir).filePath(baseName + QLatin1Char('.') + ext);
            const QIcon fromDisk = tryPath(diskPath);
            if (!fromDisk.isNull()) {
                return fromDisk;
            }
        }
    }
    return {};
}

/// 任意尺寸/长宽比的图标：等比装入 logicalSide 正方形，透明边居中；高 DPI 下按 devicePixelRatio 放大像素避免糊。
QPixmap NormalizeNavIconPixmap(const QIcon &ico, int logicalSide, qreal devicePixelRatio)
{
    if (ico.isNull() || logicalSide < 1) {
        return {};
    }
    const qreal dpr = qMax(1.0, devicePixelRatio);
    const int pxSide = qMax(1, qRound(logicalSide * dpr));

    QPixmap src;
    const QList<QSize> avail = ico.availableSizes();
    if (!avail.isEmpty()) {
        QSize pick = avail.first();
        for (const QSize &s : avail) {
            if (s.width() * s.height() > pick.width() * pick.height()) {
                pick = s;
            }
        }
        src = ico.pixmap(pick, QIcon::Normal, QIcon::Off);
    } else {
        src = ico.pixmap(256, 256, QIcon::Normal, QIcon::Off);
    }
    if (src.isNull()) {
        return {};
    }

    QPixmap canvas(pxSide, pxSide);
    canvas.fill(Qt::transparent);
    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    const QPixmap scaled = src.scaled(pxSide, pxSide, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    painter.drawPixmap((pxSide - scaled.width()) / 2, (pxSide - scaled.height()) / 2, scaled);
    painter.end();
    canvas.setDevicePixelRatio(dpr);
    return canvas;
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // CustomizeWindowHint + Frameless：减少在部分 Windows 版本上仍出现系统标题栏叠在自定义顶栏上的情况
    setWindowFlags(Qt::Window | Qt::CustomizeWindowHint | Qt::FramelessWindowHint);

    if (ui->menubar) {
        ui->menubar->hide();
    }

    setMinimumSize(800, 640);

    delete ui->tabWidget;

    auto *rootLayout = new QVBoxLayout(ui->centralwidget);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    auto *navBarFrame = new QFrame(ui->centralwidget);
    navBarFrame->setObjectName("navBarFrame");
    // 高度 = 上下边距 + 导航项固定高度（图标+间距+一行字），避免裁切或文字被挤没
    constexpr int kNavIconSide = 48;
    constexpr int kNavIconTextGap = 7;
    constexpr int kNavTextMinH = 22;
    constexpr int kNavItemVMarginTop = 8;
    constexpr int kNavItemVMarginBottom = 10;
    constexpr int kNavButtonFixedH =
        kNavItemVMarginTop + kNavIconSide + kNavIconTextGap + kNavTextMinH + kNavItemVMarginBottom;
    navBarFrame->setFixedHeight(10 + 10 + kNavButtonFixedH);

    auto *navLayout = new QHBoxLayout(navBarFrame);
    navLayout->setContentsMargins(20, 10, 16, 10);
    navLayout->setSpacing(10);

    const auto createNavButton = [this, navBarFrame, kNavIconSide, kNavIconTextGap, kNavTextMinH, kNavItemVMarginTop,
                                  kNavItemVMarginBottom,
                                  kNavButtonFixedH](const QString &text, const QStringList &iconBaseNames,
                                                    QStyle::StandardPixmap fallback, bool showCaptionBelowIcon = true) {
        auto *button = new QToolButton(navBarFrame);
        button->setObjectName(QStringLiteral("gatewayNavButton"));
        button->setCheckable(true);
        button->setAutoRaise(true);
        button->setFocusPolicy(Qt::NoFocus);
        button->setText(QString());
        button->setAccessibleName(text);

        QIcon ico = LoadNavIcon(iconBaseNames);
        if (ico.isNull()) {
            ico = style()->standardIcon(fallback);
        }
        const QPixmap iconPm = NormalizeNavIconPixmap(ico, kNavIconSide, button->devicePixelRatioF());

        auto *iconLabel = new QLabel(button);
        iconLabel->setObjectName(QStringLiteral("gatewayNavIcon"));
        iconLabel->setAlignment(Qt::AlignCenter);
        iconLabel->setFixedSize(kNavIconSide, kNavIconSide);
        if (!iconPm.isNull()) {
            iconLabel->setPixmap(iconPm);
        }

        auto *v = new QVBoxLayout(button);
        v->setContentsMargins(12, kNavItemVMarginTop, 12, kNavItemVMarginBottom);
        v->setSpacing(0);
        if (showCaptionBelowIcon) {
            auto *textLabel = new QLabel(text, button);
            textLabel->setObjectName(QStringLiteral("gatewayNavText"));
            textLabel->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
            textLabel->setMinimumHeight(kNavTextMinH);
            v->addWidget(iconLabel, 0, Qt::AlignHCenter);
            v->addSpacing(kNavIconTextGap);
            v->addWidget(textLabel, 0, Qt::AlignHCenter);
        } else {
            v->addStretch(1);
            v->addWidget(iconLabel, 0, Qt::AlignHCenter);
            v->addStretch(1);
        }

        button->setFixedHeight(kNavButtonFixedH);
        button->setMinimumWidth(96);
        button->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
        return button;
    };

    // 自定义图标：构建时由 win/ 打入 :/nav/；也可用同级 win/ 覆盖。命名：nav_*.png 或 1～5.png（左到右）。
    auto *systemLogButton = createNavButton(QStringLiteral("系统日志"),
                                            QStringList{QStringLiteral("nav_system_log"), QStringLiteral("1")},
                                            QStyle::SP_ComputerIcon);
    auto *partitionButton = createNavButton(QStringLiteral("分区管理"),
                                            QStringList{QStringLiteral("nav_partition"), QStringLiteral("2")},
                                            QStyle::SP_DriveHDIcon);
    auto *orderButton = createNavButton(QStringLiteral("订单查询"),
                                        QStringList{QStringLiteral("nav_order"), QStringLiteral("3")},
                                        QStyle::SP_FileDialogContentsView);
    auto *scriptButton = createNavButton(QStringLiteral("脚本编辑"),
                                         QStringList{QStringLiteral("nav_script"), QStringLiteral("4")},
                                         QStyle::SP_FileIcon);
    auto *settingsButton = createNavButton(QStringLiteral("系统设置"),
                                           QStringList{QStringLiteral("nav_settings"), QStringLiteral("5")},
                                           QStyle::SP_FileDialogDetailedView);

    const auto createCaptionButton = [navBarFrame](const QString &objectName, const QString &text) {
        auto *button = new QToolButton(navBarFrame);
        button->setObjectName(objectName);
        button->setAutoRaise(true);
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        button->setText(text);
        button->setFocusPolicy(Qt::NoFocus);
        button->setFixedSize(48, 40);
        return button;
    };

    auto *minimizeButton = createCaptionButton(QStringLiteral("minimizeButton"), QStringLiteral("\u2212"));
    auto *maximizeButton = createCaptionButton(QStringLiteral("maximizeButton"), QStringLiteral("\u25A1"));
    auto *closeButton = createCaptionButton(QStringLiteral("closeButton"), QStringLiteral("\u00D7"));
    m_windowMaximizeButton = maximizeButton;

    navLayout->addWidget(systemLogButton, 0, Qt::AlignVCenter);
    navLayout->addWidget(partitionButton, 0, Qt::AlignVCenter);
    navLayout->addWidget(orderButton, 0, Qt::AlignVCenter);
    navLayout->addWidget(scriptButton, 0, Qt::AlignVCenter);
    navLayout->addWidget(settingsButton, 0, Qt::AlignVCenter);
    navLayout->addStretch(1);
    auto *navBarSep = new QFrame(navBarFrame);
    navBarSep->setObjectName(QStringLiteral("navBarSeparator"));
    navBarSep->setFixedSize(1, 52);
    navLayout->addWidget(navBarSep, 0, Qt::AlignVCenter);
    navLayout->addSpacing(4);
    navLayout->addWidget(minimizeButton, 0, Qt::AlignVCenter);
    navLayout->addWidget(maximizeButton, 0, Qt::AlignVCenter);
    navLayout->addWidget(closeButton, 0, Qt::AlignVCenter);

    auto *stackedWidget = new QStackedWidget(ui->centralwidget);
    auto *logPage = new LogPage(stackedWidget);
    auto *partitionPage = new PartitionPage(stackedWidget);
    auto *orderPage = new OrderPage(stackedWidget);
    auto *scriptPage = new ScriptPage(stackedWidget);
    auto *settingsPage = new SettingsPage(stackedWidget);

    connect(partitionPage, &PartitionPage::statusMessageRequested, ui->statusbar, &QStatusBar::showMessage);
    connect(settingsPage, &SettingsPage::configReloaded, this, [this, stackedWidget, systemLogButton, logPage] {
        logPage->ReloadCurrentSessionLogs();
        UpdateGatewayListenDisplay();
        RefreshUpdateStatusLineAsync();
        stackedWidget->setCurrentIndex(0);
        systemLogButton->setChecked(true);
        ui->statusbar->showMessage(QStringLiteral("设置已保存，运行配置已重载"), 4000);
    });
    connect(settingsPage, &SettingsPage::gatewayUpdateCheckFinished, this, [this] { RefreshUpdateStatusLineAsync(); });

    stackedWidget->addWidget(logPage);
    stackedWidget->addWidget(partitionPage);
    stackedWidget->addWidget(orderPage);
    stackedWidget->addWidget(scriptPage);
    stackedWidget->addWidget(settingsPage);

    rootLayout->addWidget(navBarFrame);
    rootLayout->addWidget(stackedWidget);

    auto *buttonGroup = new QButtonGroup(this);
    buttonGroup->setExclusive(true);
    buttonGroup->addButton(systemLogButton, 0);
    buttonGroup->addButton(partitionButton, 1);
    buttonGroup->addButton(orderButton, 2);
    buttonGroup->addButton(scriptButton, 3);
    buttonGroup->addButton(settingsButton, 4);
    connect(buttonGroup, &QButtonGroup::idClicked, stackedWidget, &QStackedWidget::setCurrentIndex);
    systemLogButton->setChecked(true);

    connect(minimizeButton, &QToolButton::clicked, this, &QWidget::showMinimized);
    connect(maximizeButton, &QToolButton::clicked, this, [this] {
        if (isMaximized()) {
            showNormal();
        } else {
            showMaximized();
        }
        SyncWindowMaximizeGlyph();
    });
    SyncWindowMaximizeGlyph();
    connect(closeButton, &QToolButton::clicked, this, &MainWindow::close);

    InstallNavBarDragFilters(navBarFrame);

    m_updateStatusLabel = new QLabel(QStringLiteral("↑ 正在检测版本…"), this);
    auto *rightStatusLabel = new QLabel(QStringLiteral("版本号: %1").arg(GatewayApp::versionDisplay()), this);
    m_gatewayListenStatusLabel = new QLabel(this);
    m_updateStatusLabel->setObjectName("leftStatusLabel");
    rightStatusLabel->setObjectName("rightStatusLabel");
    m_gatewayListenStatusLabel->setObjectName("gatewayStatusLabel");
    ui->statusbar->setSizeGripEnabled(false);
    ui->statusbar->addWidget(m_updateStatusLabel);
    ui->statusbar->addPermanentWidget(m_gatewayListenStatusLabel);
    ui->statusbar->addPermanentWidget(rightStatusLabel);
    UpdateGatewayListenDisplay();
    RefreshUpdateStatusLineAsync();
    m_updateStatusPollTimer = new QTimer(this);
    m_updateStatusPollTimer->setInterval(kGatewayUpdateStatusPollIntervalMs);
    connect(m_updateStatusPollTimer, &QTimer::timeout, this, [this] { RefreshUpdateStatusLineAsync(); });
    m_updateStatusPollTimer->start();
    SetupTrayIcon();

    m_styleSheetBase = QStringLiteral(
        "QMainWindow { background: #ffffff; border: 1px solid #b7b7b7; }"
        "QPushButton { font-weight: normal; }"
        "#navBarFrame { background-color: #8b4a53; border: none; border-bottom: 1px solid #6d3a42; }"
        "#navBarSeparator { background-color: rgba(255, 255, 255, 0.22); border: none; margin: 0px 2px; }"
        "QToolButton { color: #ffffff; border: none; padding: 4px 10px; font-size: 12px; font-weight: normal; }"
        "QToolButton#gatewayNavButton { padding: 0px; border: none; border-radius: 10px; font-size: 0px; }"
        "QLabel#gatewayNavIcon { background: transparent; border: none; }"
        "QLabel#gatewayNavText { color: #ffffff; font-size: 13px; font-weight: normal; letter-spacing: 0.5px; "
        "background: transparent; border: none; margin: 0px; padding: 0px; }"
        "QToolButton#gatewayNavButton:hover:!checked { background: rgba(255, 255, 255, 0.10); }"
        "QToolButton#gatewayNavButton:checked { background: rgba(255, 255, 255, 0.22); }"
        "QToolButton:hover, QToolButton:checked { background: rgba(255, 255, 255, 0.14); border-radius: 4px; }"
        "#minimizeButton, #maximizeButton, #closeButton { color: #ffffff; padding: 0; border-radius: 0; "
        "font-size: 22px; font-weight: normal; min-width: 48px; min-height: 40px; }"
        "#closeButton { font-size: 26px; font-weight: normal; }"
        "#minimizeButton:hover, #maximizeButton:hover { background: rgba(255, 255, 255, 0.18); }"
        "#closeButton:hover { background: #d9534f; color: #ffffff; }"
        "#partitionSideBar { background: #faf7f7; border-right: 1px solid #d8d8d8; }"
        "QPushButton[sideMenu=\"true\"] { color: #333333; background: transparent; border: none; text-align: center; font-size: 12px; font-weight: normal; }"
        "QPushButton[sideMenu=\"true\"]:checked { background: #f1ecec; }"
        "QPushButton[sideMenu=\"true\"]:hover { background: #f5efef; }"
        "QPushButton[partitionAction=\"true\"] { color: #ffffff; background: #8b4a53; border: none; font-size: 12px; font-weight: normal; }"
        "QPushButton[partitionAction=\"true\"]:hover { background: #9a5660; }"
        "QPushButton[groupDownload=\"true\"] { color: #333333; background: #efefef; border: 1px solid #c9c0c1; font-size: 12px; font-weight: normal; }"
        "QPushButton[groupDownload=\"true\"]:hover { background: #f7f3f3; }"
        "QPushButton[scriptSave=\"true\"] { color: #8b4a53; background: #ffffff; border: 1px solid #b76a72; font-size: 12px; font-weight: normal; }"
        "QPushButton[scriptSave=\"true\"]:hover { background: #fff6f6; }"
        "QPushButton[settingsPrimary=\"true\"], QPushButton[settingsConfirm=\"true\"] { color: #ffffff; background: #8b4a53; border: none; font-size: 12px; border-radius: 6px; padding: 8px 18px; font-weight: normal; }"
        "QPushButton[settingsPrimary=\"true\"]:hover, QPushButton[settingsConfirm=\"true\"]:hover { background: #9a5660; }"
        "QPushButton[settingsSecondary=\"true\"] { color: #333333; background: #efefef; border: 1px solid #c9c0c1; font-size: 12px; font-weight: normal; }"
        "QPushButton[settingsSecondary=\"true\"]:hover { background: #f7f3f3; }"
        "QDateEdit, QLineEdit, QComboBox { border: 1px solid #cfcfcf; background: #ffffff; padding: 2px 6px; color: #222222; }"
        "QLineEdit[settingsField=\"true\"] { border: 1px solid #d4c8ca; border-radius: 4px; padding: 2px 8px; background: #ffffff; "
        "min-height: 24px; max-height: 24px; margin: 0px; }"
        "QLineEdit[settingsField=\"true\"]:focus { border: 1px solid #8b4a53; }"
        "QLabel[settingsFormLabel=\"true\"] { color: #555555; font-size: 12px; min-height: 24px; max-height: 24px; padding: 0px; }"
        "QLabel#settingsGatewayIpLabel, QLabel#settingsPortLabel { padding-top: 4px; }"
        "QLabel[settingsMutedValue=\"true\"] { color: #333333; font-size: 12px; min-height: 24px; max-height: 24px; padding: 0px 2px; }"
        "QLabel[settingsMutedValue=\"true\"] a { color: #7a4550; text-decoration: none; }"
        "QLabel#sectionNetworkLabel, QLabel#sectionFeaturesLabel, QLabel#sectionUpdateLabel { font-size: 12px; font-weight: normal; color: #8b4a53; }"
        "QListWidget { background: #faf7f7; border: none; border-right: 1px solid #d8d8d8; color: #333333; outline: none; }"
        "QListWidget::item { height: 40px; text-align: center; }"
        "QListWidget::item:selected { background: #f1ecec; color: #333333; }"
        "QCheckBox { color: #333333; spacing: 8px; }"
    ) + GatewayCheckboxStyle::indicatorRules(12) + QStringLiteral(
        "#settingsCard { background: #ffffff; border: 1px solid #e3dbdc; border-radius: 10px; }"
        "#settingsToggleArea { background-color: #faf7f7; border: 1px solid #ebe4e5; border-radius: 8px; }"
        "QPlainTextEdit { border: 1px solid #d8d8d8; background: #ffffff; color: #222222; }"
        "QTableWidget { border: none; background: #ffffff; color: #222222; gridline-color: #ead7d7; }"
        "QTableWidget::item { border-right: 1px solid #ead7d7; border-bottom: 1px solid #ead7d7; }"
        "QTableWidget#logTable { gridline-color: transparent; outline: none; }"
        "QTableWidget#logTable:focus { outline: none; }"
        "QTableWidget#logTable::item { border: none; outline: none; }"
        "QTableWidget#logTable::item:focus { outline: none; border: none; }"
        "QTableWidget#logTable::item:selected { background: #f0e4e6; color: #3f3135; }"
        "QTableWidget#partitionTemplateTable::item { border: none; padding: 4px 8px; }"
        "QTableWidget#partitionTemplateTable::item:selected { background: #f0e4e6; color: #3f3135; }"
        "QTableWidget#groupManageTable::item:selected { background: #f0e4e6; color: #3f3135; }"
        "QDialog QTableWidget { gridline-color: #ead7d7; color: #222222; }"
        "QDialog QTableWidget::item { border: none; padding: 4px 8px; }"
        "QDialog QTableWidget::item:selected { background: #f0e4e6; color: #3f3135; }"
        "QHeaderView::section { background: #f8e8e8; color: #333333; font-weight: normal; border: none; border-right: 1px solid #efcfcf; border-bottom: 1px solid #f1d6d6; padding: 3px 8px; }"
        "QLabel { color: #666666; }"
        "QStatusBar { background: #f3f3f3; color: #666666; }"
        "#leftStatusLabel { color: #d94a38; }"
    );
    ApplyMainWindowStyleSheet();

    const QString devQssPath = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("gateway_dev.qss"));
    const bool enableLiveQss = qEnvironmentVariableIsSet("GATEWAY_LIVE_QSS") || QFile::exists(devQssPath);
    if (enableLiveQss) {
        m_styleSheetWatcher = new QFileSystemWatcher(this);
        const auto armFileWatch = [this, devQssPath] {
            if (!m_styleSheetWatcher || !QFile::exists(devQssPath)) {
                return;
            }
            if (!m_styleSheetWatcher->files().contains(devQssPath)) {
                m_styleSheetWatcher->addPath(devQssPath);
            }
        };
        armFileWatch();
        connect(m_styleSheetWatcher, &QFileSystemWatcher::fileChanged, this,
                [this, devQssPath, armFileWatch](const QString &path) {
                    Q_UNUSED(path);
                    QTimer::singleShot(250, this, [this, devQssPath, armFileWatch]() {
                        ApplyMainWindowStyleSheet();
                        armFileWatch();
                    });
                });
    }

    m_presence = new GatewayPresenceController(this);
    m_presence->start();
}

void MainWindow::ApplyMainWindowStyleSheet()
{
    QString merged = m_styleSheetBase;
    const QString devPath = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("gateway_dev.qss"));
    if (QFile::exists(devPath)) {
        QFile f(devPath);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            merged += QLatin1Char('\n');
            merged += QString::fromUtf8(f.readAll());
        }
    }
    setStyleSheet(merged);
}

void MainWindow::UpdateGatewayListenDisplay()
{
    const AppConfigValues values = AppConfig::Load();
    const QString portText = values.port.trimmed().isEmpty() ? QStringLiteral("<empty>") : values.port.trimmed();
    setWindowTitle(QStringLiteral("7X 网关 - HTTP 端口 %1").arg(portText));
    if (m_gatewayListenStatusLabel) {
        if (GatewayHttpServer::instance().isListening()) {
            m_gatewayListenStatusLabel->setText(QStringLiteral("正在监听: %1").arg(portText));
        } else if (StartupService::HttpListenSkippedDueToAddressInUse()) {
            m_gatewayListenStatusLabel->setText(
                QStringLiteral("HTTP 未监听: 端口 %1 已被占用（请在设置中改端口或关闭占用进程）").arg(portText));
        } else {
            m_gatewayListenStatusLabel->setText(QStringLiteral("HTTP 未就绪: %1").arg(portText));
        }
    }
}

void MainWindow::RefreshUpdateStatusLineAsync()
{
    if (!m_updateStatusLabel) {
        return;
    }
    ++m_updateStatusRequestSeq;
    const int seq = m_updateStatusRequestSeq;
    m_updateStatusLabel->setText(QStringLiteral("↑ 正在检测版本…"));

    auto *watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher, seq]() {
        const QString text = watcher->result();
        watcher->deleteLater();
        if (!m_updateStatusLabel || m_updateStatusRequestSeq != seq) {
            return;
        }
        m_updateStatusLabel->setText(text);
    });
    watcher->setFuture(QtConcurrent::run([]() -> QString {
        return GatewayUpdateController::ComputeMainWindowUpdateStatusLineText();
    }));
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    auto *wig = qobject_cast<QWidget *>(watched);
    if (m_navBarFrame && wig && m_navBarFrame->isAncestorOf(wig)) {
        auto chainHasButton = [](QObject *o) {
            for (; o; o = o->parent()) {
                if (qobject_cast<const QAbstractButton *>(o))
                    return true;
            }
            return false;
        };

        const auto t = event->type();
        // 从顶栏空白处按下后拖动，光标经过导航按钮时 Move 会发到按钮上，须仍按拖动处理
        if (t == QEvent::MouseMove) {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            if (m_dragging && (mouseEvent->buttons() & Qt::LeftButton) && !isMaximized()) {
                move(UiHelpers::GlobalPosFromMouseEvent(mouseEvent) - m_dragPosition);
                return true;
            }
        }
        if (t == QEvent::MouseButtonRelease) {
            m_dragging = false;
            if (chainHasButton(watched))
                return QMainWindow::eventFilter(watched, event);
            return true;
        }
        if (chainHasButton(watched))
            return QMainWindow::eventFilter(watched, event);

        if (t == QEvent::MouseButtonPress) {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            if (mouseEvent->button() == Qt::LeftButton) {
                m_dragging = true;
                m_dragPosition = UiHelpers::GlobalPosFromMouseEvent(mouseEvent) - frameGeometry().topLeft();
                return true;
            }
        } else if (t == QEvent::MouseButtonDblClick) {
            if (isMaximized()) {
                showNormal();
            } else {
                showMaximized();
            }
            SyncWindowMaximizeGlyph();
            return true;
        }
    }

    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::InstallNavBarDragFilters(QWidget *navBar)
{
    m_navBarFrame = navBar;
    navBar->installEventFilter(this);
    const QList<QWidget *> descendants = navBar->findChildren<QWidget *>(QString(), Qt::FindChildrenRecursively);
    for (QWidget *w : descendants) {
        w->installEventFilter(this);
    }
}

void MainWindow::SyncWindowMaximizeGlyph()
{
    if (!m_windowMaximizeButton) {
        return;
    }
    // 最大化：显示「还原」；还原：显示「最大化」
    m_windowMaximizeButton->setText(isMaximized() ? QStringLiteral("\u2750") : QStringLiteral("\u25A1"));
}

void MainWindow::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::WindowStateChange) {
        SyncWindowMaximizeGlyph();
    }
    QMainWindow::changeEvent(event);
}

MainWindow::~MainWindow()
{
    if (m_presence) {
        m_presence->stop();
    }
    delete ui;
}

QIcon MainWindow::ApplicationIcon()
{
    const QString dir = QApplication::applicationDirPath();
    const QString base = QFileInfo(QApplication::applicationFilePath()).completeBaseName();
    const QStringList names = {
        QStringLiteral("1.ico"),
        QStringLiteral("2.ico"),
        QStringLiteral("cs.ico"),
        QStringLiteral("app.ico"),
        QStringLiteral("gateway.ico"),
        QStringLiteral("icon.ico"),
        base + QStringLiteral(".ico"),
        QStringLiteral("cs.png"),
        QStringLiteral("app.png"),
    };
    const QStringList diskRoots = {dir, QDir(dir).filePath(QStringLiteral("win"))};
    for (const QString &name : names) {
        for (const QString &root : diskRoots) {
            const QString path = QDir(root).filePath(name);
            if (!QFileInfo::exists(path)) {
                continue;
            }
            const QIcon icon(path);
            if (!icon.isNull()) {
                return icon;
            }
        }
    }
    // 与导航栏一致：编译期嵌入 win/1.ico 等（CMake nav_icons_embed.qrc），单文件部署无需再拷 ico 到 exe 旁
    const QStringList embedded = {QStringLiteral(":/nav/1.ico"),
                                  QStringLiteral(":/nav/2.ico"),
                                  QStringLiteral(":/nav/1.png"),
                                  QStringLiteral(":/nav/2.png")};
    for (const QString &p : embedded) {
        if (!QFile::exists(p)) {
            continue;
        }
        const QIcon icon(p);
        if (!icon.isNull()) {
            return icon;
        }
    }
    return {};
}

void MainWindow::SetupTrayIcon()
{
    const QIcon ico = ApplicationIcon();
    if (!ico.isNull()) {
        setWindowIcon(ico);
    }

    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        return;
    }

    const QIcon trayIco = ico.isNull() ? QIcon(QStringLiteral(":/nav/1.ico")) : ico;
    m_trayIcon = new QSystemTrayIcon(trayIco.isNull() ? style()->standardIcon(QStyle::SP_MessageBoxInformation) : trayIco,
                                       this);
    m_trayIcon->setToolTip(QStringLiteral("支付网关"));

    m_trayMenu = new QMenu(this);
    QAction *showAction = m_trayMenu->addAction(QStringLiteral("显示"));
    QAction *quitAction = m_trayMenu->addAction(QStringLiteral("退出"));
    connect(showAction, &QAction::triggered, this, &MainWindow::ShowFromTray);
    connect(quitAction, &QAction::triggered, this, &MainWindow::QuitApplication);
    m_trayIcon->setContextMenu(m_trayMenu);

    connect(m_trayIcon, &QSystemTrayIcon::activated, this, &MainWindow::OnTrayActivated);
}

void MainWindow::OnTrayActivated(QSystemTrayIcon::ActivationReason reason)
{
    if (reason == QSystemTrayIcon::DoubleClick) {
        ShowFromTray();
    }
}

void MainWindow::HideToTray()
{
    if (!m_trayIcon) {
        showMinimized();
        return;
    }
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        QMessageBox::warning(this,
                             QStringLiteral("提示"),
                             QStringLiteral("当前系统无法使用托盘图标，将改为最小化到任务栏。"));
        showMinimized();
        return;
    }
    m_trayIcon->show();
    hide();
}

void MainWindow::ShowFromTray()
{
    showNormal();
    raise();
    activateWindow();
}

void MainWindow::QuitApplication()
{
    m_allowClose = true;
    if (m_trayIcon) {
        m_trayIcon->hide();
    }
    QApplication::quit();
}

void MainWindow::ShowCloseChoiceDialog()
{
    QDialog dlg(this);
    dlg.setWindowFlags(Qt::Window | Qt::Dialog | Qt::FramelessWindowHint);
    dlg.setAttribute(Qt::WA_TranslucentBackground, true);
    dlg.setModal(true);
    dlg.setWindowIcon(windowIcon());
    dlg.setFixedSize(440, 252);

    if (QWidget *host = window()) {
        const QRect fr = host->frameGeometry();
        dlg.move(fr.center() - QPoint(dlg.width() / 2, dlg.height() / 2));
    }

    auto *outer = new QVBoxLayout(&dlg);
    outer->setContentsMargins(0, 0, 0, 0);

    auto *card = new QWidget(&dlg);
    card->setObjectName(QStringLiteral("closeChoiceDialogCard"));
    card->setStyleSheet(
        QStringLiteral(
            "#closeChoiceDialogCard { background: #ffffff; border: 1px solid #e5d2d5; border-radius: 12px; }"
            "QLabel#closeChoiceHint { color: #4a3d3f; font-size: 13px; line-height: 1.45; background: transparent; border: none; }"
            "QCheckBox#closeChoiceRemember { color: #4a3d3f; font-size: 12px; spacing: 8px; background: transparent; }"
            "QPushButton[closeDialogPrimary=\"true\"] { color: #ffffff; background: #8b4a53; border: none; border-radius: 6px; "
            "min-height: 32px; padding: 0 16px; font-size: 12px; font-weight: normal; }"
            "QPushButton[closeDialogPrimary=\"true\"]:hover { background: #9a5660; }"
            "QPushButton[closeDialogSecondary=\"true\"] { color: #333333; background: #efefef; border: 1px solid #c9c0c1; border-radius: 6px; "
            "min-height: 32px; padding: 0 16px; font-size: 12px; font-weight: normal; }"
            "QPushButton[closeDialogSecondary=\"true\"]:hover { background: #f7f3f3; }"
            "QPushButton[closeDialogDanger=\"true\"] { color: #8b4a53; background: #fff8f8; border: 1px solid #d9a8ae; border-radius: 6px; "
            "min-height: 32px; padding: 0 16px; font-size: 12px; font-weight: normal; }"
            "QPushButton[closeDialogDanger=\"true\"]:hover { background: #fff0f0; border-color: #c98f96; }"
            "QToolButton#closeChoiceDialogClose { color: #8a6d72; border: none; font-size: 18px; font-weight: normal; padding: 0 4px; background: transparent; }"
            "QToolButton#closeChoiceDialogClose:hover { color: #8b4a53; }")
        + GatewayCheckboxStyle::indicatorRules(16));

    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(18, 14, 18, 16);
    cardLayout->setSpacing(14);

    auto *titleRow = new QHBoxLayout;
    auto *titleLabel = new QLabel(QStringLiteral("关闭主窗口"), card);
    titleLabel->setStyleSheet(QStringLiteral("font-size: 15px; font-weight: normal; color: #8b4a53; background: transparent; border: none;"));
    auto *closeBtn = new QToolButton(card);
    closeBtn->setObjectName(QStringLiteral("closeChoiceDialogClose"));
    closeBtn->setText(QStringLiteral("×"));
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setAutoRaise(true);
    titleRow->addWidget(titleLabel);
    titleRow->addStretch();
    titleRow->addWidget(closeBtn, 0, Qt::AlignTop);
    cardLayout->addLayout(titleRow);

    auto *hint = new QLabel(
        QStringLiteral("请选择关闭方式（与主窗口右上角 × 行为一致）：\n"
                       "最小化到托盘后程序仍在运行，可从托盘恢复。"),
        card);
    hint->setObjectName(QStringLiteral("closeChoiceHint"));
    hint->setWordWrap(true);
    hint->setFixedWidth(404);
    cardLayout->addWidget(hint);

    auto *rememberCheck = new QCheckBox(QStringLiteral("记住本次选择"), card);
    rememberCheck->setObjectName(QStringLiteral("closeChoiceRemember"));
    rememberCheck->setCursor(Qt::PointingHandCursor);
    cardLayout->addWidget(rememberCheck);

    auto *btnRow = new QHBoxLayout;
    btnRow->setSpacing(10);
    btnRow->addStretch();
    auto *trayBtn = new QPushButton(QStringLiteral("最小化到托盘"), card);
    trayBtn->setProperty("closeDialogPrimary", true);
    auto *exitBtn = new QPushButton(QStringLiteral("退出程序"), card);
    exitBtn->setProperty("closeDialogDanger", true);
    auto *cancelBtn = new QPushButton(QStringLiteral("取消"), card);
    cancelBtn->setProperty("closeDialogSecondary", true);
    trayBtn->setDefault(true);
    trayBtn->setCursor(Qt::PointingHandCursor);
    exitBtn->setCursor(Qt::PointingHandCursor);
    cancelBtn->setCursor(Qt::PointingHandCursor);
    btnRow->addWidget(trayBtn);
    btnRow->addWidget(exitBtn);
    btnRow->addWidget(cancelBtn);
    cardLayout->addLayout(btnRow);

    outer->addWidget(card);

    connect(closeBtn, &QToolButton::clicked, &dlg, &QDialog::reject);
    connect(trayBtn, &QPushButton::clicked, &dlg, [&dlg]() { dlg.done(1); });
    connect(exitBtn, &QPushButton::clicked, &dlg, [&dlg]() { dlg.done(2); });
    connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);

    auto *escShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), &dlg);
    connect(escShortcut, &QShortcut::activated, &dlg, &QDialog::reject);

    const int result = dlg.exec();
    if (result == 0) {
        return;
    }

    if (result == 1) {
        if (rememberCheck->isChecked()) {
            AppConfigValues cfg = AppConfig::Load();
            cfg.mainWindowCloseAction = QStringLiteral("tray");
            AppConfig::Save(cfg);
        }
        HideToTray();
    } else {
        if (rememberCheck->isChecked()) {
            AppConfigValues cfg = AppConfig::Load();
            cfg.mainWindowCloseAction = QStringLiteral("exit");
            AppConfig::Save(cfg);
        }
        QuitApplication();
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_allowClose) {
        event->accept();
        return;
    }

    event->ignore();

    const QString action = AppConfig::Load().mainWindowCloseAction.trimmed().toLower();
    if (action == QLatin1String("tray")) {
        HideToTray();
        return;
    }
    if (action == QLatin1String("exit")) {
        QuitApplication();
        return;
    }

    ShowCloseChoiceDialog();
}
