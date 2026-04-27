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

private:
    void LoadConfig();
    void SaveAndReloadConfig();
    void ChangeGatewayId();

    Ui::SettingsPage *ui;
    QCheckBox *m_openLogCheckBox = nullptr;
    QCheckBox *m_openOrderReissueCheckBox = nullptr;
    QCheckBox *m_weixinZqCheckBox = nullptr;
    QCheckBox *m_weixinMbCheckBox = nullptr;
    QCheckBox *m_bootUpCheckBox = nullptr;
    QCheckBox *m_gameScanCheckBox = nullptr;
};

#endif // SETTINGSPAGE_H
