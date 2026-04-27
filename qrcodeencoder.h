#ifndef QRCODEENCODER_H
#define QRCODEENCODER_H

#include <QString>

namespace QrCodeEncoder {
QString GenerateLegacyMirText(const QString &data,
                              const QString &resourceCode,
                              const QString &imageCode,
                              int serial,
                              int xOffset,
                              int yOffset,
                              QString *errorMessage = nullptr);
}

#endif // QRCODEENCODER_H
