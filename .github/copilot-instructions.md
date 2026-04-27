# Copilot Instructions

## 项目指南
- In this UI, the user prefers left-side script menu text to be centered rather than left-aligned.
- The user prefers table cell content in this UI to be centered for a cleaner look.
- The user wants all table headers and cells to have visible divider lines.
- For the system settings page, the user wants the layout to match the reference but use the existing maroon theme instead of blue. The user also prefers tighter spacing in the system settings form and wants the layout visually refined, avoiding large gaps between labels and fields.
- In the system log table, the log message column should be left-aligned instead of centered.
- The system log page should start with an empty table and only show today's logs after clicking the '加载当天日志' button. The log page needs to automatically scroll to the bottom to always display the latest logs.
- The "清除日志" operation on the log page should only clear the interface content and not reload previous logs; it should continue to display only the new logs generated after the clearing operation. The log file extension should use `.log` instead of `.txt`.
- The user prefers success/failure dialogs in this UI to avoid default system-style message boxes and use a more refined custom in-app visual style. Custom dialogs should show only the centered card content without an outer framed/background ring around it.

## Partition Management
- The user wants a small right-side margin on all three subpages of the partition management area, but not as much whitespace as before on the main partition management page.
- The partition management page should remove the IP and port columns and show the currency name instead.
- The partition template page should remove the game column.

## File Monitoring
- The user wants the file monitoring logic to be a stable, root-path-based design that can monitor multiple partitions across different drive letters at the same time, and file content handling should stay aligned with the old C# FileMonitor behavior.

## Build and Test Automation
- After each code change, run a full build automatically without being asked.
- Installation scripts do not need to retain skeleton fallback logic; the user has placed the real template in the Debug directory and wants to align the reading and generation behavior with the old C# `ProcessInstall` as much as possible.

## RabbitMQ Configuration
- The RabbitMQ connection in the online environment must rely solely on the decrypted `ConnectionStrings/Socket` configuration, which uses the decryption password `#gddfxyz$123` as the primary attempt (including the leading `#`), and should retain the previous password `hdfgail9xyzgzl88` as a fallback. It should not assume that the separate RabbitMQ configuration in `AppSettings` is available.

## Runtime Environment
- The user requires that the online version must not depend on .NET/PowerShell. The refactoring goal is to ensure that it can run on Windows Server without additional environment installations. All QR code and decryption compatibility logic should be implemented in pure C++.