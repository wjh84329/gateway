#ifndef GATEWAYUPDATECONTROLLER_H
#define GATEWAYUPDATECONTROLLER_H

#include <QObject>
#include <QString>

class QWidget;

/// 在线更新：拉取 HTTPS/HTTP 上的 JSON 清单，对比版本，下载安装包并在 Windows 下替换当前 exe 后重启。
class GatewayUpdateController final : public QObject
{
public:
    explicit GatewayUpdateController(QObject *parent = nullptr);

    /// 阻塞直至用户关闭提示或更新流程结束；请在非 UI 关键路径调用（如按钮槽内）。
    void checkAndOfferInstall(QWidget *dialogParent, const QString &manifestUrl);

    /// 可在后台线程调用：按当前配置请求 GetGatewayInstallerUpdateInfo，生成主窗口左下角状态文案（与 checkAndOfferInstall 同源）。
    static QString ComputeMainWindowUpdateStatusLineText();
};

#endif
