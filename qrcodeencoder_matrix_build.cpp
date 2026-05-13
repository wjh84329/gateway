#include "qrcodeencoder_matrix_build.h"

#include <QSet>
#include <QString>
#include <QVector>
#include <climits>
#include <cmath>

namespace {

struct Vi {
    int version;
    int size;
    int dataCodewords;
    int eccPerBlock;
    int numBlocks;
};

const Vi kV[] = {
    {3, 29, 55, 15, 1},
    {4, 33, 80, 20, 1},
    {5, 37, 108, 26, 1},
    {6, 41, 136, 18, 2},
};

const Vi *findVi(int version)
{
    for (const auto &v : kV) {
        if (v.version == version) {
            return &v;
        }
    }
    return nullptr;
}

// QRCoder QRCodeGenerator._remainderBits（version 1..40）
const int kRemainderBits[40] = {0, 7, 7, 7, 7, 7, 0, 0, 0, 0, 0, 0, 0, 3, 3, 3, 3, 3, 3, 3,
                                4, 4, 4, 4, 4, 4, 4, 3, 3, 3, 3, 3, 3, 3, 0, 0, 0, 0, 0, 0};

void appendBits(QVector<int> &bits, int value, int count)
{
    for (int i = count - 1; i >= 0; --i) {
        bits.append((value >> i) & 1);
    }
}

QByteArray bitsToBytes(const QVector<int> &bits)
{
    QByteArray result;
    result.resize((bits.size() + 7) / 8);
    result.fill('\0');
    for (int i = 0; i < bits.size(); ++i) {
        if (bits.at(i) != 0) {
            result[i >> 3] = char(uchar(result.at(i >> 3)) | uchar(0x80 >> (i & 7)));
        }
    }
    return result;
}

int gfMul(int x, int y)
{
    int z = 0;
    for (int i = 7; i >= 0; --i) {
        z = (z << 1) ^ ((z >> 7) * 0x11D);
        if (((y >> i) & 1) != 0) {
            z ^= x;
        }
    }
    return z;
}

QByteArray reedSolomon(const QByteArray &data, int degree)
{
    QVector<int> generator(degree);
    generator[degree - 1] = 1;
    int root = 1;
    for (int i = 0; i < degree; ++i) {
        for (int j = 0; j < degree; ++j) {
            generator[j] = gfMul(generator[j], root);
            if (j + 1 < degree) {
                generator[j] ^= generator[j + 1];
            }
        }
        root = gfMul(root, 0x02);
    }
    QVector<int> result(degree);
    for (unsigned char value : data) {
        const int factor = int(value) ^ result.front();
        for (int i = 0; i < degree - 1; ++i) {
            result[i] = result[i + 1] ^ gfMul(generator[i], factor);
        }
        result[degree - 1] = gfMul(generator[degree - 1], factor);
    }
    QByteArray ecc;
    ecc.resize(degree);
    for (int i = 0; i < degree; ++i) {
        ecc[i] = char(result[i]);
    }
    return ecc;
}

QByteArray buildInterleavedCodewords(const QString &data, const Vi &info, bool *ok)
{
    const QByteArray bytes = data.toUtf8();
    const int dataCapacityBits = info.dataCodewords * 8;
    QVector<int> bits;
    appendBits(bits, 0x4, 4);
    appendBits(bits, bytes.size(), info.version <= 9 ? 8 : 16);
    for (unsigned char b : bytes) {
        appendBits(bits, b, 8);
    }
    if (bits.size() > dataCapacityBits) {
        if (ok) {
            *ok = false;
        }
        return {};
    }
    appendBits(bits, 0, qMin(4, dataCapacityBits - bits.size()));
    while (bits.size() % 8 != 0) {
        bits.append(0);
    }
    QByteArray dataCodewords = bitsToBytes(bits);
    bool padToggle = true;
    while (dataCodewords.size() < info.dataCodewords) {
        dataCodewords.append(char(padToggle ? 0xEC : 0x11));
        padToggle = !padToggle;
    }
    const int dataPerBlock = info.dataCodewords / info.numBlocks;
    QVector<QByteArray> blocks;
    for (int i = 0; i < info.numBlocks; ++i) {
        blocks.append(dataCodewords.mid(i * dataPerBlock, dataPerBlock));
    }
    QVector<QByteArray> eccBlocks;
    for (const auto &block : blocks) {
        eccBlocks.append(reedSolomon(block, info.eccPerBlock));
    }
    QByteArray result;
    for (int i = 0; i < dataPerBlock; ++i) {
        for (const auto &block : blocks) {
            result.append(block.at(i));
        }
    }
    for (int i = 0; i < info.eccPerBlock; ++i) {
        for (const auto &block : eccBlocks) {
            result.append(block.at(i));
        }
    }
    if (ok) {
        *ok = true;
    }
    return result;
}

QVector<int> codewordsToBitsPlusRemainder(const QByteArray &cw, int remainderBits)
{
    QVector<int> out;
    out.reserve(cw.size() * 8 + remainderBits);
    for (int i = 0; i < cw.size(); ++i) {
        uchar b = uchar(cw.at(i));
        for (int k = 7; k >= 0; --k) {
            out.append((b >> k) & 1);
        }
    }
    for (int i = 0; i < remainderBits; ++i) {
        out.append(0);
    }
    return out;
}

struct Blocked {
    int n = 0;
    QVector<QVector<bool>> g;
    void init(int inner) {
        n = inner;
        g = QVector<QVector<bool>>(inner, QVector<bool>(inner, false));
    }
    void addRect(int x, int y, int w, int h)
    {
        for (int yy = y; yy < y + h; ++yy) {
            for (int xx = x; xx < x + w; ++xx) {
                if (xx >= 0 && yy >= 0 && xx < n && yy < n) {
                    g[yy][xx] = true;
                }
            }
        }
    }
    bool isBlocked(int x, int y) const { return g[y][x]; }
    bool rectBlocked(int x, int y, int w, int h) const
    {
        for (int yy = y; yy < y + h; ++yy) {
            for (int xx = x; xx < x + w; ++xx) {
                if (xx >= 0 && yy >= 0 && xx < n && yy < n && g[yy][xx]) {
                    return true;
                }
            }
        }
        return false;
    }
};

bool maskPatternFunc(int pattern, int x, int y)
{
    switch (pattern) {
    case 0:
        return ((x + y) % 2) == 0;
    case 1:
        return (y % 2) == 0;
    case 2:
        return (x % 3) == 0;
    case 3:
        return ((x + y) % 3) == 0;
    case 4:
        return (((y / 2) + (x / 3)) % 2) == 0;
    case 5:
        return ((x * y) % 2 + (x * y) % 3) == 0;
    case 6:
        return ((((x * y) % 2) + ((x * y) % 3)) % 2) == 0;
    case 7:
        return ((((x + y) % 2) + ((x * y) % 3)) % 2) == 0;
    default:
        return false;
    }
}

int maskScore(const QVector<QVector<bool>> &m)
{
    const int size = m.size();
    int score1 = 0;
    for (int y = 0; y < size; ++y) {
        int modInRow = 0;
        int modInColumn = 0;
        bool lastValRow = m[y][0];
        bool lastValColumn = m[0][y];
        for (int x = 0; x < size; ++x) {
            if (m[y][x] == lastValRow) {
                ++modInRow;
            } else {
                modInRow = 1;
            }
            if (modInRow == 5) {
                score1 += 3;
            } else if (modInRow > 5) {
                score1 += 1;
            }
            lastValRow = m[y][x];

            if (m[x][y] == lastValColumn) {
                ++modInColumn;
            } else {
                modInColumn = 1;
            }
            if (modInColumn == 5) {
                score1 += 3;
            } else if (modInColumn > 5) {
                score1 += 1;
            }
            lastValColumn = m[x][y];
        }
    }
    int score2 = 0;
    for (int y = 0; y < size - 1; ++y) {
        for (int x = 0; x < size - 1; ++x) {
            if (m[y][x] == m[y][x + 1] && m[y][x] == m[y + 1][x] && m[y][x] == m[y + 1][x + 1]) {
                score2 += 3;
            }
        }
    }
    int score3 = 0;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size - 10; ++x) {
            auto row = [&](int dx) { return m[y][x + dx]; };
            if ((row(0) && !row(1) && row(2) && row(3) && row(4) && !row(5) && row(6) && !row(7) && !row(8) && !row(9)
                 && !row(10))
                || (!row(0) && !row(1) && !row(2) && !row(3) && row(4) && !row(5) && row(6) && row(7) && row(8)
                    && !row(9) && row(10))) {
                score3 += 40;
            }
            auto col = [&](int dy) { return m[x + dy][y]; };
            if ((col(0) && !col(1) && col(2) && col(3) && col(4) && !col(5) && col(6) && !col(7) && !col(8) && !col(9)
                 && !col(10))
                || (!col(0) && !col(1) && !col(2) && !col(3) && col(4) && !col(5) && col(6) && col(7) && col(8)
                    && !col(9) && col(10))) {
                score3 += 40;
            }
        }
    }
    int black = 0;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            if (m[y][x]) {
                ++black;
            }
        }
    }
    const int total = size * size;
    const int percentDiv5 = black * 20 / total;
    const int prevMultipleOf5 = qAbs(percentDiv5 - 10);
    const int nextMultipleOf5 = qAbs(percentDiv5 - 9);
    const int score4 = qMin(prevMultipleOf5, nextMultipleOf5) * 10;
    return score1 + score2 + score3 + score4;
}

// 与 qrcodeencoder.cpp 原 GetFormatBits 一致（与 QRCoder / ISO 格式信息兼容）
int getFormatBitsEccL(int mask)
{
    const int data = (1 << 3) | mask;
    int rem = data;
    for (int i = 0; i < 10; ++i) {
        rem = (rem << 1) ^ (((rem >> 9) & 1) * 0x537);
    }
    return (((data << 10) | rem) ^ 0x5412) & 0x7FFF;
}

// PlaceFormat 使用 formatStr[14-i]；与 DrawFormatBits 的 (bits>>k) 顺序对齐
QVector<bool> formatBits15(int maskVersion)
{
    const int bits = getFormatBitsEccL(maskVersion);
    QVector<bool> f(15);
    for (int k = 0; k < 15; ++k) {
        f[14 - k] = ((bits >> k) & 1) != 0;
    }
    return f;
}

void placeFormatPadded(QVector<QVector<bool>> &padded, const QVector<bool> &formatStr, int inner)
{
    const int offsetValue = 4;
    const int size = inner;
    for (int i = 0; i < 15; ++i) {
        const int x1 = i < 8 ? 8 : (i == 8 ? 7 : 14 - i);
        const int y1 = i < 6 ? i : (i < 7 ? i + 1 : 8);
        const int x2 = i < 8 ? size - 1 - i : 8;
        const int y2 = i < 8 ? 8 : size - (15 - i);
        padded[y1 + offsetValue][x1 + offsetValue] = formatStr[14 - i];
        padded[y2 + offsetValue][x2 + offsetValue] = formatStr[14 - i];
    }
}

void placeFormatUnpadded(QVector<QVector<bool>> &m, const QVector<bool> &formatStr, int size)
{
    for (int i = 0; i < 15; ++i) {
        const int x1 = i < 8 ? 8 : (i == 8 ? 7 : 14 - i);
        const int y1 = i < 6 ? i : (i < 7 ? i + 1 : 8);
        const int x2 = i < 8 ? size - 1 - i : 8;
        const int y2 = i < 8 ? 8 : size - (15 - i);
        m[y1][x1] = formatStr[14 - i];
        m[y2][x2] = formatStr[14 - i];
    }
}

void placeFinder(QVector<QVector<bool>> &padded, Blocked &bl, int inner, int locationX, int locationY)
{
    for (int x = 0; x < 7; ++x) {
        for (int y = 0; y < 7; ++y) {
            if (!(((x == 1 || x == 5) && y > 0 && y < 6) || (x > 0 && x < 6 && (y == 1 || y == 5)))) {
                padded[y + locationY + 4][x + locationX + 4] = true;
            }
        }
    }
    bl.addRect(locationX, locationY, 7, 7);
}

void reserveSeparator(Blocked &bl, int inner)
{
    bl.addRect(7, 0, 1, 8);
    bl.addRect(0, 7, 7, 1);
    bl.addRect(0, inner - 8, 8, 1);
    bl.addRect(7, inner - 7, 1, 7);
    bl.addRect(inner - 8, 0, 1, 8);
    bl.addRect(inner - 7, 7, 7, 1);
}

void reserveVersionAreas(Blocked &bl, int inner, int version)
{
    Q_UNUSED(version);
    bl.addRect(8, 0, 1, 6);
    bl.addRect(8, 7, 1, 1);
    bl.addRect(0, 8, 6, 1);
    bl.addRect(7, 8, 2, 1);
    bl.addRect(inner - 8, 8, 8, 1);
    bl.addRect(8, inner - 7, 1, 7);
    if (version >= 7) {
        bl.addRect(inner - 11, 0, 3, 6);
        bl.addRect(0, inner - 11, 6, 3);
    }
}

void placeTiming(QVector<QVector<bool>> &padded, Blocked &bl, int inner)
{
    for (int i = 8; i < inner - 8; ++i) {
        if (i % 2 == 0) {
            padded[6 + 4][i + 4] = true;
            padded[i + 4][6 + 4] = true;
        }
    }
    bl.addRect(6, 8, 1, inner - 16);
    bl.addRect(8, 6, inner - 16, 1);
}

void placeDarkModule(QVector<QVector<bool>> &padded, Blocked &bl, int version)
{
    padded[4 * version + 9 + 4][8 + 4] = true;
    bl.addRect(8, 4 * version + 9, 1, 1);
}

void collectAlignmentPositions(int version, QVector<QPair<int, int>> *topLefts)
{
    // 与 QRCoder 1.6.0 CreateAlignmentPatternTable：version v 使用 _alignmentPatternBaseValues[(v-1)*7 .. +6]
    static const int rows[4][7] = {
        {6, 22, 0, 0, 0, 0, 0},
        {6, 26, 0, 0, 0, 0, 0},
        {6, 30, 0, 0, 0, 0, 0},
        {6, 34, 0, 0, 0, 0, 0},
    };
    const int idx = version - 3;
    if (idx < 0 || idx > 3) {
        return;
    }
    const int *r = rows[idx];
    QVector<int> vals;
    for (int i = 0; i < 7; ++i) {
        if (r[i] > 0) {
            vals.append(r[i]);
        }
    }
    QSet<QString> seen;
    for (int a : vals) {
        for (int b : vals) {
            const int tx = a - 2;
            const int ty = b - 2;
            const QString k = QStringLiteral("%1,%2").arg(tx).arg(ty);
            if (seen.contains(k)) {
                continue;
            }
            seen.insert(k);
            topLefts->append({tx, ty});
        }
    }
}

void placeAlignmentPatterns(QVector<QVector<bool>> &padded, Blocked &bl, int inner, int version)
{
    QVector<QPair<int, int>> positions;
    collectAlignmentPositions(version, &positions);
    for (const auto &pr : positions) {
        const int locX = pr.first;
        const int locY = pr.second;
        if (bl.rectBlocked(locX, locY, 5, 5)) {
            continue;
        }
        for (int x = 0; x < 5; ++x) {
            for (int y = 0; y < 5; ++y) {
                if (y == 0 || y == 4 || x == 0 || x == 4 || (x == 2 && y == 2)) {
                    padded[locY + y + 4][locX + x + 4] = true;
                }
            }
        }
        bl.addRect(locX, locY, 5, 5);
    }
    Q_UNUSED(inner);
}

void placeDataWords(QVector<QVector<bool>> &padded, const QVector<int> &dataBits, Blocked &bl, int inner)
{
    bool up = true;
    int index = 0;
    const int count = dataBits.size();
    // 须与 QRCoder ModulePlacer.PlaceDataWords 一致：x==6 时把循环变量改为 5，使本轮结束后 x-=2 从 5 减到 3（不可仅用局部 col）。
    for (int x = inner - 1; x >= 0; x -= 2) {
        if (x == 6) {
            x = 5;
        }
        for (int yMod = 1; yMod <= inner; ++yMod) {
            const int y = up ? (inner - yMod) : (yMod - 1);
            if (index < count && !bl.isBlocked(x, y)) {
                padded[y + 4][x + 4] = (dataBits[index++] != 0);
            }
            if (index < count && x > 0 && !bl.isBlocked(x - 1, y)) {
                padded[y + 4][x - 1 + 4] = (dataBits[index++] != 0);
            }
        }
        up = !up;
    }
}

int applyBestMask(QVector<QVector<bool>> &padded, Blocked &bl, int inner)
{
    int bestMask = 0;
    int bestScore = INT_MAX;

    QVector<QVector<bool>> innerData(inner, QVector<bool>(inner));
    for (int y = 0; y < inner; ++y) {
        for (int x = 0; x < inner; ++x) {
            innerData[y][x] = padded[y + 4][x + 4];
        }
    }

    for (int maskPattern = 0; maskPattern < 8; ++maskPattern) {
        QVector<QVector<bool>> qrTemp = innerData;
        const QVector<bool> fStr = formatBits15(maskPattern);
        placeFormatUnpadded(qrTemp, fStr, inner);

        for (int x = 0; x < inner; ++x) {
            for (int y = 0; y < x; ++y) {
                if (!bl.isBlocked(x, y)) {
                    qrTemp[y][x] = qrTemp[y][x] != maskPatternFunc(maskPattern, x, y);
                    qrTemp[x][y] = qrTemp[x][y] != maskPatternFunc(maskPattern, y, x);
                }
            }
            if (!bl.isBlocked(x, x)) {
                qrTemp[x][x] = qrTemp[x][x] != maskPatternFunc(maskPattern, x, x);
            }
        }

        const int score = maskScore(qrTemp);
        if (score < bestScore) {
            bestScore = score;
            bestMask = maskPattern;
        }
    }

    for (int x = 0; x < inner; ++x) {
        for (int y = 0; y < x; ++y) {
            if (!bl.isBlocked(x, y)) {
                padded[y + 4][x + 4] = padded[y + 4][x + 4] != maskPatternFunc(bestMask, x, y);
                padded[x + 4][y + 4] = padded[x + 4][y + 4] != maskPatternFunc(bestMask, y, x);
            }
        }
        if (!bl.isBlocked(x, x)) {
            padded[x + 4][x + 4] = padded[x + 4][x + 4] != maskPatternFunc(bestMask, x, x);
        }
    }
    placeFormatPadded(padded, formatBits15(bestMask), inner);
    return bestMask;
}

} // namespace

QVector<QVector<int>> QrMatrixBuildQrcoder160(const QString &utf8Payload, int version, QString *errorMessage)
{
    const Vi *info = findVi(version);
    if (!info) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("不支持的二维码版本：%1").arg(version);
        }
        return {};
    }
    bool ok = false;
    const QByteArray cw = buildInterleavedCodewords(utf8Payload, *info, &ok);
    if (!ok) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("二维码内容超过版本 %1 容量").arg(version);
        }
        return {};
    }
    const int rem = kRemainderBits[version - 1];
    const QVector<int> dataBits = codewordsToBitsPlusRemainder(cw, rem);
    const int inner = info->size;
    const int pad = inner + 8;

    QVector<QVector<bool>> padded(pad, QVector<bool>(pad, false));
    Blocked bl;
    bl.init(inner);

    placeFinder(padded, bl, inner, 0, 0);
    placeFinder(padded, bl, inner, inner - 7, 0);
    placeFinder(padded, bl, inner, 0, inner - 7);
    reserveSeparator(bl, inner);
    placeAlignmentPatterns(padded, bl, inner, version);
    placeTiming(padded, bl, inner);
    placeDarkModule(padded, bl, version);
    reserveVersionAreas(bl, inner, version);
    placeDataWords(padded, dataBits, bl, inner);
    applyBestMask(padded, bl, inner);

    QVector<QVector<int>> out(inner, QVector<int>(inner, 0));
    for (int y = 0; y < inner; ++y) {
        for (int x = 0; x < inner; ++x) {
            out[y][x] = padded[y + 4][x + 4] ? 1 : 0;
        }
    }
    return out;
}
