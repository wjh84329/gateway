#include "legacycryptoutil.h"

#include <QByteArray>
#include <QCryptographicHash>

#ifdef Q_OS_WIN
#include <windows.h>
#include <bcrypt.h>
#include <cwchar>
#pragma comment(lib, "bcrypt.lib")
#endif

namespace {
#ifdef Q_OS_WIN
QByteArray Sha1Hash(const QByteArray &data, bool *ok)
{
    BCRYPT_ALG_HANDLE algorithmHandle = nullptr;
    BCRYPT_HASH_HANDLE hashHandle = nullptr;
    QByteArray hashObject;
    QByteArray hash;
    ULONG objectLength = 0;
    ULONG hashLength = 0;
    ULONG bytesCopied = 0;

    if (BCryptOpenAlgorithmProvider(&algorithmHandle, BCRYPT_SHA1_ALGORITHM, nullptr, 0) != 0) {
        if (ok) {
            *ok = false;
        }
        return {};
    }

    if (BCryptGetProperty(algorithmHandle, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength), &bytesCopied, 0) != 0
        || BCryptGetProperty(algorithmHandle, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&hashLength), sizeof(hashLength), &bytesCopied, 0) != 0) {
        BCryptCloseAlgorithmProvider(algorithmHandle, 0);
        if (ok) {
            *ok = false;
        }
        return {};
    }

    hashObject.resize(static_cast<int>(objectLength));
    hash.resize(static_cast<int>(hashLength));

    if (BCryptCreateHash(algorithmHandle,
                         &hashHandle,
                         reinterpret_cast<PUCHAR>(hashObject.data()),
                         objectLength,
                         nullptr,
                         0,
                         0) != 0
        || BCryptHashData(hashHandle, reinterpret_cast<PUCHAR>(const_cast<char *>(data.constData())), static_cast<ULONG>(data.size()), 0) != 0
        || BCryptFinishHash(hashHandle, reinterpret_cast<PUCHAR>(hash.data()), hashLength, 0) != 0) {
        if (hashHandle) {
            BCryptDestroyHash(hashHandle);
        }
        BCryptCloseAlgorithmProvider(algorithmHandle, 0);
        if (ok) {
            *ok = false;
        }
        return {};
    }

    BCryptDestroyHash(hashHandle);
    BCryptCloseAlgorithmProvider(algorithmHandle, 0);
    if (ok) {
        *ok = true;
    }
    return hash;
}

QByteArray DerivePasswordBytes(const QString &passPhrase, bool *ok)
{
    constexpr int iterations = 100;
    const QByteArray password = passPhrase.toUtf8();
    QByteArray baseValue = QCryptographicHash::hash(password, QCryptographicHash::Sha1);
    for (int iteration = 1; iteration < iterations - 1; ++iteration) {
        baseValue = QCryptographicHash::hash(baseValue, QCryptographicHash::Sha1);
    }

    QByteArray keyBytes;
    keyBytes.reserve(32);
    int prefix = 0;
    while (keyBytes.size() < 32) {
        QByteArray prefixedBaseValue;
        if (prefix > 0) {
            prefixedBaseValue.append(QByteArray::number(prefix));
        }
        prefixedBaseValue.append(baseValue);

        const QByteArray hash = QCryptographicHash::hash(prefixedBaseValue, QCryptographicHash::Sha1);
        keyBytes.append(hash.left(qMin(32 - keyBytes.size(), hash.size())));
        ++prefix;
    }

    if (ok) {
        *ok = true;
    }
    return keyBytes;
}

QByteArray DecryptAes256Cbc(const QByteArray &cipherBytes, const QByteArray &keyBytes, bool *ok)
{
    BCRYPT_ALG_HANDLE algorithmHandle = nullptr;
    BCRYPT_KEY_HANDLE keyHandle = nullptr;
    QByteArray keyObject;
    ULONG objectLength = 0;
    ULONG bytesCopied = 0;
    ULONG resultLength = 0;
    QByteArray iv = QByteArrayLiteral("hdfgail9xyzgzl88");
    QByteArray plainBytes;

    if (BCryptOpenAlgorithmProvider(&algorithmHandle, BCRYPT_AES_ALGORITHM, nullptr, 0) != 0) {
        if (ok) {
            *ok = false;
        }
        return {};
    }

    if (BCryptSetProperty(algorithmHandle,
                          BCRYPT_CHAINING_MODE,
                          reinterpret_cast<PUCHAR>(const_cast<wchar_t *>(BCRYPT_CHAIN_MODE_CBC)),
                          static_cast<ULONG>((std::wcslen(BCRYPT_CHAIN_MODE_CBC) + 1) * sizeof(wchar_t)),
                          0) != 0
        || BCryptGetProperty(algorithmHandle, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength), &bytesCopied, 0) != 0) {
        BCryptCloseAlgorithmProvider(algorithmHandle, 0);
        if (ok) {
            *ok = false;
        }
        return {};
    }

    keyObject.resize(static_cast<int>(objectLength));
    if (BCryptGenerateSymmetricKey(algorithmHandle,
                                   &keyHandle,
                                   reinterpret_cast<PUCHAR>(keyObject.data()),
                                   objectLength,
                                   reinterpret_cast<PUCHAR>(const_cast<char *>(keyBytes.constData())),
                                   static_cast<ULONG>(keyBytes.size()),
                                   0) != 0) {
        BCryptCloseAlgorithmProvider(algorithmHandle, 0);
        if (ok) {
            *ok = false;
        }
        return {};
    }

    QByteArray ivProbe = iv;
    if (BCryptDecrypt(keyHandle,
                      reinterpret_cast<PUCHAR>(const_cast<char *>(cipherBytes.constData())),
                      static_cast<ULONG>(cipherBytes.size()),
                      nullptr,
                      reinterpret_cast<PUCHAR>(ivProbe.data()),
                      static_cast<ULONG>(ivProbe.size()),
                      nullptr,
                      0,
                      &resultLength,
                      BCRYPT_BLOCK_PADDING) != 0) {
        BCryptDestroyKey(keyHandle);
        BCryptCloseAlgorithmProvider(algorithmHandle, 0);
        if (ok) {
            *ok = false;
        }
        return {};
    }

    plainBytes.resize(static_cast<int>(resultLength));
    QByteArray ivDecrypt = iv;
    if (BCryptDecrypt(keyHandle,
                      reinterpret_cast<PUCHAR>(const_cast<char *>(cipherBytes.constData())),
                      static_cast<ULONG>(cipherBytes.size()),
                      nullptr,
                      reinterpret_cast<PUCHAR>(ivDecrypt.data()),
                      static_cast<ULONG>(ivDecrypt.size()),
                      reinterpret_cast<PUCHAR>(plainBytes.data()),
                      static_cast<ULONG>(plainBytes.size()),
                      &resultLength,
                      BCRYPT_BLOCK_PADDING) != 0) {
        BCryptDestroyKey(keyHandle);
        BCryptCloseAlgorithmProvider(algorithmHandle, 0);
        if (ok) {
            *ok = false;
        }
        return {};
    }

    plainBytes.truncate(static_cast<int>(resultLength));
    BCryptDestroyKey(keyHandle);
    BCryptCloseAlgorithmProvider(algorithmHandle, 0);
    if (ok) {
        *ok = true;
    }
    return plainBytes;
}

#endif
}

QString LegacyCryptoUtil::DecryptRijndaelBase64(const QString &cipherText, const QString &passPhrase, bool *ok)
{
#ifdef Q_OS_WIN
    bool localOk = false;
    const QByteArray cipherBytes = QByteArray::fromBase64(cipherText.toUtf8());
    if (cipherBytes.isEmpty()) {
        if (ok) {
            *ok = false;
        }
        return {};
    }

    const QByteArray keyBytes = DerivePasswordBytes(passPhrase, &localOk);
    if (!localOk) {
        if (ok) {
            *ok = false;
        }
        return {};
    }

    const QByteArray plainBytes = DecryptAes256Cbc(cipherBytes, keyBytes, &localOk);
    if (ok) {
        *ok = localOk;
    }
    return localOk ? QString::fromUtf8(plainBytes) : QString();
#else
    Q_UNUSED(cipherText);
    Q_UNUSED(passPhrase);
    if (ok) {
        *ok = false;
    }
    return {};
#endif
}
