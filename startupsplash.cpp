#include "startupsplash.h"

#include <QApplication>
#include <QCoreApplication>
#include <QEventLoop>
#include <QGraphicsOpacityEffect>
#include <QGuiApplication>
#include <QLabel>
#include <QPropertyAnimation>
#include <QScreen>
#include <QVariantAnimation>
#include <QVBoxLayout>

StartupSplash::StartupSplash(QWidget *parent)
    : QWidget(parent)
{
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus);
    setAttribute(Qt::WA_TranslucentBackground, false);
    setFixedSize(420, 200);
    if (QScreen *screen = QGuiApplication::primaryScreen()) {
        const QRect ag = screen->availableGeometry();
        const QRect g = geometry();
        move(ag.center() - QPoint(g.width() / 2, g.height() / 2));
    }

    auto *panel = new QWidget(this);
    panel->setObjectName(QStringLiteral("startupSplashPanel"));
    panel->setStyleSheet(QStringLiteral(
        "#startupSplashPanel {"
        "  background-color: #1e1e24;"
        "  border-radius: 12px;"
        "  border: 1px solid #3d3d4a;"
        "}"));

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->addWidget(panel);

    auto *inner = new QVBoxLayout(panel);
    inner->setContentsMargins(28, 32, 28, 28);
    inner->setSpacing(16);

    m_titleLabel = new QLabel(QStringLiteral("网关客户端"), panel);
    m_titleLabel->setStyleSheet(QStringLiteral("color: #f0f0f5; font-size: 22px; font-weight: normal;"));
    m_titleLabel->setAlignment(Qt::AlignCenter);

    m_titleOpacityEffect = new QGraphicsOpacityEffect(m_titleLabel);
    m_titleOpacityEffect->setOpacity(1.0);
    m_titleLabel->setGraphicsEffect(m_titleOpacityEffect);

    m_titlePulseAnim = new QPropertyAnimation(m_titleOpacityEffect, "opacity", this);
    m_titlePulseAnim->setDuration(1100);
    m_titlePulseAnim->setStartValue(0.45);
    m_titlePulseAnim->setEndValue(1.0);
    m_titlePulseAnim->setEasingCurve(QEasingCurve::InOutSine);
    m_titlePulseAnim->setLoopCount(-1);
    m_titlePulseAnim->start();

    m_statusLabel = new QLabel(QStringLiteral("正在启动…"), panel);
    m_statusLabel->setStyleSheet(QStringLiteral("color: #a8a8b8; font-size: 13px;"));
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->setWordWrap(true);

    inner->addStretch(1);
    inner->addWidget(m_titleLabel);
    inner->addWidget(m_statusLabel);
    inner->addStretch(1);
}

void StartupSplash::setStatusText(const QString &text)
{
    if (m_statusLabel) {
        m_statusLabel->setText(text);
    }
}

void StartupSplash::pumpEvents() const
{
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
}

void StartupSplash::fadeOutAndThen(const std::function<void()> &onFinished)
{
    if (m_titlePulseAnim) {
        m_titlePulseAnim->stop();
    }

    auto *anim = new QVariantAnimation(this);
    anim->setDuration(320);
    anim->setStartValue(1.0);
    anim->setEndValue(0.0);
    anim->setEasingCurve(QEasingCurve::InOutQuad);
    QObject::connect(anim, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
        setWindowOpacity(v.toDouble());
    });
    QObject::connect(anim, &QVariantAnimation::finished, this, [this, onFinished, anim]() {
        hide();
        setWindowOpacity(1.0);
        if (onFinished) {
            onFinished();
        }
        anim->deleteLater();
    });
    anim->start();
}
