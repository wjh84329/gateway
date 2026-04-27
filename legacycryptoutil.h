#ifndef LEGACYCRYPTOUTIL_H
#define LEGACYCRYPTOUTIL_H

#include <QString>

namespace LegacyCryptoUtil {
QString DecryptRijndaelBase64(const QString &cipherText, const QString &passPhrase, bool *ok = nullptr);
}

#endif // LEGACYCRYPTOUTIL_H
