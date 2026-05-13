// 与 Gateway/Common/QrCoderUtil.cs 中 GenerateAndSaveQrCodeAsync1 的 URL 分支一致（QRCoder 1.6.0 + 居中裁切 + Mir 拼接）。
using System.IO;
using System.Text;
using QRCoder;

namespace QrMirParityReference;

internal static class Program
{
    private static int ImgIntFromSerial(int serial) => serial switch
    {
        3 => 43,
        4 => 44,
        5 => 45,
        _ => 46,
    };

    private static string BuildMirFromUrl(string url, int serial, string resourceCode, string imageCode, int xOffset, int yOffset)
    {
        var requestedVersion = serial > 0 ? serial : 6;
        var qrData = new QRCodeGenerator().CreateQrCode(url, QRCodeGenerator.ECCLevel.L, requestedVersion: requestedVersion);

        int rows = qrData.ModuleMatrix.Count - 8;
        int cols = qrData.ModuleMatrix.Count - 8;
        int totalRows = qrData.ModuleMatrix.Count;
        int totalCols = qrData.ModuleMatrix[0].Count;
        int startRow = (totalRows - rows) / 2;
        int startCol = (totalCols - cols) / 2;
        if (startRow < 0 || startCol < 0)
            throw new InvalidOperationException("invalid matrix crop");

        int imgInt = ImgIntFromSerial(serial);
        int smallStep = imgInt - 46;
        int rowStep = Math.Max(1, imgInt - 40);
        int rowJump = cols * rowStep;

        var bits = new bool[rows, cols];
        var nVals = new int[rows, cols];
        var groupOffsets = new int[rows, cols];

        int prevRowEnd = 0;
        bool firstBlackFound = false;

        for (int i = 0; i < rows; i++)
        {
            int nRow = (i == 0) ? 0 : prevRowEnd - rowJump;
            int lastNInRow = nRow;

            for (int j = 0; j < cols; j++)
            {
                bool bit = qrData.ModuleMatrix[startRow + i][startCol + j];
                bits[i, j] = bit;

                if (!bit) nRow += smallStep;

                int currentN = nRow;
                if (bit && !firstBlackFound)
                {
                    currentN = 0;
                    nRow = 0;
                    firstBlackFound = true;
                }

                lastNInRow = currentN;
                int groupOffset = i * rowStep;

                nVals[i, j] = currentN;
                groupOffsets[i, j] = groupOffset;
            }

            prevRowEnd = lastNInRow;
        }

        bool found = false;
        int baseX = 0, baseY = 0;
        for (int i = 0; i < rows && !found; i++)
        {
            for (int j = 0; j < cols; j++)
            {
                if (bits[i, j])
                {
                    baseX = nVals[i, j];
                    baseY = groupOffsets[i, j];
                    found = true;
                    break;
                }
            }
        }

        int shiftX = found ? -baseX : 0;
        int shiftY = found ? -baseY : 0;

        var sb = new StringBuilder(rows * cols * 24);
        for (int i = 0; i < rows; i++)
        {
            for (int j = 0; j < cols; j++)
            {
                if (bits[i, j])
                {
                    int outX = nVals[i, j] + shiftX + xOffset;
                    int outY = groupOffsets[i, j] + shiftY + yOffset;
                    sb.Append($"<Img:{imageCode}:{resourceCode}:{outX}:{outY}>");
                }
                else
                {
                    sb.Append("< >");
                }
            }
        }

        return sb.ToString();
    }

    public static int Main(string[] args)
    {
        var url = args.Length > 0 ? args[0] : "https://api.feixpay.cn/Call/Hlbbpay/wxRawPub?pay_order_id=202605120050421912";
        var serial = args.Length > 1 && int.TryParse(args[1], out var s) ? s : 4;
        var resourceCode = args.Length > 2 ? args[2] : "29";
        var imageCode = args.Length > 3 ? args[3] : "44";
        var xOffset = args.Length > 4 && int.TryParse(args[4], out var x) ? x : 10;
        var yOffset = args.Length > 5 && int.TryParse(args[5], out var y) ? y : 10;
        var outPath = args.Length > 6 ? args[6] : null;

        var text = BuildMirFromUrl(url, serial, resourceCode, imageCode, xOffset, yOffset);
        if (!string.IsNullOrEmpty(outPath))
        {
            File.WriteAllText(outPath, text, new UTF8Encoding(false));
            return 0;
        }

        Console.OutputEncoding = new UTF8Encoding(false);
        Console.Write(text);
        return 0;
    }
}
