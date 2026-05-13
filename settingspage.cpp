#include "settingspage.h"
#include "ui_settingspage.h"

#include "appconfig.h"
#include "applogger.h"
#include "appversion.h"
#include "gatewayapiclient.h"
#include "gatewayupdatecontroller.h"
#include "machinecode.h"
#include "startupservice.h"

#include <QCheckBox>
#include <QFont>
#include <QFontMetrics>
#include <QLabel>
#include <QLineEdit>
#include <QLayout>
#include <QMessageBox>
#include <QHBoxLayout>
#include <QSizePolicy>
#include <QUrl>

SettingsPage::SettingsPage(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::SettingsPage)
{
    ui->setupUi(this);

    ui->settingsCard->setObjectName(QStringLiteral("settingsCard"));
    ui->settingsToggleArea->setObjectName(QStringLiteral("settingsToggleArea"));

    /// 与样式表 settingsField / settingsFormLabel 一致；略矮、行距加大，避免表单区显得挤
    constexpr int kSettingsFieldH = 24;
    const QFontMetrics labelFm(ui->merchantNameLabel->font());
    auto labelTextWidth = [&labelFm](const QString &s) { return labelFm.horizontalAdvance(s); };
    int labelCol0Min = 0;
    for (const QString &s : {ui->merchantNameLabel->text(),
                             ui->passwordLabel->text(),
                             ui->apiKeyLabel->text(),
                             ui->gatewayIpLabel->text(),
                             ui->portLabel->text()}) {
        labelCol0Min = qMax(labelCol0Min, labelTextWidth(s));
    }
    constexpr int kLabelColPadding = 8;
    for (QLabel *label : {ui->merchantNameLabel,
                          ui->websiteLabel,
                          ui->portLabel,
                          ui->gatewayIpLabel,
                          ui->passwordLabel,
                          ui->apiKeyLabel}) {
        label->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
        label->setFixedHeight(kSettingsFieldH);
        label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        label->setProperty("settingsFormLabel", true);
    }

    ui->merchantNameValue->setMinimumHeight(kSettingsFieldH);
    ui->merchantNameValue->setMaximumHeight(kSettingsFieldH);
    ui->merchantNameValue->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    ui->merchantNameValue->setProperty("settingsMutedValue", true);
    ui->merchantNameValue->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    ui->merchantNameValue->setWordWrap(false);

    ui->websiteValue->setMinimumHeight(kSettingsFieldH);
    ui->websiteValue->setMaximumHeight(kSettingsFieldH);
    ui->websiteValue->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    ui->websiteValue->setProperty("settingsMutedValue", true);
    ui->websiteValue->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    ui->websiteValue->setWordWrap(false);

    const auto applyFieldStyle = [kSettingsFieldH](QLineEdit *edit) {
        edit->setProperty("settingsField", true);
        edit->setFixedHeight(kSettingsFieldH);
        edit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    };

    applyFieldStyle(ui->passwordEdit);
    applyFieldStyle(ui->apiKeyEdit);
    applyFieldStyle(ui->gatewayAdvertisedIpEdit);
    applyFieldStyle(ui->portEdit);
    ui->portEdit->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    ui->portEdit->setFixedWidth(100);

    const auto createCheckBox = [this](const QString &text) {
        return new QCheckBox(text, this);
    };

    m_openLogCheckBox = createCheckBox(QStringLiteral("开启网关日志"));
    m_openOrderReissueCheckBox = createCheckBox(QStringLiteral("开启订单补发"));
    m_weixinZqCheckBox = createCheckBox(QStringLiteral("开启微信转区"));
    m_weixinMbCheckBox = createCheckBox(QStringLiteral("开启微信密保"));
    m_bootUpCheckBox = createCheckBox(QStringLiteral("开机自启"));
    m_gameScanCheckBox = createCheckBox(QStringLiteral("开启游戏扫码"));

    {
        auto *toggleLayout = ui->settingsToggleAreaLayout;
        const Qt::Alignment a = Qt::AlignLeft | Qt::AlignVCenter;
        toggleLayout->addWidget(m_openLogCheckBox, 0, 0, a);
        toggleLayout->addWidget(m_openOrderReissueCheckBox, 0, 1, a);
        toggleLayout->addWidget(m_weixinMbCheckBox, 0, 2, a);
        toggleLayout->addWidget(m_weixinZqCheckBox, 1, 0, a);
        toggleLayout->addWidget(m_gameScanCheckBox, 1, 1, a);
        toggleLayout->addWidget(m_bootUpCheckBox, 1, 2, a);
        toggleLayout->setColumnStretch(0, 1);
        toggleLayout->setColumnStretch(1, 1);
        toggleLayout->setColumnStretch(2, 1);
    }

    // 纵向多行 QHBoxLayout，避免 QGridLayout 跨列 + 对齐在部分环境下叠行
    const int labelCol0W = labelCol0Min + kLabelColPadding;
    for (QLabel *label : {ui->merchantNameLabel, ui->passwordLabel, ui->apiKeyLabel, ui->gatewayIpLabel}) {
        label->setMinimumWidth(labelCol0W);
    }
    ui->portLabel->setMinimumWidth(labelTextWidth(ui->portLabel->text()) + kLabelColPadding);
    // 与左侧「商户昵称」等列宽一致，两行标题区横向对齐
    ui->websiteLabel->setMinimumWidth(labelCol0W);

    // 该行两列「标签+输入框」，部分环境下标题相对框体略偏上，由主窗口样式表 padding-top 微调
    ui->gatewayIpLabel->setObjectName(QStringLiteral("settingsGatewayIpLabel"));
    ui->portLabel->setObjectName(QStringLiteral("settingsPortLabel"));

    ui->settingsCard->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    ui->settingsRowMerchant->setStretch(1, 1);
    ui->settingsRowMerchant->setStretch(4, 1);
    ui->settingsRowPassword->setStretch(1, 1);
    ui->settingsRowApiKey->setStretch(1, 1);
    ui->settingsRowIpPort->setStretch(1, 1);

    ui->currentVersionValue->setMinimumHeight(kSettingsFieldH);
    ui->currentVersionValue->setMaximumHeight(kSettingsFieldH);
    ui->currentVersionValue->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    ui->currentVersionValue->setProperty("settingsMutedValue", true);
    ui->currentVersionValue->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);

    ui->checkUpdateButton->setProperty("settingsSecondary", true);
    ui->checkUpdateButton->setFixedHeight(kSettingsFieldH);

    for (QLabel *footerLbl : {ui->sectionUpdateLabel, ui->currentVersionLabel}) {
        footerLbl->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
        footerLbl->setFixedHeight(kSettingsFieldH);
        footerLbl->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    }

    const auto vcenterRowWidgets = [](QHBoxLayout *row) {
        if (!row) {
            return;
        }
        for (int i = 0; i < row->count(); ++i) {
            if (QLayoutItem *item = row->itemAt(i)) {
                if (QWidget *w = item->widget()) {
                    row->setAlignment(w, Qt::AlignVCenter);
                }
            }
        }
    };
    for (QHBoxLayout *row : {ui->settingsRowMerchant,
                             ui->settingsRowPassword,
                             ui->settingsRowApiKey,
                             ui->settingsRowIpPort}) {
        vcenterRowWidgets(row);
    }
    vcenterRowWidgets(ui->settingsFooterLayout);

    // 与上方「标签列宽 + 行间距」对齐，避免勾选说明缩在标签列下方
    ui->gatewayIpAutoRowLayout->setContentsMargins(labelCol0W + 10, 4, 0, 0);

    ui->confirmSettingsButton->setProperty("settingsConfirm", true);

    connect(ui->gatewayAdvertisedIpAutoDetectCheckBox, &QCheckBox::toggled, this, &SettingsPage::SyncGatewayAdvertisedIpFieldMode);

    LoadConfig();

    connect(ui->confirmSettingsButton, &QPushButton::clicked, this, &SettingsPage::SaveAndReloadConfig);
    connect(ui->checkUpdateButton, &QPushButton::clicked, this, &SettingsPage::OnCheckUpdateClicked);
}

SettingsPage::~SettingsPage()
{
    delete ui;
}

void SettingsPage::SyncGatewayAdvertisedIpFieldMode()
{
    const bool autodetect = ui->gatewayAdvertisedIpAutoDetectCheckBox->isChecked();
    ui->gatewayAdvertisedIpEdit->setReadOnly(autodetect);
    if (autodetect) {
        AppConfigValues basis = AppConfig::Load();
        basis.secretKey = ui->passwordEdit->text().trimmed();
        ui->gatewayAdvertisedIpEdit->setText(AppConfig::ResolveGatewayAdvertisedIpForAutoDetect(basis));
        ui->gatewayAdvertisedIpEdit->setPlaceholderText(QString());
        ui->gatewayAdvertisedIpEdit->setToolTip(
            QStringLiteral("优先通过租户平台接口（与本网关访问平台同线路）获取出口 IPv4；不可用时再使用外网探测服务；仍失败则用本机网卡 IPv4。关闭勾选后可手动填写固定 IP。"));
    } else {
        ui->gatewayAdvertisedIpEdit->setToolTip(QString());
        const QString t = ui->gatewayAdvertisedIpEdit->text().trimmed();
        ui->gatewayAdvertisedIpEdit->setPlaceholderText(
            t.isEmpty() ? QStringLiteral("留空则使用本机：%1").arg(MachineCode::PreferredLocalIPv4()) : QString());
    }
}

void SettingsPage::ApplyWebsiteValueDisplay(const QString &urlFromConfig)
{
    const QString t = urlFromConfig.trimmed();
    ui->websiteValue->setTextFormat(Qt::RichText);
    if (t.isEmpty()) {
        ui->websiteValue->setText(QStringLiteral("—"));
        ui->websiteValue->setOpenExternalLinks(false);
        return;
    }
    const QUrl u = QUrl::fromUserInput(t);
    const QString href = QString::fromUtf8(u.toEncoded(QUrl::FullyEncoded));
    ui->websiteValue->setText(QStringLiteral("<a href=\"%1\">%2</a>").arg(href, QString(t).toHtmlEscaped()));
    ui->websiteValue->setOpenExternalLinks(true);
}

void SettingsPage::LoadConfig()
{
    const auto values = AppConfig::Load();

    ui->merchantNameValue->setText(values.merchantName);
    ApplyWebsiteValueDisplay(values.website);
    ui->portEdit->setText(values.port);
    ui->gatewayAdvertisedIpAutoDetectCheckBox->blockSignals(true);
    ui->gatewayAdvertisedIpAutoDetectCheckBox->setChecked(values.gatewayAdvertisedIpAutoDetect);
    ui->gatewayAdvertisedIpAutoDetectCheckBox->blockSignals(false);
    if (!values.gatewayAdvertisedIpAutoDetect) {
        ui->gatewayAdvertisedIpEdit->setText(values.gatewayAdvertisedIp.trimmed());
    }
    SyncGatewayAdvertisedIpFieldMode();
    ui->passwordEdit->setText(values.secretKey);
    ui->apiKeyEdit->setText(values.signKey);
    ui->currentVersionValue->setText(GatewayApp::versionDisplay());

    m_openLogCheckBox->setChecked(values.isOpenLog);
    m_openOrderReissueCheckBox->setChecked(values.isOpenOrderReissue);
    m_weixinZqCheckBox->setChecked(values.isWeixinZq);
    m_weixinMbCheckBox->setChecked(values.isWeixinMb);
    m_bootUpCheckBox->setChecked(AppConfig::IsAutoStartEnabled());
    m_gameScanCheckBox->setChecked(values.isSm);
}

void SettingsPage::SaveAndReloadConfig()
{
    quint16 portProbe = 0;
    QString portProbeError;
    if (!StartupService::TryParseHttpListenPort(ui->portEdit->text(), &portProbe, &portProbeError)) {
        QMessageBox::warning(this, QStringLiteral("提示"), portProbeError);
        return;
    }

    AppConfigValues values = AppConfig::Load();
    // 商户昵称由平台接口同步，不从界面写回
    // 充值网站仅展示，配置中的地址仍由平台/其它流程维护，保存时不改写
    values.port = ui->portEdit->text().trimmed();
    values.gatewayAdvertisedIpAutoDetect = ui->gatewayAdvertisedIpAutoDetectCheckBox->isChecked();
    values.secretKey = ui->passwordEdit->text().trimmed();
    values.signKey = ui->apiKeyEdit->text().trimmed();
    if (values.gatewayAdvertisedIpAutoDetect) {
        values.gatewayAdvertisedIp = AppConfig::ResolveGatewayAdvertisedIpForAutoDetect(values);
    } else {
        values.gatewayAdvertisedIp = ui->gatewayAdvertisedIpEdit->text().trimmed();
    }
    // 商户 Uuid 仍保存在配置文件中（机器码/登记等使用），不在本页展示与编辑
    values.isOpenLog = m_openLogCheckBox->isChecked();
    values.isOpenOrderReissue = m_openOrderReissueCheckBox->isChecked();
    values.isWeixinZq = m_weixinZqCheckBox->isChecked();
    values.isWeixinMb = m_weixinMbCheckBox->isChecked();
    values.bootUp = m_bootUpCheckBox->isChecked();
    values.isSm = m_gameScanCheckBox->isChecked();
    // 自定义清单已移除；UpdateManifestUrl 仍随 Load 的 values 写回，不在此页编辑

    GatewayApiClient api;
    api.SyncClientMerchantIdentity(values, nullptr);

    AppConfig::Save(values);
    AppConfig::SetAutoStartEnabled(values.bootUp);

    QString syncError;
    if (!api.RegisterGatewayEndpoint(values, &syncError)) {
        if (GatewayApiClient::IsGatewayEndpointOccupiedError(syncError)) {
            AppLogger::WriteLog(QStringLiteral("同步网关到平台跳过：IP 与通讯端口已被本商户下其他网关占用。%1").arg(syncError));
        } else {
            AppLogger::WriteLog(QStringLiteral("同步网关端口到平台失败：%1").arg(syncError));
            QMessageBox::warning(this,
                                 QStringLiteral("提示"),
                                 QStringLiteral("配置已保存到本机，但同步到平台失败（网页端网关列表可能仍是旧端口）：%1").arg(syncError));
        }
    }

    StartupService::ApplySavedAppConfigReload();

    LoadConfig();
    emit configReloaded();
}

void SettingsPage::OnCheckUpdateClicked()
{
    GatewayUpdateController ctl;
    ctl.checkAndOfferInstall(this, QString());
    emit gatewayUpdateCheckFinished();
}
