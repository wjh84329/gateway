#ifndef RECHARGEPROCESSOR_H
#define RECHARGEPROCESSOR_H

#include <QJsonObject>
#include <QString>

namespace RechargeProcessor {
bool Process(const QJsonObject &rechargeObject,
             const QString &operation,
             QString *errorMessage = nullptr);
}

#endif // RECHARGEPROCESSOR_H
