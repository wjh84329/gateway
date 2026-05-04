# gateway 目录 AGENTS.md

## 0. 项目背景

新网关：D:\7xNew\gateway，开发语言是c++，使用的qt
老网关：D:\7xNew\Qynet.GamePlatform\Gateway 开发语言是c#，框架是.net framework 4.6.2
老项目控制器：D:\7xNew\Qynet.GamePlatform\TenantServer\Controllers\ClientController.cs,使用的是.net core 6.0

在重构过程中，需要对齐老项目的功能实现，特别是 `RabbitMessageEnum.ManualReissue` 等消息处理逻辑。

## 1. 目的

本文件用于约束在新平台网关目录 `D:\7xNew\gateway` 内工作的智能代理、自动化脚本与协作开发者，确保在阅读、修改、构建、测试、排障、数据库访问和文件处理时遵循统一规则。

该目录是新平台网关，虽然允许在用户任务范围内进行代码修改，但仍需坚持最小改动、优先局部验证、谨慎处理外部副作用。

## 2. 目录概览

该目录是一个基于 `Qt/C++` 与 `CMake` 的桌面程序，核心工程文件包括：

- `CMakeLists.txt`
- `CMakePresets.json`
- `main.cpp`

主要模块大致如下：

- `main.cpp`：程序入口
- `startupservice.*`：启动流程、网关绑定、RabbitMQ 启动、文件监控初始化
- `appconfig.*`：配置文件读写，默认配置生成
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
- `partitiontemplatepage.cpp`
- `groupmanagepage.cpp`
- `scriptpage.cpp`

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

- `Config.ini`（运行时生成/读取）
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
- 网关标识
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
- 网关绑定操作
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

可参考的构建方式：

```powershell
cmake -S . -B out\build
cmake --build out\build --config Debug
cmake --build out\build --config Release
```

说明：

- 该项目依赖本机 Qt、编译器和 CMake 环境
- 验证应优先选择编译通过、静态阅读、局部路径测试
- 不要默认启动程序去连接真实 RabbitMQ、真实数据库、真实接口

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
