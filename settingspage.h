#ifndef SETTINGSPAGE_H
#define SETTINGSPAGE_H

#include <QWidget>

QT_BEGIN_NAMESPACE
class QCheckBox;
namespace Ui {
class SettingsPage;
}
QT_END_NAMESPACE

class SettingsPage : public QWidget
{
    Q_OBJECT

public:
    explicit SettingsPage(QWidget *parent = nullptr);
    ~SettingsPage() override;

signals:
    void configReloaded();
    /// 设置页「检查更新」流程结束（含用户关闭提示），用于刷新主窗口版本状态
    void gatewayUpdateCheckFinished();

private:
    void LoadConfig();
    void SaveAndReloadConfig();
    void OnCheckUpdateClicked();
    void ApplyWebsiteValueDisplay(const QString &urlFromConfig);
    void SyncGatewayAdvertisedIpFieldMode();

    Ui::SettingsPage *ui;
    QCheckBox *m_openLogCheckBox = nullptr;
    QCheckBox *m_openOrderReissueCheckBox = nullptr;
    QCheckBox *m_weixinZqCheckBox = nullptr;
    QCheckBox *m_weixinMbCheckBox = nullptr;
    QCheckBox *m_bootUpCheckBox = nullptr;
    QCheckBox *m_gameScanCheckBox = nullptr;
};

#endif // SETTINGSPAGE_H
