#include <QByteArray>
#include <QCoreApplication>
#include <QFile>
#include <QString>
#include <QTextStream>

#include <cstdio>

#include "qrcodeencoder.h"
#include "qrcodeencoder_matrix_build.h"

#include <cstring>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    int argBase = 1;
    if (argc > 1 && std::strcmp(argv[1], "--matrix") == 0) {
        argBase = 2;
    }

    const QString url = QString::fromUtf8(argc > argBase ? argv[argBase]
                                                       : "https://api.feixpay.cn/Call/Hlbbpay/wxRawPub?pay_order_id=202605120050421912");
    const int serial = argc > argBase + 1 ? QString::fromUtf8(argv[argBase + 1]).toInt() : 4;
    const QString resourceCode = argc > argBase + 2 ? QString::fromUtf8(argv[argBase + 2]) : QStringLiteral("29");
    const QString imageCode = argc > argBase + 3 ? QString::fromUtf8(argv[argBase + 3]) : QStringLiteral("44");
    const int xOffset = argc > argBase + 4 ? QString::fromUtf8(argv[argBase + 4]).toInt() : 10;
    const int yOffset = argc > argBase + 5 ? QString::fromUtf8(argv[argBase + 5]).toInt() : 10;
    const QString outPath = argc > argBase + 6 ? QString::fromUtf8(argv[argBase + 6]) : QString();

    if (argc > 1 && std::strcmp(argv[1], "--matrix") == 0) {
        QString mErr;
        const QVector<QVector<int>> mod = QrMatrixBuildQrcoder160(url, serial > 0 ? serial : 6, &mErr);
        if (mod.isEmpty()) {
            QTextStream es(stderr);
            es << (mErr.isEmpty() ? QStringLiteral("matrix failed") : mErr) << "\n";
            return 2;
        }
        QString lines;
        lines.reserve(mod.size() * (mod.size() + 1));
        for (int y = 0; y < mod.size(); ++y) {
            for (int x = 0; x < mod[y].size(); ++x) {
                lines += mod[y][x] ? QLatin1Char('1') : QLatin1Char('0');
            }
            lines += QLatin1Char('\n');
        }
        const QByteArray utf8 = lines.toUtf8();
        if (!outPath.isEmpty()) {
            QFile f(outPath);
            if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                return 3;
            }
            if (f.write(utf8) != utf8.size()) {
                return 3;
            }
            return 0;
        }
        if (std::fwrite(utf8.constData(), 1, static_cast<size_t>(utf8.size()), stdout)
            != static_cast<size_t>(utf8.size())) {
            return 3;
        }
        return 0;
    }

    QString err;
    const QString out = QrCodeEncoder::GenerateLegacyMirText(url,
                                                             resourceCode,
                                                             imageCode,
                                                             serial,
                                                             xOffset,
                                                             yOffset,
                                                             &err);
    if (out.isEmpty()) {
        QTextStream errStream(stderr);
        errStream << err << "\n";
        return 2;
    }

    const QByteArray utf8 = out.toUtf8();
    if (!outPath.isEmpty()) {
        QFile f(outPath);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            QTextStream errStream(stderr);
            errStream << "cannot write " << outPath << "\n";
            return 3;
        }
        if (f.write(utf8) != utf8.size()) {
            return 3;
        }
        return 0;
    }

    if (std::fwrite(utf8.constData(), 1, static_cast<size_t>(utf8.size()), stdout) != static_cast<size_t>(utf8.size())) {
        return 3;
    }
    return 0;
}
