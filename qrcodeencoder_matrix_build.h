#ifndef QRCODEENCODER_MATRIX_BUILD_H
#define QRCODEENCODER_MATRIX_BUILD_H

#include <QVector>
#include <QString>

/// 与 NuGet QRCoder 1.6.0（老网关）一致的掩码后模块矩阵：边长 = 21+4*(version-1)，元素 0/1。
QVector<QVector<int>> QrMatrixBuildQrcoder160(const QString &utf8Payload,
                                              int version,
                                              QString *errorMessage);

#endif
