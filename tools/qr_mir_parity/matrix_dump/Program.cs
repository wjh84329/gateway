// Dumps QRCoder 1.6.0 inner matrix (same crop as Mir reference): size = ModuleMatrix.Count - 8, centered.
using System.Text;
using QRCoder;

var url = args.Length > 0 ? args[0] : "https://api.feixpay.cn/Call/Hlbbpay/wxRawPub?pay_order_id=202605120050421912";
var serial = args.Length > 1 && int.TryParse(args[1], out var s) ? s : 4;
var outPath = args.Length > 2 ? args[2] : null;

var requestedVersion = serial > 0 ? serial : 6;
var qrData = new QRCodeGenerator().CreateQrCode(url, QRCodeGenerator.ECCLevel.L, requestedVersion: requestedVersion);

int rows = qrData.ModuleMatrix.Count - 8;
int totalRows = qrData.ModuleMatrix.Count;
int startRow = (totalRows - rows) / 2;

var sb = new StringBuilder(rows * (rows + 1));
for (int i = 0; i < rows; i++)
{
    for (int j = 0; j < rows; j++)
        sb.Append(qrData.ModuleMatrix[startRow + i][startRow + j] ? '1' : '0');
    sb.Append('\n');
}

var text = sb.ToString();
if (!string.IsNullOrEmpty(outPath))
    File.WriteAllText(outPath, text, new UTF8Encoding(false));
else
    Console.Write(text);
return 0;
