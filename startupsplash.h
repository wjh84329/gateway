#ifndef STARTUPSPLASH_H
#define STARTUPSPLASH_H

#include <functional>

#include <QString>
#include <QWidget>

class QLabel;
class QGraphicsOpacityEffect;
class QPropertyAnimation;

/// 主窗口显示前的启动过渡界面（轻量动画 + 状态文案），避免长时间白屏等待。
class StartupSplash : public QWidget
{
    Q_OBJECT
public:
    explicit StartupSplash(QWidget *parent = nullptr);
    void setStatusText(const QString &text);
    /// 在耗时启动步骤之间调用，驱动界面动画刷新。
    void pumpEvents() const;
    /// 淡出后调用 onFinished（在动画结束、事件循环运行中触发）。
    void fadeOutAndThen(const std::function<void()> &onFinished);

private:
    QLabel *m_titleLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QGraphicsOpacityEffect *m_titleOpacityEffect = nullptr;
    QPropertyAnimation *m_titlePulseAnim = nullptr;
};

#endif // STARTUPSPLASH_H
