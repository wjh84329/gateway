在线更新 zip 解压（优先于 tar / PowerShell）

重要：官方「Extra」里的 7zr.exe 多为精简版，通常不含 zip 解码器（`7zr i` 列表中无 zip），无法解压管理端下发的 wg zip。
本仓库 tools 目录应放置带 zip 的 7za.exe（或安装完整 7-Zip 后由程序自动查找 7z.exe）。

1. 推荐：将 7za.exe 放到本目录（与 README 同级）。可从 7-Zip 官方「Extra」包解压得到，或使用与官方兼容的独立构建（例如 develar/7zip-bin 的 win/x64/7za.exe）。
   https://www.7-zip.org/download.html
2. 若仅有 7zr.exe：CMake 仍可嵌入，但在线更新 zip 大概率会失败，只能依赖系统 tar / PowerShell 回退（环境差异大）。
3. 重新运行 CMake「配置」并编译：仅 cmake --build 不会重新扫描嵌入文件；须在仓库根执行一次「配置」（并带上 Qt 的 CMAKE_PREFIX_PATH）。若存在 tools\7za.exe，应看到：
   Gateway: embedding tools/7za.exe for zip extract (online update)
   示例：cmake -S D:\7xNew\gateway -B D:\7xNew\gateway\out\build -DCMAKE_PREFIX_PATH=你的Qt根目录
4. scripts\build_single_file.ps1 / build_single_file.bat：若存在 tools\7za.exe 或 tools\7zr.exe，或尚未生成 out\build\CMakeCache.txt，会先自动 cmake -S -B 再全量编译后打包单文件。
5. 运行时若未嵌入资源，可在网关 exe 同目录放置 7za.exe / 7z.exe，或 tools\ 子目录下同名文件；已安装完整 7-Zip 时也可使用默认安装路径下的 7z.exe。

许可：7-Zip / LZMA 相关组件请遵守其许可证（见 7-zip.org）；分发时请在产品说明中保留版权与许可信息。
