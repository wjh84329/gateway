#ifndef RABBITMQDISPATCHER_H
#define RABBITMQDISPATCHER_H

#include <QString>

namespace RabbitMqDispatcher {
bool HandleMessage(const QString &payload);
}

#endif // RABBITMQDISPATCHER_H
