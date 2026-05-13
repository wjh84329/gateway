#ifndef TEMPLATEDIALOG_H
#define TEMPLATEDIALOG_H

#include <QDialog>
#include <QJsonObject>
#include <QPoint>

class QEvent;
class QTabWidget;
class QLineEdit;
class QComboBox;
class QCheckBox;
class QTableWidget;
class QPushButton;
class QWidget;
class QFormLayout;

/// 新建 / 编辑分区模板（请求体对齐 TenantServer `TempAndProductViewModel` + 旧版 `InstallTemplate`）
class TemplateDialog : public QDialog
{
    Q_OBJECT

public:
    /// @param templateId 0 表示新建；否则为编辑已有模板 id
    /// @param preloadTemplate 编辑时传入 `GetClientSignalTemplate` 的 JSON；新建时传空对象
    explicit TemplateDialog(int templateId, const QJsonObject &preloadTemplate, QWidget *parent = nullptr);
    ~TemplateDialog() override;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void OnCurrencyChanged(int index);
    void OnEngineChanged(int index);
    void OnConfirm();

private:
    void BuildUi();
    void LoadEngines();
    void LoadFromJson(const QJsonObject &o);
    void ApplyNewTemplateDefaults();
    void LoadProductsIntoChannelTable();
    void LoadTempRatesIntoChannelTable(const QJsonArray &rates);
    void LoadInstallScanModelOptions();
    void ApplyThemedFieldStyles();
    void SetupAdditionalIntegralRowWidgets(QTableWidget *table, int row, int showVal = -1, int typeVal = -1, int webAsInt = -1);
    void SyncWxmbScanWidgets();
    void SyncTongQuWidgets();
    void SyncGiftTabEnabledStates();

    QJsonObject BuildSubmitJson();

    int m_templateId = 0;
    QJsonObject m_sourceTemplate;

    QWidget *m_titleWidget = nullptr;
    bool m_dragging = false;
    QPoint m_dragOffset;

    QTabWidget *m_tabs = nullptr;

    QWidget *m_basicTab = nullptr;
    QFormLayout *m_basicFormLayout = nullptr;
    QLineEdit *m_nameEdit = nullptr;
    QLineEdit *m_gameNameEdit = nullptr;
    QComboBox *m_typeCombo = nullptr;
    QComboBox *m_currencyCombo = nullptr;
    QLineEdit *m_scriptCommandEdit = nullptr;
    QLineEdit *m_ratioEdit = nullptr;
    QLineEdit *m_minAmountEdit = nullptr;
    QLineEdit *m_maxAmountEdit = nullptr;
    QComboBox *m_engineCombo = nullptr;
    QLineEdit *m_browserCommandEdit = nullptr;
    QLineEdit *m_payDirEdit = nullptr;
    QLineEdit *m_tongQuDirEdit = nullptr;
    QLineEdit *m_dirEdit = nullptr;
    QCheckBox *m_isTongQuCheck = nullptr;
    QComboBox *m_dirModeCombo = nullptr;
    QCheckBox *m_isTestCheck = nullptr;
    QCheckBox *m_isBetchCheck = nullptr;
    QLineEdit *m_betchEdit = nullptr;
    QLineEdit *m_safetyMoneyEdit = nullptr;
    QComboBox *m_rechargeWayCombo = nullptr;
    QComboBox *m_equipMethodCombo = nullptr;
    QComboBox *m_giveWayCombo = nullptr;
    QCheckBox *m_showAdditionalCheck = nullptr;
    QCheckBox *m_showEquipCheck = nullptr;
    QCheckBox *m_isShowCheck = nullptr;
    QCheckBox *m_showIntegralCheck = nullptr;
    QCheckBox *m_giveStateCheck = nullptr;
    QCheckBox *m_isContainsCheck = nullptr;
    QComboBox *m_templateColorCombo = nullptr;
    QComboBox *m_installModelCombo = nullptr;
    QComboBox *m_scanModelCombo = nullptr;
    QCheckBox *m_isWxmbCheck = nullptr;
    QCheckBox *m_isScanCheck = nullptr;
    QComboBox *m_isShowGlodCombo = nullptr;
    QComboBox *m_giveOptionCombo = nullptr;
    QComboBox *m_giveOptionStateCombo = nullptr;
    QCheckBox *m_redPacketStateCheck = nullptr;
    QCheckBox *m_redPacketAdditionalCheck = nullptr;
    QCheckBox *m_redPacketEquipCheck = nullptr;
    QCheckBox *m_redPacketIntegralCheck = nullptr;

    QWidget *m_npcTab = nullptr;
    QTableWidget *m_npcTable = nullptr;
    QWidget *m_redPacketTab = nullptr;
    QTableWidget *m_redNpcTable = nullptr;
    QTableWidget *m_redPacketDetailTable = nullptr;

    QTableWidget *m_additionalTable = nullptr;
    QTableWidget *m_integralTable = nullptr;
    QTableWidget *m_equipTable = nullptr;
    QTableWidget *m_incentiveTable = nullptr;
    QTableWidget *m_channelTable = nullptr;
    QWidget *m_additionalGiftTabPage = nullptr;
    QWidget *m_integralGiftTabPage = nullptr;
    QWidget *m_equipGiftTabPage = nullptr;
    QWidget *m_incentiveGiftTabPage = nullptr;
    QWidget *m_channelGiftTabPage = nullptr;
    QWidget *m_equipTabBody = nullptr;
    QWidget *m_incentiveTabBody = nullptr;
    QWidget *m_channelTabBody = nullptr;
    QWidget *m_redPacketTabBody = nullptr;

    QPushButton *m_confirmBtn = nullptr;
    QPushButton *m_cancelBtn = nullptr;
};

#endif // TEMPLATEDIALOG_H
