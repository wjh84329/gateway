#include "mainwindow.h"
#include "./ui_mainwindow.h"

#include "appconfig.h"
#include "applogger.h"
#include "logpage.h"
#include "orderpage.h"
#include "partitionpage.h"
#include "scriptpage.h"
#include "settingspage.h"

#include <QButtonGroup>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QStackedWidget>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);

    if (ui->menubar) {
        ui->menubar->hide();
    }

    setMinimumSize(800, 600);

    delete ui->tabWidget;

    auto *rootLayout = new QVBoxLayout(ui->centralwidget);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    auto *navBarFrame = new QFrame(ui->centralwidget);
    navBarFrame->setObjectName("navBarFrame");
    navBarFrame->setFixedHeight(78);

    auto *navLayout = new QHBoxLayout(navBarFrame);
    navLayout->setContentsMargins(18, 8, 18, 6);
    navLayout->setSpacing(18);

    const auto createNavButton = [this](const QString &text, QStyle::StandardPixmap iconType) {
        auto *button = new QToolButton(this);
        button->setText(text);
        button->setCheckable(true);
        button->setAutoRaise(true);
        button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        button->setMinimumSize(86, 60);
        button->setIcon(style()->standardIcon(iconType));
        button->setIconSize(QSize(28, 28));
        return button;
    };

    auto *systemLogButton = createNavButton(QStringLiteral("系统日志"), QStyle::SP_ComputerIcon);
    auto *partitionButton = createNavButton(QStringLiteral("分区管理"), QStyle::SP_DriveHDIcon);
    auto *orderButton = createNavButton(QStringLiteral("订单查询"), QStyle::SP_FileDialogContentsView);
    auto *scriptButton = createNavButton(QStringLiteral("脚本编辑"), QStyle::SP_FileIcon);
    auto *settingsButton = createNavButton(QStringLiteral("系统设置"), QStyle::SP_FileDialogDetailedView);

    auto *dragArea = new QWidget(navBarFrame);
    dragArea->setObjectName("dragArea");
    dragArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    dragArea->installEventFilter(this);

    const auto createWindowButton = [this](QStyle::StandardPixmap iconType, const QString &objectName) {
        auto *button = new QToolButton(this);
        button->setObjectName(objectName);
        button->setAutoRaise(true);
        button->setIcon(style()->standardIcon(iconType));
        button->setIconSize(QSize(14, 14));
        button->setFixedSize(30, 24);
        return button;
    };

    auto *minimizeButton = createWindowButton(QStyle::SP_TitleBarMinButton, QStringLiteral("minimizeButton"));
    auto *maximizeButton = createWindowButton(QStyle::SP_TitleBarMaxButton, QStringLiteral("maximizeButton"));
    auto *closeButton = createWindowButton(QStyle::SP_TitleBarCloseButton, QStringLiteral("closeButton"));

    navLayout->addWidget(systemLogButton);
    navLayout->addWidget(partitionButton);
    navLayout->addWidget(orderButton);
    navLayout->addWidget(scriptButton);
    navLayout->addWidget(settingsButton);
    navLayout->addWidget(dragArea);
    navLayout->addWidget(minimizeButton);
    navLayout->addWidget(maximizeButton);
    navLayout->addWidget(closeButton);

    auto *stackedWidget = new QStackedWidget(ui->centralwidget);
    auto *logPage = new LogPage(stackedWidget);
    auto *partitionPage = new PartitionPage(stackedWidget);
    auto *orderPage = new OrderPage(stackedWidget);
    auto *scriptPage = new ScriptPage(stackedWidget);
    auto *settingsPage = new SettingsPage(stackedWidget);

    connect(partitionPage, &PartitionPage::statusMessageRequested, ui->statusbar, &QStatusBar::showMessage);
    connect(settingsPage, &SettingsPage::configReloaded, this, [this, stackedWidget, systemLogButton, logPage] {
        AppLogger::WriteLog(QStringLiteral("配置加载成功"));
        logPage->ReloadCurrentSessionLogs();
        UpdateGatewayIdDisplay();
        stackedWidget->setCurrentIndex(0);
        systemLogButton->setChecked(true);
        ui->statusbar->showMessage(QStringLiteral("配置已重新加载"), 3000);
    });

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
    connect(maximizeButton, &QToolButton::clicked, this, [this, maximizeButton] {
        if (isMaximized()) {
            showNormal();
            maximizeButton->setIcon(style()->standardIcon(QStyle::SP_TitleBarMaxButton));
        } else {
            showMaximized();
            maximizeButton->setIcon(style()->standardIcon(QStyle::SP_TitleBarNormalButton));
        }
    });
    connect(closeButton, &QToolButton::clicked, this, &QWidget::close);

    auto *leftStatusLabel = new QLabel(QStringLiteral("↑ 已是最新版本!"), this);
    auto *rightStatusLabel = new QLabel(QStringLiteral("版本号: V2.3.3"), this);
    m_gatewayIdStatusLabel = new QLabel(this);
    leftStatusLabel->setObjectName("leftStatusLabel");
    rightStatusLabel->setObjectName("rightStatusLabel");
    m_gatewayIdStatusLabel->setObjectName("gatewayStatusLabel");
    ui->statusbar->setSizeGripEnabled(false);
    ui->statusbar->addWidget(leftStatusLabel);
    ui->statusbar->addPermanentWidget(m_gatewayIdStatusLabel);
    ui->statusbar->addPermanentWidget(rightStatusLabel);
    UpdateGatewayIdDisplay();

    setStyleSheet(
        "QMainWindow { background: #ffffff; border: 1px solid #b7b7b7; }"
        "#navBarFrame { background: #8b4a53; }"
        "QToolButton { color: #ffffff; border: none; padding: 4px 10px; font-size: 12px; }"
        "QToolButton:hover, QToolButton:checked { background: rgba(255, 255, 255, 0.14); border-radius: 4px; }"
        "#minimizeButton, #maximizeButton, #closeButton { padding: 0; border-radius: 0; }"
        "#minimizeButton:hover, #maximizeButton:hover { background: rgba(255, 255, 255, 0.18); }"
        "#closeButton:hover { background: #d9534f; }"
        "#partitionSideBar { background: #faf7f7; border-right: 1px solid #d8d8d8; }"
        "QPushButton[sideMenu=\"true\"] { color: #333333; background: transparent; border: none; text-align: center; font-size: 12px; }"
        "QPushButton[sideMenu=\"true\"]:checked { background: #f1ecec; }"
        "QPushButton[sideMenu=\"true\"]:hover { background: #f5efef; }"
        "QPushButton[partitionAction=\"true\"] { color: #ffffff; background: #8b4a53; border: none; font-size: 12px; }"
        "QPushButton[partitionAction=\"true\"]:hover { background: #9a5660; }"
        "QPushButton[groupDownload=\"true\"] { color: #333333; background: #efefef; border: 1px solid #c9c0c1; font-size: 12px; }"
        "QPushButton[groupDownload=\"true\"]:hover { background: #f7f3f3; }"
        "QPushButton[scriptSave=\"true\"] { color: #8b4a53; background: #ffffff; border: 1px solid #b76a72; font-size: 12px; }"
        "QPushButton[scriptSave=\"true\"]:hover { background: #fff6f6; }"
        "QPushButton[settingsPrimary=\"true\"], QPushButton[settingsConfirm=\"true\"] { color: #ffffff; background: #8b4a53; border: none; font-size: 12px; }"
        "QPushButton[settingsPrimary=\"true\"]:hover, QPushButton[settingsConfirm=\"true\"]:hover { background: #9a5660; }"
        "QPushButton[settingsSecondary=\"true\"] { color: #333333; background: #efefef; border: 1px solid #c9c0c1; font-size: 12px; }"
        "QPushButton[settingsSecondary=\"true\"]:hover { background: #f7f3f3; }"
        "QDateEdit, QLineEdit, QComboBox { border: 1px solid #cfcfcf; background: #ffffff; padding: 2px 6px; color: #222222; }"
        "QLineEdit[settingsField=\"true\"] { border: 1px solid #caaeb2; padding: 4px 6px; }"
        "QLineEdit[settingsField=\"true\"]:focus { border: 1px solid #8b4a53; }"
        "QListWidget { background: #faf7f7; border: none; border-right: 1px solid #d8d8d8; color: #333333; outline: none; }"
        "QListWidget::item { height: 40px; text-align: center; }"
        "QListWidget::item:selected { background: #f1ecec; color: #333333; }"
        "QCheckBox { color: #333333; spacing: 8px; }"
        "QCheckBox::indicator { width: 14px; height: 14px; border: 1px solid #b98990; background: #ffffff; }"
        "QCheckBox::indicator:checked { background: #8b4a53; border: 1px solid #8b4a53; }"
        "QPlainTextEdit { border: 1px solid #d8d8d8; background: #ffffff; color: #222222; }"
        "QTableWidget { border: none; background: #ffffff; color: #222222; gridline-color: #ead7d7; }"
        "QTableWidget::item { border-right: 1px solid #ead7d7; border-bottom: 1px solid #ead7d7; }"
        "QTableWidget#logTable { gridline-color: transparent; }"
        "QTableWidget#logTable::item { border: none; }"
        "QHeaderView::section { background: #f8e8e8; color: #333333; border: none; border-right: 1px solid #efcfcf; border-bottom: 1px solid #f1d6d6; padding: 3px 8px; }"
        "QLabel { color: #666666; }"
        "#gatewayIdValue { color: #d94a38; }"
        "QStatusBar { background: #f3f3f3; color: #666666; }"
        "#leftStatusLabel { color: #d94a38; }"
    );
}

void MainWindow::UpdateGatewayIdDisplay()
{
    const AppConfigValues values = AppConfig::Load();
    const QString gatewayId = values.gatewayId.trimmed().isEmpty() ? QStringLiteral("<empty>") : values.gatewayId.trimmed();
    setWindowTitle(QStringLiteral("7X 网关 - 网关标识: %1").arg(gatewayId));
    if (m_gatewayIdStatusLabel) {
        m_gatewayIdStatusLabel->setText(QStringLiteral("网关标识: %1").arg(gatewayId));
    }
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched->objectName() == QLatin1String("dragArea")) {
        if (event->type() == QEvent::MouseButtonPress) {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            if (mouseEvent->button() == Qt::LeftButton) {
                m_dragging = true;
                m_dragPosition = mouseEvent->globalPosition().toPoint() - frameGeometry().topLeft();
                return true;
            }
        } else if (event->type() == QEvent::MouseMove) {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            if (m_dragging && (mouseEvent->buttons() & Qt::LeftButton) && !isMaximized()) {
                move(mouseEvent->globalPosition().toPoint() - m_dragPosition);
                return true;
            }
        } else if (event->type() == QEvent::MouseButtonRelease) {
            m_dragging = false;
            return true;
        } else if (event->type() == QEvent::MouseButtonDblClick) {
            if (isMaximized()) {
                showNormal();
            } else {
                showMaximized();
            }
            return true;
        }
    }

    return QMainWindow::eventFilter(watched, event);
}

MainWindow::~MainWindow()
{
    delete ui;
}
