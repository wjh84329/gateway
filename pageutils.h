#ifndef PAGEUTILS_H
#define PAGEUTILS_H

#include <QAbstractItemView>
#include <QApplication>
#include <QDialog>
#include <QMouseEvent>
#include <QPoint>
#include <QtGlobal>
#include <QScreen>
#include <QGraphicsDropShadowEffect>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QToolButton>
#include <QStyle>
#include <QTableWidget>
#include <QShortcut>
#include <QSizePolicy>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

namespace UiHelpers {
/// Qt5：QMouseEvent::globalPos；Qt6：globalPosition().toPoint()
inline QPoint GlobalPosFromMouseEvent(const QMouseEvent *e)
{
    if (!e) {
        return {};
    }
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return e->globalPosition().toPoint();
#else
    return e->globalPos();
#endif
}

/// 将对话框置于 host 所属顶层窗口几何中心（与 parent=host 配合，保证叠放在主窗口之上）。
/// 调用前请先对无边框对话框执行 adjustSize()，否则尺寸未稳定会导致看似「不居中」。
inline void CenterDialogOnWindow(QWidget *dialog, QWidget *host)
{
    if (!dialog || !host) {
        return;
    }
    QWidget *win = host->window();
    if (!win) {
        return;
    }
    const QRect fr = win->frameGeometry();
    const QSize sz = dialog->size();
    QPoint pos = fr.center() - QPoint(sz.width() / 2, sz.height() / 2);
    if (QScreen *scr = win->screen()) {
        const QRect ag = scr->availableGeometry();
        pos.setX(qBound(ag.left(), pos.x(), qMax(ag.left(), ag.right() - sz.width() + 1)));
        pos.setY(qBound(ag.top(), pos.y(), qMax(ag.top(), ag.bottom() - sz.height() + 1)));
    }
    dialog->move(pos);
}

inline void CenterTableItems(QTableWidget *table)
{
    for (int row = 0; row < table->rowCount(); ++row) {
        for (int column = 0; column < table->columnCount(); ++column) {
            if (auto *item = table->item(row, column)) {
                item->setTextAlignment(Qt::AlignCenter);
            }
        }
    }
}

inline void ConfigureReadonlyTable(QTableWidget *table)
{
    table->setSelectionMode(QAbstractItemView::NoSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->verticalHeader()->setVisible(false);
    table->horizontalHeader()->setDefaultAlignment(Qt::AlignCenter);
    table->setShowGrid(true);
    table->setFocusPolicy(Qt::NoFocus);
}

inline QWidget *EnsurePageLoadingOverlay(QWidget *page, const QString &text, const QString &subText = QStringLiteral("正在请求并渲染数据"))
{
    if (!page) {
        return nullptr;
    }

    auto *overlay = page->findChild<QWidget *>(QStringLiteral("__pageLoadingOverlay"), Qt::FindDirectChildrenOnly);
    auto *label = page->findChild<QLabel *>(QStringLiteral("__pageLoadingLabel"), Qt::FindChildrenRecursively);
    auto *panel = page->findChild<QWidget *>(QStringLiteral("__pageLoadingPanel"), Qt::FindChildrenRecursively);
    auto *spinnerLabel = page->findChild<QLabel *>(QStringLiteral("__pageLoadingSpinnerLabel"), Qt::FindChildrenRecursively);
    auto *subLabel = page->findChild<QLabel *>(QStringLiteral("__pageLoadingSubLabel"), Qt::FindChildrenRecursively);
    auto *timer = page->findChild<QTimer *>(QStringLiteral("__pageLoadingSpinnerTimer"), Qt::FindChildrenRecursively);
    if (!overlay) {
        overlay = new QWidget(page);
        overlay->setObjectName(QStringLiteral("__pageLoadingOverlay"));
        overlay->setStyleSheet(QStringLiteral("background-color: rgba(248, 239, 240, 155);"));

        auto *layout = new QVBoxLayout(overlay);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);

        panel = new QWidget(overlay);
        panel->setObjectName(QStringLiteral("__pageLoadingPanel"));
        panel->setFixedSize(240, 138);
        panel->setStyleSheet(QStringLiteral("#%1 { background: rgba(255,255,255,250); border: 1px solid rgba(139,74,83,35); border-radius: 16px; }")
                                 .arg(panel->objectName()));

        auto *shadow = new QGraphicsDropShadowEffect(panel);
        shadow->setBlurRadius(24);
        shadow->setOffset(0, 10);
        shadow->setColor(QColor(139, 74, 83, 45));
        panel->setGraphicsEffect(shadow);

        auto *panelLayout = new QVBoxLayout(panel);
        panelLayout->setContentsMargins(26, 22, 26, 22);
        panelLayout->setSpacing(10);

        spinnerLabel = new QLabel(QStringLiteral("◐"), panel);
        spinnerLabel->setObjectName(QStringLiteral("__pageLoadingSpinnerLabel"));
        spinnerLabel->setAlignment(Qt::AlignCenter);
        spinnerLabel->setStyleSheet(QStringLiteral("color: #8f4a58; font-size: 28px; font-weight: 700; background: transparent;"));

        label = new QLabel(text, panel);
        label->setObjectName(QStringLiteral("__pageLoadingLabel"));
        label->setAlignment(Qt::AlignCenter);
        label->setStyleSheet(QStringLiteral("color: #7a3f49; font-size: 16px; font-weight: normal; background: transparent;"));

        subLabel = new QLabel(subText, panel);
        subLabel->setObjectName(QStringLiteral("__pageLoadingSubLabel"));
        subLabel->setAlignment(Qt::AlignCenter);
        subLabel->setStyleSheet(QStringLiteral("color: #9a7b81; font-size: 12px; background: transparent;"));

        timer = new QTimer(panel);
        timer->setObjectName(QStringLiteral("__pageLoadingSpinnerTimer"));
        QObject::connect(timer, &QTimer::timeout, panel, [spinnerLabel] {
            static const QStringList frames = {QStringLiteral("◐"), QStringLiteral("◓"), QStringLiteral("◑"), QStringLiteral("◒")};
            static int index = 0;
            spinnerLabel->setText(frames.at(index % frames.size()));
            ++index;
        });
        timer->start(120);

        panelLayout->addStretch();
        panelLayout->addWidget(spinnerLabel);
        panelLayout->addWidget(label);
        panelLayout->addWidget(subLabel);
        panelLayout->addStretch();

        layout->addStretch();
        layout->addWidget(panel, 0, Qt::AlignCenter);
        layout->addStretch();
    }

    if (label) {
        label->setText(text);
    }
    if (subLabel) {
        subLabel->setText(subText);
    }
    if (spinnerLabel) {
        spinnerLabel->setVisible(true);
    }
    if (timer) {
        if (!timer->isActive()) {
            timer->start(120);
        }
    }
    if (panel) {
        panel->raise();
    }
    overlay->setGeometry(page->rect());
    return overlay;
}

inline void SetPageLoading(QWidget *page,
                           bool loading,
                           const QString &text = QStringLiteral("加载中..."),
                           const QString &subText = QStringLiteral("正在请求并渲染数据"))
{
    auto *overlay = EnsurePageLoadingOverlay(page, text, subText);
    if (!overlay) {
        return;
    }

    overlay->setVisible(loading);
    if (loading) {
        overlay->raise();
    } else if (auto *timer = page->findChild<QTimer *>(QStringLiteral("__pageLoadingSpinnerTimer"), Qt::FindChildrenRecursively)) {
        timer->stop();
    }
    QApplication::processEvents();
}

inline void ShowStyledMessageBox(QWidget *parent,
                                 QMessageBox::Icon icon,
                                 const QString &title,
                                 const QString &text)
{
    QDialog dialog(parent);
    dialog.setModal(true);
    dialog.setFixedSize(260, 120);
    dialog.setWindowFlags(Qt::Window | Qt::Dialog | Qt::FramelessWindowHint | Qt::CustomizeWindowHint);
    dialog.setAttribute(Qt::WA_TranslucentBackground, true);
    dialog.setStyleSheet(QStringLiteral("QDialog { background: transparent; border: none; }"));

    auto *rootLayout = new QVBoxLayout(&dialog);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    auto *card = new QWidget(&dialog);
    card->setStyleSheet(QStringLiteral("background: #fff; border-radius: 14px; border: none;"));
    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(18, 16, 18, 16);
    cardLayout->setSpacing(10);

    auto *contentLayout = new QHBoxLayout();
    contentLayout->setSpacing(10);

    auto *iconLabel = new QLabel(card);
    iconLabel->setFixedSize(36, 36);
    iconLabel->setAlignment(Qt::AlignCenter);
    iconLabel->setStyleSheet(QStringLiteral("border-radius: 18px; color: white; font-size: 18px; font-weight: 700; border: none;"));
    switch (icon) {
    case QMessageBox::Information:
        iconLabel->setText(QStringLiteral("i"));
        iconLabel->setStyleSheet(iconLabel->styleSheet() + QStringLiteral("background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #6aa8e2, stop:1 #4f8fca);"));
        break;
    case QMessageBox::Warning:
        iconLabel->setText(QStringLiteral("!"));
        iconLabel->setStyleSheet(iconLabel->styleSheet() + QStringLiteral("background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #efbb57, stop:1 #d39a2f);"));
        break;
    case QMessageBox::Critical:
        iconLabel->setText(QStringLiteral("×"));
        iconLabel->setStyleSheet(iconLabel->styleSheet() + QStringLiteral("background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #e47a7a, stop:1 #cf5b5b);"));
        break;
    default:
        iconLabel->setText(QStringLiteral("•"));
        iconLabel->setStyleSheet(iconLabel->styleSheet() + QStringLiteral("background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #a45c67, stop:1 #8b4a53);"));
        break;
    }

    auto *textLabel = new QLabel(text, card);
    textLabel->setWordWrap(true);
    textLabel->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    textLabel->setStyleSheet(QStringLiteral("color: #5f5053; font-size: 14px; line-height: 1.4; background: transparent; border: none;"));

    contentLayout->addWidget(iconLabel, 0, Qt::AlignTop);
    contentLayout->addWidget(textLabel, 1);

    auto *buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    auto *okButton = new QPushButton(QStringLiteral("确定"), card);
    okButton->setCursor(Qt::PointingHandCursor);
    okButton->setFixedSize(80, 30);
    okButton->setStyleSheet(QStringLiteral(
        "QPushButton {"
        " color: #ffffff;"
        " background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #a15864, stop:1 #8b4a53);"
        " border: none;"
        " border-radius: 8px;"
        " font-size: 13px;"
        " font-weight: normal;"
        "}"
        "QPushButton:hover { background: #9a5660; }"
        "QPushButton:pressed { background: #7a3f49; }"));
    QObject::connect(okButton, &QPushButton::clicked, &dialog, &QDialog::accept);
    buttonLayout->addWidget(okButton);

    cardLayout->addLayout(contentLayout);
    cardLayout->addLayout(buttonLayout);
    rootLayout->addWidget(card, 0, Qt::AlignCenter);

    dialog.exec();
}

inline void ShowOverlayMessage(QWidget *page,
                              QMessageBox::Icon icon,
                              const QString &text,
                              const QString &buttonText = QStringLiteral("确定"))
{
    if (!page) return;
    // 1. 创建/查找遮罩
    auto *overlay = page->findChild<QWidget *>(QStringLiteral("__pageOverlayMessage"), Qt::FindDirectChildrenOnly);
    auto *panel = overlay ? overlay->findChild<QWidget *>(QStringLiteral("__pageOverlayPanel"), Qt::FindChildrenRecursively) : nullptr;
    auto *iconLabel = panel ? panel->findChild<QLabel *>(QStringLiteral("__pageOverlayIcon"), Qt::FindChildrenRecursively) : nullptr;
    auto *textLabel = panel ? panel->findChild<QLabel *>(QStringLiteral("__pageOverlayText"), Qt::FindChildrenRecursively) : nullptr;
    auto *button = panel ? panel->findChild<QPushButton *>(QStringLiteral("__pageOverlayButton"), Qt::FindChildrenRecursively) : nullptr;
    if (!overlay) {
        overlay = new QWidget(page);
        overlay->setObjectName("__pageOverlayMessage");
        overlay->setStyleSheet("background-color: rgba(248,239,240,180);");
        overlay->setGeometry(page->rect());
        overlay->raise();
        overlay->show();
        auto *layout = new QVBoxLayout(overlay);
        layout->setContentsMargins(0,0,0,0);
        layout->setSpacing(0);
        panel = new QWidget(overlay);
        panel->setObjectName("__pageOverlayPanel");
        panel->setFixedSize(260, 140);
        panel->setStyleSheet("background: #fff; border-radius: 16px; border: 1px solid #e5d2d5;");
        auto *panelLayout = new QVBoxLayout(panel);
        panelLayout->setContentsMargins(18, 18, 18, 18);
        panelLayout->setSpacing(12);
        iconLabel = new QLabel(panel);
        iconLabel->setObjectName("__pageOverlayIcon");
        iconLabel->setFixedSize(36, 36);
        iconLabel->setAlignment(Qt::AlignCenter);
        iconLabel->setStyleSheet("border-radius: 18px; color: white; font-size: 18px; font-weight: 700; border: none;");
        textLabel = new QLabel(panel);
        textLabel->setObjectName("__pageOverlayText");
        textLabel->setAlignment(Qt::AlignCenter);
        textLabel->setStyleSheet("color: #5f5053; font-size: 15px; background: transparent; border: none;");
        button = new QPushButton(buttonText, panel);
        button->setObjectName("__pageOverlayButton");
        button->setCursor(Qt::PointingHandCursor);
        button->setFixedSize(80, 30);
        button->setStyleSheet(
            "QPushButton { color: #fff; background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #a15864, stop:1 #8b4a53); border: none; border-radius: 8px; font-size: 13px; font-weight: normal; }"
            "QPushButton:hover { background: #9a5660; }"
            "QPushButton:pressed { background: #7a3f49; }"
        );
        QObject::connect(button, &QPushButton::clicked, overlay, [overlay]{ overlay->hide(); });
        panelLayout->addWidget(iconLabel, 0, Qt::AlignCenter);
        panelLayout->addWidget(textLabel, 0, Qt::AlignCenter);
        panelLayout->addWidget(button, 0, Qt::AlignCenter);
        layout->addStretch();
        layout->addWidget(panel, 0, Qt::AlignCenter);
        layout->addStretch();
    }
    // 2. 设置内容
    if (iconLabel) {
        switch (icon) {
        case QMessageBox::Information:
            iconLabel->setText(QStringLiteral("i"));
            iconLabel->setStyleSheet("border-radius: 18px; color: white; font-size: 18px; font-weight: 700; background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #6aa8e2, stop:1 #4f8fca);");
            break;
        case QMessageBox::Warning:
            iconLabel->setText(QStringLiteral("!"));
            iconLabel->setStyleSheet("border-radius: 18px; color: white; font-size: 18px; font-weight: 700; background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #efbb57, stop:1 #d39a2f);");
            break;
        case QMessageBox::Critical:
            iconLabel->setText(QStringLiteral("×"));
            iconLabel->setStyleSheet("border-radius: 18px; color: white; font-size: 18px; font-weight: 700; background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #e47a7a, stop:1 #cf5b5b);");
            break;
        default:
            iconLabel->setText(QStringLiteral("•"));
            iconLabel->setStyleSheet("border-radius: 18px; color: white; font-size: 18px; font-weight: 700; background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #a45c67, stop:1 #8b4a53);");
            break;
        }
    }
    if (textLabel) textLabel->setText(text);
    if (button) button->setText(buttonText);
    overlay->setGeometry(page->rect());
    overlay->raise();
    overlay->show();
    QApplication::processEvents();
}

/// 与主界面一致的酒红主题卡片提示（标题显示在卡片顶部，避免系统 MessageBox 双标题栏问题）
inline void ShowThemedAlert(QWidget *parent, QMessageBox::Icon icon, const QString &title, const QString &text)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(title);
    dlg.setModal(true);
    dlg.setWindowFlags(Qt::Window | Qt::Dialog | Qt::FramelessWindowHint);
    dlg.setAttribute(Qt::WA_TranslucentBackground, true);

    auto *outer = new QVBoxLayout(&dlg);
    outer->setContentsMargins(18, 18, 18, 18);

    auto *card = new QWidget(&dlg);
    card->setObjectName(QStringLiteral("themedAlertCard"));
    card->setStyleSheet(QStringLiteral(
        "QWidget#themedAlertCard { background: #ffffff; border-radius: 14px; border: 1px solid #e8d4d8; }"));

    auto *shadow = new QGraphicsDropShadowEffect(card);
    shadow->setBlurRadius(26);
    shadow->setOffset(0, 10);
    shadow->setColor(QColor(139, 74, 83, 50));
    card->setGraphicsEffect(shadow);

    auto *vl = new QVBoxLayout(card);
    vl->setContentsMargins(18, 14, 18, 16);
    vl->setSpacing(12);

    auto *titleRow = new QHBoxLayout;
    auto *titleLbl = new QLabel(title, card);
    titleLbl->setStyleSheet(QStringLiteral(
        "font-size: 15px; font-weight: normal; color: #8b4a53; background: transparent; border: none;"));
    auto *closeBtn = new QToolButton(card);
    closeBtn->setObjectName(QStringLiteral("themedAlertClose"));
    closeBtn->setText(QStringLiteral("×"));
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setAutoRaise(true);
    closeBtn->setStyleSheet(QStringLiteral(
        "QToolButton#themedAlertClose { color: #8a6d72; border: none; font-size: 18px; font-weight: normal; padding: 0 4px; background: transparent; }"
        "QToolButton#themedAlertClose:hover { color: #8b4a53; }"));
    titleRow->addWidget(titleLbl);
    titleRow->addStretch();
    titleRow->addWidget(closeBtn, 0, Qt::AlignTop);
    vl->addLayout(titleRow);

    auto *contentRow = new QHBoxLayout;
    contentRow->setSpacing(12);
    auto *iconLabel = new QLabel(card);
    iconLabel->setFixedSize(36, 36);
    iconLabel->setAlignment(Qt::AlignCenter);
    iconLabel->setStyleSheet(QStringLiteral("border-radius: 18px; color: white; font-size: 17px; font-weight: 700; border: none;"));
    switch (icon) {
    case QMessageBox::Information:
        iconLabel->setText(QStringLiteral("i"));
        iconLabel->setStyleSheet(iconLabel->styleSheet()
                                 + QStringLiteral("background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #6aa8e2, stop:1 #4f8fca);"));
        break;
    case QMessageBox::Warning:
        iconLabel->setText(QStringLiteral("!"));
        iconLabel->setStyleSheet(iconLabel->styleSheet()
                                 + QStringLiteral("background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #efbb57, stop:1 #d39a2f);"));
        break;
    case QMessageBox::Critical:
        iconLabel->setText(QStringLiteral("×"));
        iconLabel->setStyleSheet(iconLabel->styleSheet()
                                 + QStringLiteral("background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #e47a7a, stop:1 #cf5b5b);"));
        break;
    default:
        iconLabel->setText(QStringLiteral("?"));
        iconLabel->setStyleSheet(iconLabel->styleSheet()
                                 + QStringLiteral("background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #a45c67, stop:1 #8b4a53);"));
        break;
    }
    auto *textLbl = new QLabel(text, card);
    textLbl->setWordWrap(true);
    textLbl->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    textLbl->setStyleSheet(QStringLiteral(
        "color: #4a3d3f; font-size: 13px; line-height: 1.45; background: transparent; border: none;"));
    textLbl->setMinimumWidth(360);
    textLbl->setMaximumWidth(500);
    contentRow->addWidget(iconLabel, 0, Qt::AlignTop);
    contentRow->addWidget(textLbl, 1);
    vl->addLayout(contentRow);

    auto *btnRow = new QHBoxLayout;
    btnRow->addStretch();
    auto *okBtn = new QPushButton(QStringLiteral("确定"), card);
    okBtn->setDefault(true);
    okBtn->setCursor(Qt::PointingHandCursor);
    okBtn->setMinimumSize(92, 30);
    okBtn->setStyleSheet(QStringLiteral(
        "QPushButton { color: #ffffff; background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #a15864, stop:1 #8b4a53);"
        " border: none; border-radius: 8px; font-size: 13px; font-weight: normal; }"
        "QPushButton:hover { background: #9a5660; }"
        "QPushButton:pressed { background: #7a3f49; }"));
    btnRow->addWidget(okBtn);
    vl->addLayout(btnRow);

    outer->addWidget(card);

    QObject::connect(closeBtn, &QToolButton::clicked, &dlg, &QDialog::accept);
    QObject::connect(okBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    auto *esc = new QShortcut(QKeySequence(Qt::Key_Escape), &dlg);
    QObject::connect(esc, &QShortcut::activated, &dlg, &QDialog::accept);

    dlg.adjustSize();
    CenterDialogOnWindow(&dlg, parent);
    dlg.exec();
}

/// 主题「是 / 否」确认框；标题在卡片内展示，与 Alert 一致。返回 true 表示「是」。
inline bool ShowThemedQuestion(QWidget *parent, const QString &title, const QString &text)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(title);
    dlg.setModal(true);
    dlg.setWindowFlags(Qt::Window | Qt::Dialog | Qt::FramelessWindowHint);
    dlg.setAttribute(Qt::WA_TranslucentBackground, true);

    auto *outer = new QVBoxLayout(&dlg);
    outer->setContentsMargins(18, 18, 18, 18);

    auto *card = new QWidget(&dlg);
    card->setObjectName(QStringLiteral("themedQuestionCard"));
    card->setStyleSheet(QStringLiteral(
        "QWidget#themedQuestionCard { background: #ffffff; border-radius: 14px; border: 1px solid #e8d4d8; }"));

    auto *shadow = new QGraphicsDropShadowEffect(card);
    shadow->setBlurRadius(26);
    shadow->setOffset(0, 10);
    shadow->setColor(QColor(139, 74, 83, 50));
    card->setGraphicsEffect(shadow);

    auto *vl = new QVBoxLayout(card);
    vl->setContentsMargins(18, 14, 18, 16);
    vl->setSpacing(12);

    auto *titleRow = new QHBoxLayout;
    auto *titleLbl = new QLabel(title, card);
    titleLbl->setStyleSheet(QStringLiteral(
        "font-size: 15px; font-weight: normal; color: #8b4a53; background: transparent; border: none;"));
    auto *closeBtn = new QToolButton(card);
    closeBtn->setObjectName(QStringLiteral("themedQuestionClose"));
    closeBtn->setText(QStringLiteral("×"));
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setAutoRaise(true);
    closeBtn->setStyleSheet(QStringLiteral(
        "QToolButton#themedQuestionClose { color: #8a6d72; border: none; font-size: 18px; font-weight: normal; padding: 0 4px; background: transparent; }"
        "QToolButton#themedQuestionClose:hover { color: #8b4a53; }"));
    titleRow->addWidget(titleLbl);
    titleRow->addStretch();
    titleRow->addWidget(closeBtn, 0, Qt::AlignTop);
    vl->addLayout(titleRow);

    auto *contentRow = new QHBoxLayout;
    contentRow->setSpacing(12);
    auto *iconLabel = new QLabel(card);
    iconLabel->setFixedSize(36, 36);
    iconLabel->setAlignment(Qt::AlignCenter);
    iconLabel->setText(QStringLiteral("?"));
    iconLabel->setStyleSheet(QStringLiteral(
        "border-radius: 18px; color: white; font-size: 17px; font-weight: 700; border: none;"
        "background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #a45c67, stop:1 #8b4a53);"));
    auto *textLbl = new QLabel(text, card);
    textLbl->setWordWrap(true);
    textLbl->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    textLbl->setStyleSheet(QStringLiteral(
        "color: #4a3d3f; font-size: 13px; line-height: 1.45; background: transparent; border: none;"));
    textLbl->setMinimumWidth(360);
    textLbl->setMaximumWidth(500);
    contentRow->addWidget(iconLabel, 0, Qt::AlignTop);
    contentRow->addWidget(textLbl, 1);
    vl->addLayout(contentRow);

    auto *btnRow = new QHBoxLayout;
    btnRow->addStretch();
    auto *yesBtn = new QPushButton(QStringLiteral("是"), card);
    yesBtn->setDefault(true);
    yesBtn->setCursor(Qt::PointingHandCursor);
    yesBtn->setMinimumSize(88, 30);
    yesBtn->setStyleSheet(QStringLiteral(
        "QPushButton { color: #ffffff; background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #a15864, stop:1 #8b4a53);"
        " border: none; border-radius: 8px; font-size: 13px; font-weight: normal; }"
        "QPushButton:hover { background: #9a5660; }"
        "QPushButton:pressed { background: #7a3f49; }"));
    auto *noBtn = new QPushButton(QStringLiteral("否"), card);
    noBtn->setCursor(Qt::PointingHandCursor);
    noBtn->setMinimumSize(88, 30);
    noBtn->setStyleSheet(QStringLiteral(
        "QPushButton { color: #333333; background: #efefef; border: 1px solid #c9c0c1; border-radius: 8px;"
        " font-size: 13px; font-weight: normal; }"
        "QPushButton:hover { background: #f7f3f3; }"));
    // 左「是」右「否」（与常见中文桌面习惯一致）
    btnRow->addWidget(yesBtn);
    btnRow->addSpacing(10);
    btnRow->addWidget(noBtn);
    vl->addLayout(btnRow);

    outer->addWidget(card);

    QObject::connect(closeBtn, &QToolButton::clicked, &dlg, &QDialog::reject);
    QObject::connect(noBtn, &QPushButton::clicked, &dlg, &QDialog::reject);
    QObject::connect(yesBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    auto *esc = new QShortcut(QKeySequence(Qt::Key_Escape), &dlg);
    QObject::connect(esc, &QShortcut::activated, &dlg, &QDialog::reject);

    dlg.adjustSize();
    CenterDialogOnWindow(&dlg, parent);
    return dlg.exec() == QDialog::Accepted;
}

/// 在线更新等场景：无边框卡片 + 进度条 + 取消（与 ShowThemedAlert 同色）。输出控件由参数返回，事件由调用方连接。
inline void SetupThemedDownloadProgressDialog(QDialog *dlg,
                                              QWidget *centerHost,
                                              const QString &windowTitle,
                                              const QString &headerTitle,
                                              const QString &statusText,
                                              QProgressBar *&progressBarOut,
                                              QLabel *&percentLabelOut,
                                              QPushButton *&cancelButtonOut,
                                              QToolButton *&closeButtonOut)
{
    if (!dlg) {
        return;
    }
    progressBarOut = nullptr;
    percentLabelOut = nullptr;
    cancelButtonOut = nullptr;
    closeButtonOut = nullptr;

    dlg->setWindowTitle(windowTitle);
    dlg->setModal(true);
    dlg->setWindowFlags(Qt::Window | Qt::Dialog | Qt::FramelessWindowHint);
    dlg->setAttribute(Qt::WA_TranslucentBackground, true);

    auto *outer = new QVBoxLayout(dlg);
    outer->setContentsMargins(18, 18, 18, 18);

    auto *card = new QWidget(dlg);
    card->setObjectName(QStringLiteral("themedProgressCard"));
    card->setStyleSheet(QStringLiteral(
        "QWidget#themedProgressCard { background: #ffffff; border-radius: 14px; border: 1px solid #e8d4d8; }"));

    auto *shadow = new QGraphicsDropShadowEffect(card);
    shadow->setBlurRadius(26);
    shadow->setOffset(0, 10);
    shadow->setColor(QColor(139, 74, 83, 50));
    card->setGraphicsEffect(shadow);

    auto *vl = new QVBoxLayout(card);
    vl->setContentsMargins(18, 14, 18, 16);
    vl->setSpacing(14);

    auto *titleRow = new QHBoxLayout;
    auto *titleLbl = new QLabel(headerTitle, card);
    titleLbl->setStyleSheet(QStringLiteral(
        "font-size: 15px; font-weight: normal; color: #8b4a53; background: transparent; border: none;"));
    closeButtonOut = new QToolButton(card);
    closeButtonOut->setObjectName(QStringLiteral("themedProgressClose"));
    closeButtonOut->setText(QStringLiteral("×"));
    closeButtonOut->setCursor(Qt::PointingHandCursor);
    closeButtonOut->setAutoRaise(true);
    closeButtonOut->setStyleSheet(QStringLiteral(
        "QToolButton#themedProgressClose { color: #8a6d72; border: none; font-size: 18px; font-weight: normal; padding: 0 4px; background: transparent; }"
        "QToolButton#themedProgressClose:hover { color: #8b4a53; }"));
    titleRow->addWidget(titleLbl);
    titleRow->addStretch();
    titleRow->addWidget(closeButtonOut, 0, Qt::AlignTop);
    vl->addLayout(titleRow);

    auto *msg = new QLabel(statusText, card);
    msg->setStyleSheet(QStringLiteral(
        "color: #4a3d3f; font-size: 13px; background: transparent; border: none;"));
    msg->setWordWrap(true);
    msg->setMinimumWidth(360);
    vl->addWidget(msg);

    auto *barCol = new QVBoxLayout;
    barCol->setSpacing(6);
    barCol->setContentsMargins(0, 0, 0, 0);
    progressBarOut = new QProgressBar(card);
    progressBarOut->setRange(0, 100);
    progressBarOut->setValue(0);
    progressBarOut->setTextVisible(false);
    progressBarOut->setFixedHeight(22);
    progressBarOut->setMinimumWidth(360);
    progressBarOut->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    progressBarOut->setStyleSheet(QStringLiteral(
        "QProgressBar { border: 1px solid #e4cfd3; border-radius: 8px; background: #f6f0f1; }"
        "QProgressBar::chunk { background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #b56d78, stop:1 #8b4a53); border-radius: 7px; }"));
    percentLabelOut = new QLabel(QStringLiteral("0%"), card);
    percentLabelOut->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    percentLabelOut->setWordWrap(true);
    percentLabelOut->setMinimumWidth(360);
    percentLabelOut->setStyleSheet(QStringLiteral(
        "color: #6b5256; font-size: 13px; font-weight: normal; border: none; background: transparent;"));
    barCol->addWidget(progressBarOut);
    barCol->addWidget(percentLabelOut);
    vl->addLayout(barCol);

    auto *btnRow = new QHBoxLayout;
    btnRow->addStretch();
    cancelButtonOut = new QPushButton(QStringLiteral("取消"), card);
    cancelButtonOut->setCursor(Qt::PointingHandCursor);
    cancelButtonOut->setMinimumSize(88, 30);
    cancelButtonOut->setStyleSheet(QStringLiteral(
        "QPushButton { color: #333333; background: #efefef; border: 1px solid #c9c0c1; border-radius: 8px; font-size: 13px; font-weight: normal; }"
        "QPushButton:hover { background: #f7f3f3; }"));
    btnRow->addWidget(cancelButtonOut);
    vl->addLayout(btnRow);

    outer->addWidget(card);

    dlg->adjustSize();
    CenterDialogOnWindow(dlg, centerHost);
}
}

#endif // PAGEUTILS_H
