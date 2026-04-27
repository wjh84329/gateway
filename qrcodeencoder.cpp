#include "qrcodeencoder.h"

#include <QByteArray>
#include <QVector>

#include <climits>

namespace {
struct VersionInfo {
    int version;
    int size;
    int dataCodewords;
    int eccCodewordsPerBlock;
    int numBlocks;
    int alignment;
};

const VersionInfo kVersions[] = {
    {3, 29, 55, 15, 1, 22},
    {4, 33, 80, 20, 1, 26},
    {5, 37, 108, 26, 1, 30},
    {6, 41, 68, 18, 2, 34}
};

const VersionInfo *FindVersionInfo(int version)
{
    for (const auto &info : kVersions) {
        if (info.version == version) {
            return &info;
        }
    }
    return nullptr;
}

void AppendBits(QVector<int> &bits, int value, int count)
{
    for (int i = count - 1; i >= 0; --i) {
        bits.append((value >> i) & 1);
    }
}

QByteArray BitsToBytes(const QVector<int> &bits)
{
    QByteArray result;
    result.resize((bits.size() + 7) / 8);
    result.fill('\0');
    for (int i = 0; i < bits.size(); ++i) {
        if (bits.at(i) != 0) {
            result[i >> 3] = char(result.at(i >> 3) | (0x80 >> (i & 7)));
        }
    }
    return result;
}

int GfMultiply(int x, int y)
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

QByteArray ComputeReedSolomon(const QByteArray &data, int degree)
{
    QVector<int> generator(degree);
    generator[degree - 1] = 1;
    int root = 1;
    for (int i = 0; i < degree; ++i) {
        for (int j = 0; j < degree; ++j) {
            generator[j] = GfMultiply(generator[j], root);
            if (j + 1 < degree) {
                generator[j] ^= generator[j + 1];
            }
        }
        root = GfMultiply(root, 0x02);
    }

    QVector<int> result(degree);
    for (unsigned char value : data) {
        const int factor = int(value) ^ result.front();
        for (int i = 0; i < degree - 1; ++i) {
            result[i] = result[i + 1] ^ GfMultiply(generator[i], factor);
        }
        result[degree - 1] = GfMultiply(generator[degree - 1], factor);
    }

    QByteArray ecc;
    ecc.resize(degree);
    for (int i = 0; i < degree; ++i) {
        ecc[i] = char(result[i]);
    }
    return ecc;
}

void DrawFinder(QVector<QVector<int>> &modules, QVector<QVector<bool>> &isFunction, int x, int y)
{
    const int size = modules.size();
    for (int dy = -1; dy <= 7; ++dy) {
        for (int dx = -1; dx <= 7; ++dx) {
            const int xx = x + dx;
            const int yy = y + dy;
            if (xx < 0 || yy < 0 || xx >= size || yy >= size) {
                continue;
            }
            const bool isBorder = dx == -1 || dx == 7 || dy == -1 || dy == 7;
            const bool isOuter = dx == 0 || dx == 6 || dy == 0 || dy == 6;
            const bool isInner = dx >= 2 && dx <= 4 && dy >= 2 && dy <= 4;
            modules[yy][xx] = (!isBorder && (isOuter || isInner)) ? 1 : 0;
            isFunction[yy][xx] = true;
        }
    }
}

void DrawAlignment(QVector<QVector<int>> &modules, QVector<QVector<bool>> &isFunction, int cx, int cy)
{
    for (int dy = -2; dy <= 2; ++dy) {
        for (int dx = -2; dx <= 2; ++dx) {
            const int xx = cx + dx;
            const int yy = cy + dy;
            modules[yy][xx] = qMax(qAbs(dx), qAbs(dy)) != 1 ? 1 : 0;
            isFunction[yy][xx] = true;
        }
    }
}

int GetFormatBits(int mask)
{
    const int data = (1 << 3) | mask; // ECC L
    int rem = data;
    for (int i = 0; i < 10; ++i) {
        rem = (rem << 1) ^ (((rem >> 9) & 1) * 0x537);
    }
    return ((data << 10) | rem) ^ 0x5412;
}

void DrawFormatBits(QVector<QVector<int>> &modules, QVector<QVector<bool>> &isFunction, int mask)
{
    const int size = modules.size();
    const int bits = GetFormatBits(mask);
    for (int i = 0; i <= 5; ++i) {
        modules[8][i] = (bits >> i) & 1;
        isFunction[8][i] = true;
    }
    modules[8][7] = (bits >> 6) & 1;
    modules[8][8] = (bits >> 7) & 1;
    modules[7][8] = (bits >> 8) & 1;
    isFunction[8][7] = isFunction[8][8] = isFunction[7][8] = true;
    for (int i = 9; i < 15; ++i) {
        modules[14 - i][8] = (bits >> i) & 1;
        isFunction[14 - i][8] = true;
    }
    for (int i = 0; i < 8; ++i) {
        modules[size - 1 - i][8] = (bits >> i) & 1;
        isFunction[size - 1 - i][8] = true;
    }
    for (int i = 8; i < 15; ++i) {
        modules[8][size - 15 + i] = (bits >> i) & 1;
        isFunction[8][size - 15 + i] = true;
    }
    modules[size - 8][8] = 1;
    isFunction[size - 8][8] = true;
}

bool GetMaskBit(int mask, int x, int y)
{
    switch (mask) {
    case 0: return ((x + y) % 2) == 0;
    case 1: return (y % 2) == 0;
    case 2: return (x % 3) == 0;
    case 3: return ((x + y) % 3) == 0;
    case 4: return (((y / 2) + (x / 3)) % 2) == 0;
    case 5: return ((x * y) % 2 + (x * y) % 3) == 0;
    case 6: return ((((x * y) % 2) + ((x * y) % 3)) % 2) == 0;
    case 7: return ((((x + y) % 2) + ((x * y) % 3)) % 2) == 0;
    default: return false;
    }
}

void DrawCodewords(QVector<QVector<int>> &modules,
                   const QVector<QVector<bool>> &isFunction,
                   const QByteArray &codewords)
{
    int bitIndex = 0;
    int direction = -1;
    const int size = modules.size();
    for (int right = size - 1; right >= 1; right -= 2) {
        if (right == 6) {
            right = 5;
        }
        for (int vert = 0; vert < size; ++vert) {
            const int y = direction == 1 ? vert : size - 1 - vert;
            for (int j = 0; j < 2; ++j) {
                const int x = right - j;
                if (isFunction[y][x]) {
                    continue;
                }
                int bit = 0;
                if (bitIndex < codewords.size() * 8) {
                    bit = (uchar(codewords.at(bitIndex >> 3)) >> (7 - (bitIndex & 7))) & 1;
                    ++bitIndex;
                }
                modules[y][x] = bit;
            }
        }
        direction = -direction;
    }
}

int FinderPenaltyCountPatterns(const QVector<int> &runHistory)
{
    const int n = runHistory.size();
    if (n < 7) {
        return 0;
    }
    const int core = runHistory[n - 2];
    if (core == 0) {
        return 0;
    }
    const bool pattern = runHistory[n - 1] == core
                         && runHistory[n - 3] == core
                         && runHistory[n - 4] == core * 3
                         && runHistory[n - 5] == core
                         && runHistory[n - 6] == core;
    if (!pattern) {
        return 0;
    }
    int count = 0;
    if (runHistory[n - 7] >= core * 4 && runHistory[n - 1] >= core) {
        ++count;
    }
    if (n >= 8 && runHistory[n - 8] >= core * 4 && runHistory[n - 2] >= core) {
        ++count;
    }
    return count;
}

int ComputePenalty(const QVector<QVector<int>> &modules)
{
    const int size = modules.size();
    int penalty = 0;

    for (int y = 0; y < size; ++y) {
        int runColor = 0;
        int runX = 0;
        QVector<int> runHistory(7);
        for (int x = 0; x < size; ++x) {
            if (modules[y][x] == runColor) {
                ++runX;
                if (x == size - 1 && runX >= 5) {
                    penalty += 3 + (runX - 5);
                }
            } else {
                if (runX >= 5) {
                    penalty += 3 + (runX - 5);
                }
                runHistory.removeFirst();
                runHistory.append(runX);
                if (runColor == 0) {
                    penalty += FinderPenaltyCountPatterns(runHistory) * 40;
                }
                runColor = modules[y][x];
                runX = 1;
            }
        }
    }

    for (int x = 0; x < size; ++x) {
        int runColor = 0;
        int runY = 0;
        QVector<int> runHistory(7);
        for (int y = 0; y < size; ++y) {
            if (modules[y][x] == runColor) {
                ++runY;
                if (y == size - 1 && runY >= 5) {
                    penalty += 3 + (runY - 5);
                }
            } else {
                if (runY >= 5) {
                    penalty += 3 + (runY - 5);
                }
                runHistory.removeFirst();
                runHistory.append(runY);
                if (runColor == 0) {
                    penalty += FinderPenaltyCountPatterns(runHistory) * 40;
                }
                runColor = modules[y][x];
                runY = 1;
            }
        }
    }

    for (int y = 0; y < size - 1; ++y) {
        for (int x = 0; x < size - 1; ++x) {
            const int color = modules[y][x];
            if (color == modules[y][x + 1] && color == modules[y + 1][x] && color == modules[y + 1][x + 1]) {
                penalty += 3;
            }
        }
    }

    int dark = 0;
    for (const auto &row : modules) {
        for (int bit : row) {
            dark += bit;
        }
    }
    const int total = size * size;
    const int k = qAbs(dark * 20 - total * 10) / total;
    penalty += k * 10;
    return penalty;
}

QVector<QVector<int>> BuildBaseMatrix(const VersionInfo &info, QVector<QVector<bool>> &isFunction)
{
    QVector<QVector<int>> modules(info.size, QVector<int>(info.size, 0));
    isFunction = QVector<QVector<bool>>(info.size, QVector<bool>(info.size, false));

    DrawFinder(modules, isFunction, 0, 0);
    DrawFinder(modules, isFunction, info.size - 7, 0);
    DrawFinder(modules, isFunction, 0, info.size - 7);

    for (int i = 8; i < info.size - 8; ++i) {
        modules[6][i] = i % 2 == 0;
        modules[i][6] = i % 2 == 0;
        isFunction[6][i] = true;
        isFunction[i][6] = true;
    }

    DrawAlignment(modules, isFunction, info.alignment, info.alignment);
    modules[info.size - 8][8] = 1;
    isFunction[info.size - 8][8] = true;
    return modules;
}

QByteArray BuildCodewords(const QString &data, const VersionInfo &info, bool *ok)
{
    const QByteArray bytes = data.toUtf8();
    const int dataCapacityBits = info.dataCodewords * 8;
    QVector<int> bits;
    AppendBits(bits, 0x4, 4);
    AppendBits(bits, bytes.size(), info.version <= 9 ? 8 : 16);
    for (unsigned char b : bytes) {
        AppendBits(bits, b, 8);
    }
    if (bits.size() > dataCapacityBits) {
        if (ok) {
            *ok = false;
        }
        return {};
    }
    AppendBits(bits, 0, qMin(4, dataCapacityBits - bits.size()));
    while (bits.size() % 8 != 0) {
        bits.append(0);
    }
    QByteArray dataCodewords = BitsToBytes(bits);
    bool padToggle = true;
    while (dataCodewords.size() < info.dataCodewords) {
        dataCodewords.append(char(padToggle ? 0xEC : 0x11));
        padToggle = !padToggle;
    }

    QVector<QByteArray> blocks;
    const int dataPerBlock = info.dataCodewords / info.numBlocks;
    for (int i = 0; i < info.numBlocks; ++i) {
        blocks.append(dataCodewords.mid(i * dataPerBlock, dataPerBlock));
    }

    QVector<QByteArray> eccBlocks;
    for (const auto &block : blocks) {
        eccBlocks.append(ComputeReedSolomon(block, info.eccCodewordsPerBlock));
    }

    QByteArray result;
    for (int i = 0; i < dataPerBlock; ++i) {
        for (const auto &block : blocks) {
            result.append(block.at(i));
        }
    }
    for (int i = 0; i < info.eccCodewordsPerBlock; ++i) {
        for (const auto &block : eccBlocks) {
            result.append(block.at(i));
        }
    }

    if (ok) {
        *ok = true;
    }
    return result;
}

QString BuildLegacyMirText(const QVector<QVector<int>> &modules,
                           const QString &resourceCode,
                           const QString &imageCode,
                           int serial,
                           int xOffset,
                           int yOffset)
{
    const int rows = modules.size();
    const int cols = modules.size();
    int imgInt = 46;
    if (serial == 3) {
        imgInt = 43;
    } else if (serial == 4) {
        imgInt = 44;
    } else if (serial == 5) {
        imgInt = 45;
    }

    const int smallStep = imgInt - 46;
    const int rowStep = qMax(1, imgInt - 40);
    const int rowJump = cols * rowStep;
    QVector<QVector<int>> nVals(rows, QVector<int>(cols));
    QVector<QVector<int>> groupOffsets(rows, QVector<int>(cols));
    int prevRowEnd = 0;
    bool firstBlackFound = false;

    for (int i = 0; i < rows; ++i) {
        int nRow = i == 0 ? 0 : prevRowEnd - rowJump;
        int lastNInRow = nRow;
        for (int j = 0; j < cols; ++j) {
            const bool bit = modules[i][j] != 0;
            if (!bit) {
                nRow += smallStep;
            }
            int currentN = nRow;
            if (bit && !firstBlackFound) {
                currentN = 0;
                nRow = 0;
                firstBlackFound = true;
            }
            lastNInRow = currentN;
            const int groupOffset = i * rowStep;
            nVals[i][j] = currentN;
            groupOffsets[i][j] = groupOffset;
        }
        prevRowEnd = lastNInRow;
    }

    bool found = false;
    int baseX = 0;
    int baseY = 0;
    for (int i = 0; i < rows && !found; ++i) {
        for (int j = 0; j < cols; ++j) {
            if (modules[i][j] != 0) {
                baseX = nVals[i][j];
                baseY = groupOffsets[i][j];
                found = true;
                break;
            }
        }
    }

    const int shiftX = found ? -baseX : 0;
    const int shiftY = found ? -baseY : 0;
    QString result;
    result.reserve(rows * cols * 20);
    for (int i = 0; i < rows; ++i) {
        for (int j = 0; j < cols; ++j) {
            if (modules[i][j] != 0) {
                result += QStringLiteral("<Img:%1:%2:%3:%4>")
                              .arg(imageCode, resourceCode)
                              .arg(nVals[i][j] + shiftX + xOffset)
                              .arg(groupOffsets[i][j] + shiftY + yOffset);
            } else {
                result += QStringLiteral("< >");
            }
        }
    }
    return result;
}
}

QString QrCodeEncoder::GenerateLegacyMirText(const QString &data,
                                             const QString &resourceCode,
                                             const QString &imageCode,
                                             int serial,
                                             int xOffset,
                                             int yOffset,
                                             QString *errorMessage)
{
    const int version = serial > 0 ? serial : 6;
    const VersionInfo *info = FindVersionInfo(version);
    if (!info) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("不支持的二维码版本：%1").arg(version);
        }
        return {};
    }

    bool ok = false;
    const QByteArray codewords = BuildCodewords(data, *info, &ok);
    if (!ok) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("二维码内容超过版本 %1 容量").arg(version);
        }
        return {};
    }

    QVector<QVector<bool>> isFunction;
    const QVector<QVector<int>> base = BuildBaseMatrix(*info, isFunction);
    int bestPenalty = INT_MAX;
    QVector<QVector<int>> bestModules;
    for (int mask = 0; mask < 8; ++mask) {
        QVector<QVector<int>> modules = base;
        DrawCodewords(modules, isFunction, codewords);
        for (int y = 0; y < info->size; ++y) {
            for (int x = 0; x < info->size; ++x) {
                if (!isFunction[y][x] && GetMaskBit(mask, x, y)) {
                    modules[y][x] ^= 1;
                }
            }
        }
        DrawFormatBits(modules, isFunction, mask);
        const int penalty = ComputePenalty(modules);
        if (penalty < bestPenalty) {
            bestPenalty = penalty;
            bestModules = modules;
        }
    }

    if (bestModules.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("二维码矩阵生成失败");
        }
        return {};
    }

    return BuildLegacyMirText(bestModules,
                              resourceCode.isEmpty() ? QStringLiteral("46") : resourceCode,
                              imageCode.isEmpty() ? QStringLiteral("0") : imageCode,
                              version,
                              xOffset,
                              yOffset);
}
