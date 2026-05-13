#ifndef GATEWAYPRESENCECONTROLLER_H
#define GATEWAYPRESENCECONTROLLER_H

#include <QObject>
#include <QTimer>

/// 定时向平台 POST /api/Client/GatewayEndpoint/Ping，与后台 LastPresenceUtc 宽限配合判定在线。
class GatewayPresenceController final : public QObject
{
    Q_OBJECT
public:
    explicit GatewayPresenceController(QObject *parent = nullptr);
    void start();
    void stop();

private slots:
    void onTick();

private:
    QTimer m_timer;
};

#endif // GATEWAYPRESENCECONTROLLER_H
