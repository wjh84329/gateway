# gateway 目录 AGENTS.md

## 0. 项目背景

新网关：D:\7xNew\gateway，开发语言是c++，使用的qt
老网关：D:\7xNew\Qynet.GamePlatform\Gateway 开发语言是c#，框架是.net framework 4.6.2
老项目控制器：D:\7xNew\Qynet.GamePlatform\TenantServer\Controllers\ClientController.cs,使用的是.net core 6.0
商户端：D:\7xNew\tenantweb
管理后台API：D:\7xNew\Qynet.GamePlatform\ManageServer
管理后台前端：D:\7xNew\manageweb

**对齐基准**：功能与行为默认以本仓库所述 **老网关（`Qynet.GamePlatform\Gateway`）** 及 TenantServer 等线上约定为准；**不要**把同解决方案内的 `GatewayNew` 或其它变体默认当作「老网关」对照，除非任务或文档明确要求以其中某一版为准。

在重构过程中，需要对齐老项目的功能实现，特别是 `RabbitMessageEnum.ManualReissue` 等消息处理逻辑。

**功能全貌与差异**：本版已实现能力、RabbitMQ 类型与「相对老网关仍偏弱」的项见 **§3.1～§3.5**，改需求前先对照，避免重复造轮子或漏改后端契约。

## 1. 目的

本文件用于约束在新平台网关目录 `D:\7xNew\gateway` 内工作的智能代理、自动化脚本与协作开发者，确保在阅读、修改、构建、测试、排障、数据库访问和文件处理时遵循统一规则。

该目录是新平台网关，虽然允许在用户任务范围内进行代码修改，但仍需坚持最小改动、优先局部验证、谨慎处理外部副作用。

**编译与打包（项目约定）**：在本目录凡需 **配置 CMake、编译 gateway、做单文件静态 Release 验证**，**一律以 `docs/单文件打包指南.md` 为唯一依据**；日常重编优先执行 `scripts\rebuild_gateway_static_release.bat`（产物见该文档与 §8）。**除非当前任务里用户明确要求** 使用 Debug、动态 Qt、`out\build` 等其它方式，否则不要默认改用指南外的随意命令。

## 2. 目录概览

该目录是一个基于 `Qt/C++` 与 `CMake` 的桌面程序，核心工程文件包括：

- `CMakeLists.txt`
- `CMakePresets.json`
- `main.cpp`

主要模块大致如下：

- `main.cpp`：程序入口（翻译加载、启动页、主窗口显示顺序见 **§12**）
- `startupservice.*`：启动流程、网关端点登记、RabbitMQ、文件监控初始化（可传 `StartupSplash`）
- `startupsplash.*`：主窗口前的启动过渡界面
- `machinecode.*`：设备实例标识（机器码）派生，与 **§12.1** 一致
- `appconfig.*`：配置文件读写，默认配置生成（含 `GatewayAdvertisedIp`）
- `applogger.*`：本地日志
- `gatewayapiclient.*`：平台 HTTP API 访问
- `rabbitmqservice.*`：RabbitMQ AMQP 连接、消费与发布
- `rabbitmqdispatcher.*`：消息分发与本地动作执行
- `filemonitorservice.*`：目录/文件监听与结果处理
- `installscriptprocessor.*`：安装脚本与模板文件生成
- `rechargeprocessor.*`：充值处理，包含数据库更新逻辑
- `mainwindow.*` 与各 `*page.*`：UI 页面

## 3. 技术栈与运行特征

- 语言：`C++17`
- 构建：`CMake`
- UI：`Qt5/Qt6 Widgets`
- 网络：`Qt Network`
- 数据库：`Qt Sql`
- 并发：`Qt Concurrent`
- 国际化：`LinguistTools`

该网关承担的职责包括：

- 启动时校验网关绑定状态
- 调用平台接口获取分区、模板、分组、订单、设备信息
- 监听 RabbitMQ 队列并消费平台下发消息
- 向 RabbitMQ 发布订单、扫码、微信密保、转区、充值结果等消息
- 监听本地目录与文件变化
- 生成、覆盖、改写本地脚本、模板、结果文件
- 在部分充值场景直接连接数据库并执行更新

## 4. 重点源码区域

默认优先关注以下文件：

- `main.cpp`
- `startupservice.cpp`
- `appconfig.cpp`
- `gatewayapiclient.cpp`
- `rabbitmqservice.cpp`
- `rabbitmqdispatcher.cpp`
- `filemonitorservice.cpp`
- `installscriptprocessor.cpp`
- `rechargeprocessor.cpp`
- `settingspage.cpp`
- `orderpage.cpp`
- `partitionpage.cpp`
- `partitionmanagepage.cpp`
- `partitiondialog.*`：添加/编辑分区对话框（请求体字段、与模板联动）
- `partitiontemplatepage.cpp`
- `templatedialog.*`：模板添加/编辑对话框（赠送类表格单元格控件、红包 Tab 行为，见 **§12.7**）
- `groupmanagepage.cpp`（含 `GroupManageTableWidget`，见 **§12.5**）
- `scriptpage.cpp`（脚本保存与商户端共用平台存储，见 **§12.8**）
- `startupsplash.*`、`machinecode.*`：见 **§12**

### 4.1 分区类型（业务与 UI 约定）

- **实际业务范围**：本网关常见部署下 **只使用热血传奇**（对应模板 **`Type = 1`**），不按多游戏类型运营。
- **界面**：添加/编辑分区对话框 **不展示「分区类型」提示或选择**（有意去掉，见 `partitiondialog.*`），避免多余信息。
- **接口**：提交安装/更新分区时，请求体里的 **`Type` 仍按所选模板的 Type 写入**（与 TenantServer / 老网关语义一致），代码中 `m_partitionType` 与 `SyncPartitionKindUi()` 仍随模板类型处理路径、元宝蛋、脚本更新等可见性。
- **对代理与协作者的约束**：修改分区相关 UI 或逻辑时，**不要**擅自恢复「分区类型」展示，或默认扩展传世 / 传奇3 / 通用 SQL / Web 等多类型说明与流程，**除非当前任务里用户明确要求**。

### 4.2 已固化的产品行为（摘要）

设备标识、登记容错、启动页、日志降噪、设置项与分组管理 UI 等已按 **§12** 实现；**不要随意回退**为硬件机器码、登记失败即退出、恢复满屏连接/心跳日志、或恢复分组表内联编辑/系统原生分组对话框，**除非用户明确要求变更**。

## 5. 高风险点

### 5.1 数据库操作限制

本目录存在直接数据库写入逻辑，重点位置：

- `rechargeprocessor.cpp`

已知存在直接执行更新语句的实现，例如对业务库做充值加值处理。因此默认必须遵守：

- 未经用户明确允许，只能进行 `SELECT` 类只读操作
- 未经用户明确允许，不得执行任何写库行为

明确禁止的操作包括：

- `INSERT`
- `UPDATE`
- `DELETE`
- `MERGE`
- `TRUNCATE`
- `ALTER`
- `DROP`
- `CREATE`
- 迁移、初始化、批量导入导出

即使代码路径本身会自动执行写库逻辑，也不能为了“验证功能”而直接运行。

### 5.2 文件删除限制

- 删除文件前必须先获得用户明确允许
- 删除文件只能单个执行
- 删除文件只能移动到回收站
- 禁止永久删除
- 禁止递归删除目录
- 禁止批量清理脚本目录、日志目录、输出目录

### 5.3 配置与敏感信息

重点配置位置：

- `Libs\setting.config`（运行时生成/读取）
- `appconfig.cpp` 中默认配置逻辑

配置中可能包含：

- `RestUrl`
- `SecretKey`
- `SignKey`
- `SqlConnectionStr`
- `RabbitMqHost`
- `RabbitMqUser`
- `RabbitMqPassword`
- `Socket`
- 本地监控目录

未经用户明确允许，不要擅自修改：

- 生产或实际使用中的接口地址
- RabbitMQ 参数
- 数据库连接串
- 密钥
- 商户 Uuid、通讯端口（与平台 `GatewayEquips` 登记相关）
- **网关对外 IP**（`GatewayAdvertisedIp`，与设备实例标识、网页安装分区填写一致）
- 自动启动设置

### 5.4 外部系统与副作用

以下模块会产生真实副作用，必须格外谨慎：

- `gatewayapiclient.cpp`：调用平台接口
- `rabbitmqservice.cpp`：建立消息连接并真实收发
- `rabbitmqdispatcher.cpp`：消费消息后执行本地落地动作
- `filemonitorservice.cpp`：监控目录并驱动消息发送
- `installscriptprocessor.cpp`：创建/覆盖安装脚本、充值模板、目录结构
- `rechargeprocessor.cpp`：可能写本地文件，也可能直接写数据库

未经用户明确允许，不要主动触发：

- 真实 RabbitMQ 连接与消息收发验证
- 真实平台 API 写操作
- 网关注册/端点登记（`GatewayEndpoint/Register`）等会写平台设备表的接口
- 自动安装脚本生成
- 真实充值处理
- 微信密保、转区、扫码、代付等业务动作

## 6. 默认不应主动修改的内容

除非用户明确要求，否则不要主动编辑：

- `.git/`
- `.github/`
- `.qtcreator/`
- `.vscode/`
- `out/`
- 自动生成的构建产物
- 翻译产物或构建缓存

UI `.ui` 文件和 `CMake` 文件可以修改，但应先确认影响范围并尽量最小化。

## 7. 代码修改原则

- 先读调用链，再决定改动点
- 优先修复根因，不做无关重构
- 保持现有 Qt/C++ 风格，不强行引入与现有结构不一致的封装
- 与消息、文件监控、脚本生成相关的改动必须额外说明副作用
- 涉及配置字段变更时，要同时检查读写两侧是否一致
- 涉及文件路径时，优先保持当前目录结构和命名约定

## 8. 构建与验证建议

优先使用局部和非侵入式验证。

**强制约定（与用户一致）：** 在本目录凡需要 **配置 CMake、编译 gateway、打单文件/静态 Release 包** 时，**一律以 `docs/单文件打包指南.md` 为唯一依据**（脚本路径、静态 Qt 目录、`out\build_static_release` 等以该文档为准）。不要默认改用未在指南中出现的随意命令（例如随意的 `cmake -S . -B out\build` Debug/动态 Qt），**除非同一任务里用户明确要求其他方式**。

日常验证编译：按指南 **「日常重编 gateway」**，执行 `scripts\rebuild_gateway_static_release.bat`（脚本内会加载 VS、使用文档所述静态 Qt 与输出目录）。

### 8.1 推荐：日常重编（与指南一致）

在 **x64 已加载 VS 工具链** 的前提下，任选其一（细节仍以 **`docs/单文件打包指南.md`** 为准）：

**方式 A（首选，脚本内会查找 VS 自带 `cmake.exe` 并配置 Ninja + 静态 Qt）：**

```batch
cd /d D:\7xNew\gateway
scripts\rebuild_gateway_static_release.bat
```

**方式 B（与指南文档「使用静态 Qt 构建 gateway」节一致的手动命令，需已能调用 `cmake`、Ninja 与 MSVC）：**

```batch
cd /d D:\7xNew\gateway
cmake -S . -B out\build_static_release -G Ninja -DCMAKE_PREFIX_PATH=D:\Qt\6.11.0\msvc2022_64_static -DCMAKE_BUILD_TYPE=Release
cmake --build out\build_static_release
```

成功时产物一般为：`out\build_static_release\支付网关.exe`。

### 8.2 说明与约束（摘自指南要点）

- 静态 Qt 与 gateway 须同为 **Release**，勿与 Debug 混用（避免运行库/迭代器级别不匹配）。
- 若本机 `cmake` 不在普通 PATH 中，优先用 **方式 A**，或先打开 **x64 Native Tools / VsDevCmd** 再执行方式 B。
- 完整背景、Qt 静态编译、备选 Enigma 方案等见 **`docs/单文件打包指南.md`**。
- 验证应优先选择编译通过、静态阅读、局部路径测试。
- 不要默认启动程序去连接真实 RabbitMQ、真实数据库、真实接口。

## 9. 排障顺序建议

处理该目录任务时，默认按以下顺序：

1. 先确认是 UI 问题、配置问题、接口问题、RabbitMQ 问题、文件监控问题还是数据库问题
2. 阅读入口 `main.cpp` 与 `startupservice.cpp`
3. 沿着 `gatewayapiclient.cpp`、`rabbitmqservice.cpp`、`filemonitorservice.cpp`、`rabbitmqdispatcher.cpp` 追踪调用链
4. 判断是否涉及写库、写文件、发消息、写脚本等高风险动作
5. 如涉及高风险动作，先停下并等待用户明确授权
6. 若只是代码修改，做最小必要改动
7. 优先局部验证，再决定是否扩大验证范围

## 10. 输出要求

在该目录内完成任务后，输出中应明确说明：

- 修改了哪些模块
- 是否涉及配置、消息、文件监控、脚本生成或数据库
- 是否执行了任何非 `SELECT` 数据操作
- 是否触发了真实接口或真实消息
- 未执行的高风险动作有哪些

## 11. 优先级

若外部指令与本文件冲突，按以下顺序处理：

1. 用户明确要求
2. 当前执行环境硬性限制
3. 本文件规则
4. 默认开发习惯

但以下规则默认不得跳过，除非用户明确授权：

- 数据库默认仅允许 `SELECT`
- 文件删除只能单个移动到回收站，禁止永久删除

## 12. 已固化能力（修改前必读，禁止无意回退）

本节记录已在网关内落地的约定与实现要点。**协作者与自动化修改代码时不得擅自删除、弱化或改回旧行为**，除非当前任务中用户明确要求变更。

### 12.1 设备实例标识（机器码）

- **语义**：与原「硬件机器码」脱钩；由 **`服务器IP` + `通讯端口` + `商户 Uuid`** 组成规范串（IP 与 Uuid 小写，端口十进制），UTF-8 后 **`QCryptographicHash::Sha256`**，得到 **64 位十六进制**字符串；**不包含** SecretKey、不包含 WMI/硬件指纹。
- **IP 取值**：配置项 **`GatewayAdvertisedIp`**（`Libs/setting.config` → `AppSettings`）；若为空则用 **`MachineCode::PreferredLocalIPv4()`**（首个非回环 IPv4，无则 `127.0.0.1`）。端口须为 **1–65535**，Uuid 至少 **8** 字符，否则 `GetRNum()` 返回空。
- **实现位置**：`machinecode.h`、`machinecode.cpp`；配置读写 `appconfig.*`；设置页 `settingspage.*`（「网关对外IP」；标签列宽约 **100px**；配置为空时输入框 **占位提示**「留空则使用本机：」+ `MachineCode::PreferredLocalIPv4()` 的文本）。
- **平台影响**：与历史硬件式机器码 **不兼容**；上线或改 IP/端口/Uuid 后，分区绑定与平台侧 `MachineCode` / 设备记录需按业务重新对齐。
- **禁止回退**：不要恢复为 CPU/磁盘/BIOS/MAC + SecretKey 等旧派生逻辑，除非用户书面要求。

### 12.2 网关端点登记（`GatewayEndpoint/Register`）与「占用」容错

- **占用判定**：`GatewayApiClient::IsGatewayEndpointOccupiedError()` — 响应体含 **「已被当前商户下其他网关实例占用」**（含 JSON `message` 字段）即视为占用。
- **启动时**（`startupservice.cpp`）：若登记失败且为占用，**仅写日志**，**不弹阻塞对话框**，**继续启动** HTTP、RabbitMQ、文件监控与主界面；非占用类失败仍按原逻辑失败退出。
- **设置保存时**（`settingspage.cpp`）：登记失败且为占用时 **仅日志**；其它失败仍 `QMessageBox::warning`。
- **HTTP 层**：`ExecuteTextRequest` 对占用类错误 **不再打**通用「接口请求失败」日志，避免与启动日志重复。
- **禁止回退**：不要把「占用」改回必现的 `QMessageBox::critical` 并 `return false` 阻止进主界面，除非用户明确要求。

### 12.3 启动过渡界面

- **流程**：`main.cpp` 在 `StartupService::RunStartupSequence` **之前**构造并显示 `StartupSplash`；启动序列各阶段可更新文案并 `processEvents`；成功后 **淡出**再 **`MainWindow::show`**。
- **实现**：`startupsplash.h`、`startupsplash.cpp`；`RunStartupSequence(StartupSplash *splash = nullptr)`（`startupservice.h`）。
- **弹窗**：启动失败类 `QMessageBox` 前会 **隐藏**启动页，避免遮挡。
- **禁止回退**：不要删启动页改回「长时间无反馈直出主窗口」，除非用户明确要求。

### 12.4 日志策略（降噪）

- **已削减**：RabbitMQ AMQP 握手/心跳/逐帧调试、消费侧原始 payload 刷屏、文件监控「检测到变化/已注册路径」等例行 INFO；启动流水式「连接成功」叠句；`gatewayhttpserver` 监听成功句；部分中间步骤成功句等。
- **保留**：分区创建成功/失败、充值汇总主行、补发结果通知成功/失败、业务与接口 **错误**、JSON 解析失败关键片段、RabbitMQ 断连/错误等。
- **禁止回退**：不要为「调试方便」在无用户要求时把上述噪音整段恢复；若需诊断应使用局部、可开关或提高级别的日志方案。

### 12.5 分组管理 UI（表格与添加/编辑对话框）

- **表格禁止内联编辑**：`groupmanagepage.ui` 将 `groupTable` **提升**为 **`GroupManageTableWidget`**（`groupmanagepage.h`），在 **Qt 6** 下重写受保护成员 **`bool edit(const QModelIndex &, EditTrigger, QEvent *)`** 并 **`return false`**（公有槽 `void edit(QModelIndex)` **不可 override**，无效）。另设 **`ReadOnlyTableItemDelegate`** 双保险。
- **样式**：`groupTable` 的 **`objectName`** 为 **`groupManageTable`**；`mainwindow.cpp` 全局样式中为 **`QTableWidget#groupManageTable::item:selected`** 配置与分区模板表一致的选中底色，避免默认焦点/编辑态像「白框编辑器」。
- **添加/编辑分组**：`RunGroupNameDialog` 使用 **无边框主题卡片**（酒红标题与主按钮、灰次要按钮、输入框焦点色与主界面一致），支持 **×** 与 **Esc** 关闭，相对主窗口居中。
- **禁止回退**：不要把分组表改回纯 `QTableWidget` 且依赖 `NoEditTriggers` 单防（Qt 6 仍会进编辑）；不要把分组对话框改回无样式的系统默认 `QDialog`，除非用户明确要求。

### 12.6 同机多进程与同目录单实例

- **同一部署目录**（`applicationDirPath` 相同）：`gatewaysingleinstance.*` 使用 **`QLocalServer` 唤醒** + **`QLockFile`**（锁文件在 **系统临时目录**，按安装路径哈希命名，**不在 exe 旁落盘**）；第二次启动会唤醒已有窗口并退出。
- **不同部署目录**仍可同时各跑一份（锁与管道名均含路径哈希）。
- **同一目录**仍仅一份 `Libs/setting.config`，多开同目录会抢端口与配置；刻意多开需不同目录、不同 HTTP 端口、各自配置。
- 默认队列名在配置为占位 `gateway.queue` 时会 **按机器码解析**（见 `rabbitmqservice.cpp` `ResolveQueueName`），端口不同则机器码不同、队列名一般不同；文件监控路径若重叠仍可能重复处理，需自行规避。

### 12.7 模板编辑对话框（`templatedialog.*`）

- **赠送表单元格对齐**：「附加赠送」「积分赠送」等表中，赠送方式 / 网站显示 / 游戏显示等列使用 **`AttachAlignedCellWidget`**（容器 + 横向布局 + 垂直居中）挂载 `QComboBox` / `QCheckBox`，减轻控件相对格线偏移；勿在已套容器后仍假定 `cellWidget` 即为控件本体。
- **读表与提交**：**`TableComboIntData` / `TableCheckBool`** 通过 **`FindComboInCellWidget` / `FindCheckInCellWidget`**（含 `findChild`）解析单元格，与上述容器方案配套；若改回裸 `setCellWidget(combo)` 或只改 UI 不更新这两处读取逻辑，会导致 JSON 字段读错。
- **红包赠送 Tab**：**`SyncGiftTabEnabledStates`** 对 **`m_redPacketTabBody` 始终 `setEnabled(true)`**，使未勾选「红包赠送」时仍可编辑红包 NPC 表、详情区间表及「添加行 / 删除行」；**`RedPacketState` 等勾选状态仍写入提交 JSON**，与装备/渠道等赠送 Tab「整页随主开关禁用」刻意区分。
- **禁止回退**：不要把红包 body 改回随 `m_redPacketStateCheck` 整块禁用（会导致添加行等按钮无效）；不要拆除容器对齐方案却不同步更新 `TableComboIntData` / `TableCheckBool`。

### 12.8 脚本页保存与商户端（平台契约）

- **写入同源**：`scriptpage.cpp` 使用 **`GatewayApiClient::SaveClientInstallScriptFile`**（`POST /api/Client/SaveClientInstallScriptFile`，带 SecretKey）；TenantServer **`ClientController.SaveClientInstallScriptFile`** 内部转为 **`InstallScriptFileSaveDto`** 并调用 **`InstallScriptTemplateRepository.SaveFileAsync`**，与商户端 **`InstallScriptTemplate/SaveInstallScriptFile`** 一致。拉取合并脚本同理：**`GetClientInstallScriptFiles`** 与 **`GetInstallScriptFiles`** 均使用 **`GetMergedFilesAsync`**（默认 + 商户覆盖）。
- **产品差异**：网关脚本页为 **固定菜单项**（NPC / 充值 / 附加 / 积分 / 装备，通区时充值文件名为 `通区测试充值.txt`），**未实现**商户端脚本编辑页的 **「恢复默认」**（`ResetInstallScriptFile`）等能力；属界面与功能范围差异，不是两套存储。

