#ifndef INSTALLSCRIPTPROCESSOR_H
#define INSTALLSCRIPTPROCESSOR_H

#include <QJsonObject>
#include <QString>

namespace InstallScriptProcessor {
bool Process(const QJsonObject &dataObject, QString *errorMessage = nullptr);
}

#endif // INSTALLSCRIPTPROCESSOR_H
