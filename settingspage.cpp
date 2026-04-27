#include "settingspage.h"
#include "ui_settingspage.h"

#include "appconfig.h"
#include "changegatewayiddialog.h"

#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>

SettingsPage::SettingsPage(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::SettingsPage)
{
    ui->setupUi(this);

    ui->gatewayIdValue->setObjectName("gatewayIdValue");
    for (QLabel *label : {ui->merchantNameLabel, ui->gatewayIdLabel}) {
        label->setFixedWidth(68);
        label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    }
    for (QLabel *label : {ui->websiteLabel, ui->portLabel, ui->passwordLabel, ui->apiKeyLabel}) {
        label->setFixedWidth(68);
        label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    }

    ui->merchantNameValue->setMinimumWidth(120);
    ui->changeGatewayButton->setProperty("settingsPrimary", true);
    ui->changeGatewayButton->setFixedSize(118, 30);

    const auto applyFieldStyle = [](QLineEdit *edit) {
        edit->setProperty("settingsField", true);
        edit->setFixedSize(316, 30);
    };

    applyFieldStyle(ui->websiteEdit);
    applyFieldStyle(ui->portEdit);
    applyFieldStyle(ui->passwordEdit);
    applyFieldStyle(ui->apiKeyEdit);

    const auto createCheckBox = [this](const QString &text) {
        return new QCheckBox(text, this);
    };

    m_openLogCheckBox = createCheckBox(QStringLiteral("开启网关日志"));
    m_openOrderReissueCheckBox = createCheckBox(QStringLiteral("开启订单补发"));
    m_weixinZqCheckBox = createCheckBox(QStringLiteral("开启微信转区"));
    m_weixinMbCheckBox = createCheckBox(QStringLiteral("开启微信密保"));
    m_bootUpCheckBox = createCheckBox(QStringLiteral("开机自启"));
    m_gameScanCheckBox = createCheckBox(QStringLiteral("开启游戏扫码"));

    ui->settingsOptionLayout->addWidget(m_openLogCheckBox, 0, 0);
    ui->settingsOptionLayout->addWidget(m_openOrderReissueCheckBox, 0, 1);
    ui->settingsOptionLayout->addWidget(m_weixinZqCheckBox, 1, 0);
    ui->settingsOptionLayout->addWidget(m_weixinMbCheckBox, 1, 1);
    ui->settingsOptionLayout->addWidget(m_bootUpCheckBox, 2, 0);
    ui->settingsOptionLayout->addWidget(m_gameScanCheckBox, 2, 1);

    ui->confirmSettingsButton->setProperty("settingsConfirm", true);
    ui->confirmSettingsButton->setFixedSize(98, 34);

    LoadConfig();

    connect(ui->changeGatewayButton, &QPushButton::clicked, this, &SettingsPage::ChangeGatewayId);
    connect(ui->confirmSettingsButton, &QPushButton::clicked, this, &SettingsPage::SaveAndReloadConfig);
}

SettingsPage::~SettingsPage()
{
    delete ui;
}

void SettingsPage::LoadConfig()
{
    const auto values = AppConfig::Load();

    ui->merchantNameValue->setText(values.merchantName);
    ui->gatewayIdValue->setText(values.gatewayId);
    ui->websiteEdit->setText(values.website);
    ui->portEdit->setText(values.port);
    ui->passwordEdit->setText(values.secretKey);
    ui->apiKeyEdit->setText(values.signKey);

    m_openLogCheckBox->setChecked(values.isOpenLog);
    m_openOrderReissueCheckBox->setChecked(values.isOpenOrderReissue);
    m_weixinZqCheckBox->setChecked(values.isWeixinZq);
    m_weixinMbCheckBox->setChecked(values.isWeixinMb);
    m_bootUpCheckBox->setChecked(AppConfig::IsAutoStartEnabled());
    m_gameScanCheckBox->setChecked(values.isSm);
}

void SettingsPage::SaveAndReloadConfig()
{
    AppConfigValues values = AppConfig::Load();
    values.merchantName = ui->merchantNameValue->text();
    values.gatewayId = ui->gatewayIdValue->text();
    values.website = ui->websiteEdit->text().trimmed();
    values.port = ui->portEdit->text().trimmed();
    values.secretKey = ui->passwordEdit->text().trimmed();
    values.signKey = ui->apiKeyEdit->text().trimmed();
    values.isOpenLog = m_openLogCheckBox->isChecked();
    values.isOpenOrderReissue = m_openOrderReissueCheckBox->isChecked();
    values.isWeixinZq = m_weixinZqCheckBox->isChecked();
    values.isWeixinMb = m_weixinMbCheckBox->isChecked();
    values.bootUp = m_bootUpCheckBox->isChecked();
    values.isSm = m_gameScanCheckBox->isChecked();

    AppConfig::Save(values);
    AppConfig::SetAutoStartEnabled(values.bootUp);
    LoadConfig();
    emit configReloaded();
}

void SettingsPage::ChangeGatewayId()
{
    auto dialog = new ChangeGatewayIdDialog(ui->gatewayIdValue->text(), this);
    if (dialog->exec() == QDialog::Accepted) {
        const QString gatewayId = dialog->getGatewayId();
        if (!gatewayId.isEmpty()) {
            ui->gatewayIdValue->setText(gatewayId);
        }
    }
}
