#ifndef PAGEUTILS_H
#define PAGEUTILS_H

#include <QAbstractItemView>
#include <QApplication>
#include <QDialog>
#include <QGraphicsDropShadowEffect>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QToolButton>
#include <QStyle>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

namespace UiHelpers {
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
        label->setStyleSheet(QStringLiteral("color: #7a3f49; font-size: 16px; font-weight: 600; background: transparent;"));

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
    dialog.setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint | Qt::CustomizeWindowHint);
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
        " font-weight: 600;"
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
            "QPushButton { color: #fff; background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #a15864, stop:1 #8b4a53); border: none; border-radius: 8px; font-size: 13px; font-weight: 600; }"
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
}

#endif // PAGEUTILS_H
