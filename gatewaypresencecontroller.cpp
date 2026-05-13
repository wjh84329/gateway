#include "gatewaypresencecontroller.h"

#include "gatewayapiclient.h"

namespace {
constexpr int kIntervalMs = 60 * 1000;
}

GatewayPresenceController::GatewayPresenceController(QObject *parent)
    : QObject(parent)
{
    m_timer.setInterval(kIntervalMs);
    m_timer.setSingleShot(false);
    connect(&m_timer, &QTimer::timeout, this, &GatewayPresenceController::onTick);
}

void GatewayPresenceController::start()
{
    onTick();
    if (!m_timer.isActive()) {
        m_timer.start();
    }
}

void GatewayPresenceController::stop()
{
    m_timer.stop();
}

void GatewayPresenceController::onTick()
{
    QString err;
    GatewayApiClient api;
    api.PingGatewayPresence(&err);
    Q_UNUSED(err);
}
