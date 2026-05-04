#include "partitiondialog.h"

#include "appconfig.h"
#include "machinecode.h"
#include "pageutils.h"

#include <QApplication>
#include <QCursor>
#include <QFileDialog>
#include <QGraphicsDropShadowEffect>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QJsonDocument>
#include <QJsonArray>

// ---------------------------------------------------------------------------
// Helper: 从 JsonObject 中读取字符串字段（多名称兼容）
// ---------------------------------------------------------------------------
namespace {
QString ReadField(const QJsonObject &obj, const QStringList &names)
{
    for (const auto &name : names) {
        const QString val = obj.value(name).toVariant().toString().trimmed();
        if (!val.isEmpty())
            return val;
    }
    return {};
}

int ReadIntField(const QJsonObject &obj, const QStringList &names, int defaultVal = 0)
{
    for (const auto &name : names) {
        if (obj.contains(name)) {
            bool ok = false;
            int v = obj.value(name).toVariant().toInt(&ok);
            if (ok) return v;
        }
    }
    return defaultVal;
}
} // namespace

// ---------------------------------------------------------------------------
// 通用的暗红主题卡片按钮样式
// ---------------------------------------------------------------------------
static const char *kPrimaryButtonStyle = R"(
QPushButton {
    color: #ffffff;
    background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #a15864, stop:1 #8b4a53);
    border: none;
    border-radius: 8px;
    font-size: 13px;
    font-weight: 600;
    padding: 6px 24px;
}
QPushButton:hover { background: #9a5660; }
QPushButton:pressed { background: #7a3f49; }
)";

static const char *kSecondaryButtonStyle = R"(
QPushButton {
    color: #8b4a53;
    background: #f5edef;
    border: 1px solid #dccbce;
    border-radius: 8px;
    font-size: 13px;
    font-weight: 600;
    padding: 6px 24px;
}
QPushButton:hover { background: #efe4e7; border-color: #c9b3b7; }
QPushButton:pressed { background: #e5d5d9; }
)";

static const char *kLineEditStyle = R"(
QLineEdit {
    padding: 8px 12px;
    border: 1px solid #ddd1d4;
    border-radius: 8px;
    font-size: 14px;
    color: #4a3d3f;
    background: #fcfafa;
}
QLineEdit:focus {
    border-color: #a15864;
    background: #ffffff;
}
)";

static const char *kComboBoxStyle = R"(
QComboBox {
    padding: 8px 12px;
    border: 1px solid #ddd1d4;
    border-radius: 8px;
    font-size: 14px;
    color: #4a3d3f;
    background: #fcfafa;
}
QComboBox:focus { border-color: #a15864; }
QComboBox::drop-down {
    subcontrol-origin: padding;
    subcontrol-position: top right;
    width: 28px;
    border-left: 1px solid #ddd1d4;
    border-top-right-radius: 8px;
    border-bottom-right-radius: 8px;
}
QComboBox QAbstractItemView {
    border: 1px solid #dccbce;
    border-radius: 6px;
    background: #fff;
    selection-background-color: #f0e4e6;
    selection-color: #4a3d3f;
}
)";

static const char *kLabelStyle = R"(
QLabel {
    color: #5f5053;
    font-size: 13px;
    font-weight: 500;
}
)";

// ---------------------------------------------------------------------------
// 构造函数
// ---------------------------------------------------------------------------
PartitionDialog::PartitionDialog(const QJsonObject &partitionObject,
                                 const QJsonArray &templates,
                                 const QJsonArray &groups,
                                 QWidget *parent)
    : QDialog(parent)
    , m_partitionObject(partitionObject)
    , m_templates(templates)
    , m_groups(groups)
{
    m_partitionId = ReadField(partitionObject, {"Id", "id"});

    setWindowTitle(IsEditMode() ? QStringLiteral("编辑分区") : QStringLiteral("添加分区"));
    setFixedSize(600, 620);
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint | Qt::CustomizeWindowHint);
    setAttribute(Qt::WA_TranslucentBackground, true);

    SetupUi();

    if (IsEditMode()) {
        PopulateFields();
    } else {
        // 新建模式默认选中第一项
        m_radioNoUpdate->setChecked(true);
        m_dateTimeEdit->setDateTime(QDateTime::currentDateTime());
    }
}

PartitionDialog::~PartitionDialog() = default;

// ---------------------------------------------------------------------------
// SetupUi：构建美观的卡片式对话框
// ---------------------------------------------------------------------------
void PartitionDialog::SetupUi()
{
    // 根布局（无边框对话框要留边距让阴影可见）
    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(12, 12, 12, 12);

    // 卡片容器
    auto *card = new QWidget(this);
    card->setObjectName(QStringLiteral("dialogCard"));
    card->setStyleSheet(QStringLiteral(
        "#dialogCard { background: #ffffff; border-radius: 16px; border: 1px solid #e5d2d5; }"));
    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(28, 24, 28, 20);
    cardLayout->setSpacing(0);

    // 阴影
    auto *shadow = new QGraphicsDropShadowEffect(card);
    shadow->setBlurRadius(32);
    shadow->setOffset(0, 8);
    shadow->setColor(QColor(139, 74, 83, 50));
    card->setGraphicsEffect(shadow);

    // ----- 标题栏 -----
    auto *titleBar = new QWidget(card);
    auto *titleLayout = new QHBoxLayout(titleBar);
    titleLayout->setContentsMargins(0, 0, 0, 0);
    auto *titleLabel = new QLabel(IsEditMode() ? QStringLiteral("编辑分区") : QStringLiteral("添加分区"), titleBar);
    titleLabel->setStyleSheet(QStringLiteral("color: #4a3d3f; font-size: 18px; font-weight: 700; background: transparent;"));
    auto *closeButton = new QPushButton(QStringLiteral("✕"), titleBar);
    closeButton->setFixedSize(28, 28);
    closeButton->setCursor(Qt::PointingHandCursor);
    closeButton->setStyleSheet(QStringLiteral(
        "QPushButton { color: #a08388; background: transparent; border: none; border-radius: 14px; font-size: 14px; }"
        "QPushButton:hover { color: #fff; background: #cf5b5b; }"));
    connect(closeButton, &QPushButton::clicked, this, &QDialog::reject);
    titleLayout->addWidget(titleLabel);
    titleLayout->addStretch();
    titleLayout->addWidget(closeButton);
    cardLayout->addWidget(titleBar);
    cardLayout->addSpacing(16);

    // ----- 可滚动的表单区域 -----
    auto *scrollArea = new QScrollArea(card);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; border: none; }"
                                              "QScrollBar:vertical { width: 6px; }"
                                              "QScrollBar::handle:vertical { background: #dccbce; border-radius: 3px; }"));

    auto *formWidget = new QWidget(scrollArea);
    formWidget->setStyleSheet(QStringLiteral("background: transparent;"));
    auto *formLayout = new QVBoxLayout(formWidget);
    formLayout->setContentsMargins(0, 0, 10, 0);
    formLayout->setSpacing(16);

    // ---------- 1. 分区名称 ----------
    {
        auto *label = new QLabel(QStringLiteral("分区名称"), formWidget);
        label->setStyleSheet(kLabelStyle);
        m_nameEdit = new QLineEdit(formWidget);
        m_nameEdit->setPlaceholderText(QStringLiteral("请输入分区名称"));
        m_nameEdit->setStyleSheet(kLineEditStyle);
        formLayout->addWidget(label);
        formLayout->addWidget(m_nameEdit);
    }

    // ---------- 2. 分组选择 ----------
    {
        auto *label = new QLabel(QStringLiteral("所属分组（可多选）"), formWidget);
        label->setStyleSheet(kLabelStyle);
        m_groupTable = new QTableWidget(formWidget);
        m_groupTable->setColumnCount(2);
        m_groupTable->setHorizontalHeaderLabels({QStringLiteral("编号"), QStringLiteral("分组名称")});
        m_groupTable->setRowCount(0);

        // 填充分组数据
        m_groupTable->setRowCount(m_groups.size());
        for (int i = 0; i < m_groups.size(); ++i) {
            const QJsonObject g = m_groups.at(i).toObject();
            auto *idItem = new QTableWidgetItem(ReadField(g, {"Id", "id"}));
            idItem->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            idItem->setCheckState(Qt::Unchecked);
            m_groupTable->setItem(i, 0, idItem);
            m_groupTable->setItem(i, 1, new QTableWidgetItem(ReadField(g, {"Name", "name", "GroupName", "groupName"})));
        }

        m_groupTable->setSelectionMode(QAbstractItemView::NoSelection);
        m_groupTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_groupTable->verticalHeader()->setVisible(false);
        m_groupTable->horizontalHeader()->setDefaultAlignment(Qt::AlignCenter);
        m_groupTable->setShowGrid(true);
        m_groupTable->setFocusPolicy(Qt::NoFocus);
        m_groupTable->setAlternatingRowColors(true);
        m_groupTable->setStyleSheet(QStringLiteral(
            "QTableWidget { background: #fcfafa; border: 1px solid #ddd1d4; border-radius: 8px; font-size: 13px; color: #4a3d3f; }"
            "QTableWidget::item { padding: 4px 8px; }"
            "QHeaderView::section { background: #f5edef; color: #5f5053; font-weight: 600; border: none; padding: 6px; }"
            "QTableWidget::item:alternate { background: #f8f3f4; }"));
        m_groupTable->setColumnWidth(0, 80);
        m_groupTable->horizontalHeader()->setStretchLastSection(true);
        m_groupTable->setFixedHeight(140);

        formLayout->addWidget(label);
        formLayout->addWidget(m_groupTable);
    }

    // ---------- 3. 安装路径 ----------
    {
        auto *label = new QLabel(QStringLiteral("安装路径"), formWidget);
        label->setStyleSheet(kLabelStyle);
        auto *pathLayout = new QHBoxLayout();
        pathLayout->setSpacing(8);
        m_pathEdit = new QLineEdit(formWidget);
        m_pathEdit->setPlaceholderText(QStringLiteral("请选择安装路径或手动输入"));
        m_pathEdit->setStyleSheet(kLineEditStyle);
        m_browseButton = new QPushButton(QStringLiteral("浏览..."), formWidget);
        m_browseButton->setCursor(Qt::PointingHandCursor);
        m_browseButton->setFixedSize(80, 36);
        m_browseButton->setStyleSheet(kPrimaryButtonStyle);
        connect(m_browseButton, &QPushButton::clicked, this, &PartitionDialog::OnBrowsePath);
        pathLayout->addWidget(m_pathEdit, 1);
        pathLayout->addWidget(m_browseButton);
        formLayout->addWidget(label);
        formLayout->addLayout(pathLayout);
    }

    // ---------- 4. 模板选择 ----------
    {
        auto *label = new QLabel(QStringLiteral("使用模板"), formWidget);
        label->setStyleSheet(kLabelStyle);
        m_templateCombo = new QComboBox(formWidget);
        m_templateCombo->setStyleSheet(kComboBoxStyle);
        // 填充模板
        for (int i = 0; i < m_templates.size(); ++i) {
            const QJsonObject t = m_templates.at(i).toObject();
            const QString display = QStringLiteral("[%1] %2")
                                        .arg(ReadField(t, {"Id", "id"}),
                                             ReadField(t, {"Name", "name"}));
            m_templateCombo->addItem(display);
            m_templateCombo->setItemData(i, ReadField(t, {"Id", "id"}), Qt::UserRole);
        }
        formLayout->addWidget(label);
        formLayout->addWidget(m_templateCombo);
    }

    // ---------- 5. 开区时间 ----------
    {
        auto *label = new QLabel(QStringLiteral("开区时间"), formWidget);
        label->setStyleSheet(kLabelStyle);
        m_dateTimeEdit = new QDateTimeEdit(QDateTime::currentDateTime(), formWidget);
        m_dateTimeEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        m_dateTimeEdit->setCalendarPopup(true);
        m_dateTimeEdit->setStyleSheet(QStringLiteral(
            "QDateTimeEdit { padding: 8px 12px; border: 1px solid #ddd1d4; border-radius: 8px; font-size: 14px; color: #4a3d3f; background: #fcfafa; }"
            "QDateTimeEdit:focus { border-color: #a15864; }"
            "QDateTimeEdit::drop-down { subcontrol-origin: padding; subcontrol-position: top right; width: 28px; border-left: 1px solid #ddd1d4; }"));
        formLayout->addWidget(label);
        formLayout->addWidget(m_dateTimeEdit);
    }

    // ---------- 6. 脚本命令类型 ----------
    {
        auto *label = new QLabel(QStringLiteral("脚本命令"), formWidget);
        label->setStyleSheet(kLabelStyle);
        auto *radioWidget = new QWidget(formWidget);
        radioWidget->setStyleSheet(QStringLiteral("background: transparent;"));
        auto *radioLayout = new QHBoxLayout(radioWidget);
        radioLayout->setContentsMargins(0, 0, 0, 0);
        radioLayout->setSpacing(16);

        m_radioNoUpdate = new QRadioButton(QStringLiteral("不更新脚本"), radioWidget);
        m_radioOnlyRecharge = new QRadioButton(QStringLiteral("仅更新充值"), radioWidget);
        m_radioAllUpdate = new QRadioButton(QStringLiteral("全部更新"), radioWidget);

        const QString radioStyle = QStringLiteral(
            "QRadioButton { color: #5f5053; font-size: 13px; spacing: 6px; }"
            "QRadioButton::indicator { width: 16px; height: 16px; border-radius: 8px; border: 2px solid #dccbce; }"
            "QRadioButton::indicator:checked { border-color: #a15864; background: qradialgradient(cx:0.5,cy:0.5,r:0.5, fx:0.5,fy:0.5, stop:0 #a15864, stop:0.6 #a15864, stop:0.7 #fff); }");
        m_radioNoUpdate->setStyleSheet(radioStyle);
        m_radioOnlyRecharge->setStyleSheet(radioStyle);
        m_radioAllUpdate->setStyleSheet(radioStyle);

        radioLayout->addWidget(m_radioNoUpdate);
        radioLayout->addWidget(m_radioOnlyRecharge);
        radioLayout->addWidget(m_radioAllUpdate);
        radioLayout->addStretch();

        m_cmdTypeGroup = new QButtonGroup(this);
        m_cmdTypeGroup->addButton(m_radioNoUpdate, 0);
        m_cmdTypeGroup->addButton(m_radioOnlyRecharge, 1);
        m_cmdTypeGroup->addButton(m_radioAllUpdate, 2);

        formLayout->addWidget(label);
        formLayout->addWidget(radioWidget);
    }

    // ---------- 7. 元宝蛋 ----------
    {
        m_ybEggCheck = new QCheckBox(QStringLiteral("开启元宝蛋"), formWidget);
        m_ybEggCheck->setStyleSheet(QStringLiteral(
            "QCheckBox { color: #5f5053; font-size: 13px; spacing: 8px; }"
            "QCheckBox::indicator { width: 18px; height: 18px; border-radius: 4px; border: 2px solid #dccbce; background: #fcfafa; }"
            "QCheckBox::indicator:checked { background: #a15864; border-color: #a15864; }"));
        formLayout->addWidget(m_ybEggCheck);
    }

    formLayout->addStretch();
    scrollArea->setWidget(formWidget);
    cardLayout->addWidget(scrollArea, 1);
    cardLayout->addSpacing(16);

    // ----- 按钮栏 -----
    auto *buttonBar = new QWidget(card);
    auto *buttonLayout = new QHBoxLayout(buttonBar);
    buttonLayout->setContentsMargins(0, 0, 0, 0);
    buttonLayout->setSpacing(12);

    m_cancelButton = new QPushButton(QStringLiteral("取消"), buttonBar);
    m_cancelButton->setCursor(Qt::PointingHandCursor);
    m_cancelButton->setFixedHeight(38);
    m_cancelButton->setStyleSheet(kSecondaryButtonStyle);
    connect(m_cancelButton, &QPushButton::clicked, this, &QDialog::reject);

    m_confirmButton = new QPushButton(IsEditMode() ? QStringLiteral("保存") : QStringLiteral("确定添加"), buttonBar);
    m_confirmButton->setCursor(Qt::PointingHandCursor);
    m_confirmButton->setFixedHeight(38);
    m_confirmButton->setStyleSheet(kPrimaryButtonStyle);
    connect(m_confirmButton, &QPushButton::clicked, this, &PartitionDialog::OnConfirm);

    buttonLayout->addStretch();
    buttonLayout->addWidget(m_cancelButton);
    buttonLayout->addWidget(m_confirmButton);

    cardLayout->addWidget(buttonBar);

    rootLayout->addWidget(card);
}

// ---------------------------------------------------------------------------
// PopulateFields：编辑模式时填充已有数据
// ---------------------------------------------------------------------------
void PartitionDialog::PopulateFields()
{
    // 分区名称
    m_nameEdit->setText(ReadField(m_partitionObject, {"Name", "name", "PartitionName"}));

    // 安装路径
    m_pathEdit->setText(ReadField(m_partitionObject, {"ScriptPath", "scriptPath", "PartitionPath", "partitionPath"}));

    // 模板
    const QString templateId = ReadField(m_partitionObject, {"TemplateId", "templateId"});
    for (int i = 0; i < m_templateCombo->count(); ++i) {
        if (m_templateCombo->itemData(i, Qt::UserRole).toString() == templateId) {
            m_templateCombo->setCurrentIndex(i);
            break;
        }
    }

    // 开区时间
    const QString useDate = ReadField(m_partitionObject, {"UseDate", "useDate"});
    if (!useDate.isEmpty()) {
        QDateTime dt = QDateTime::fromString(useDate, Qt::ISODate);
        if (dt.isValid())
            m_dateTimeEdit->setDateTime(dt);
    }

    // 脚本命令类型
    int cmdType = ReadIntField(m_partitionObject, {"PartitionCmdType", "partitionCmdType"}, 2);
    auto *btn = m_cmdTypeGroup->button(cmdType);
    if (btn) btn->setChecked(true);
    else m_radioAllUpdate->setChecked(true);

    // 元宝蛋
    m_ybEggCheck->setChecked(m_partitionObject.value(QStringLiteral("YbEgg")).toBool()
                             || m_partitionObject.value(QStringLiteral("ybEgg")).toBool());

    // 分组勾选 - 编辑时，从 partitionObject.Groups 或直接取 Groups 数组
    QJsonArray selectedGroups;
    const QJsonValue groupsVal = m_partitionObject.value(QStringLiteral("Groups"));
    if (groupsVal.isArray()) {
        selectedGroups = groupsVal.toArray();
    } else {
        // 兼容老字段
        const QJsonValue groupIds = m_partitionObject.value(QStringLiteral("GroupIds"));
        if (groupIds.isArray()) {
            selectedGroups = groupIds.toArray();
        }
    }

    for (int i = 0; i < m_groupTable->rowCount(); ++i) {
        const QString rowId = m_groupTable->item(i, 0)->text();
        for (int j = 0; j < selectedGroups.size(); ++j) {
            const QJsonObject sg = selectedGroups.at(j).toObject();
            const QString sgId = ReadField(sg, {"GroupId", "groupId", "Id", "id"});
            if (sgId == rowId) {
                m_groupTable->item(i, 0)->setCheckState(Qt::Checked);
                break;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// OnBrowsePath：浏览安装目录
// ---------------------------------------------------------------------------
void PartitionDialog::OnBrowsePath()
{
    QString startPath = m_pathEdit->text().trimmed();
    if (startPath.isEmpty()) {
        const AppConfigValues values = AppConfig::Load();
        if (!values.installStartPath.isEmpty()) {
            startPath = values.installStartPath;
        }
    }

    const QString dir = QFileDialog::getExistingDirectory(this,
                                                          QStringLiteral("选择安装路径"),
                                                          startPath,
                                                          QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (!dir.isEmpty()) {
        m_pathEdit->setText(dir);
    }
}

// ---------------------------------------------------------------------------
// OnConfirm：校验并发出请求 body
// ---------------------------------------------------------------------------
void PartitionDialog::OnConfirm()
{
    // 校验
    if (m_nameEdit->text().trimmed().isEmpty()) {
        UiHelpers::ShowStyledMessageBox(this, QMessageBox::Warning,
                                        QStringLiteral("提示"), QStringLiteral("分区名称不能为空"));
        m_nameEdit->setFocus();
        return;
    }
    if (m_pathEdit->text().trimmed().isEmpty()) {
        UiHelpers::ShowStyledMessageBox(this, QMessageBox::Warning,
                                        QStringLiteral("提示"), QStringLiteral("安装路径不能为空"));
        m_pathEdit->setFocus();
        return;
    }

    bool hasGroup = false;
    for (int i = 0; i < m_groupTable->rowCount(); ++i) {
        if (m_groupTable->item(i, 0)->checkState() == Qt::Checked) {
            hasGroup = true;
            break;
        }
    }
    if (!hasGroup) {
        UiHelpers::ShowStyledMessageBox(this, QMessageBox::Warning,
                                        QStringLiteral("提示"), QStringLiteral("请至少选择一个分组"));
        return;
    }

    accept();
}

// ---------------------------------------------------------------------------
// GetRequestBody：构造发送给后端的 JSON body
// ---------------------------------------------------------------------------
QJsonObject PartitionDialog::GetRequestBody() const
{
    QJsonObject body;

    // 分区 ID（编辑模式）
    if (IsEditMode()) {
        bool ok = false;
        int id = m_partitionId.toInt(&ok);
        if (ok)
            body.insert(QStringLiteral("Id"), id);
    }

    // 分区名称
    body.insert(QStringLiteral("Name"), m_nameEdit->text().trimmed());

    // 安装路径
    body.insert(QStringLiteral("ScriptPath"), m_pathEdit->text().trimmed());

    // 模板 ID
    const int templateIndex = m_templateCombo->currentIndex();
    if (templateIndex >= 0) {
        bool ok = false;
        int tId = m_templateCombo->itemData(templateIndex, Qt::UserRole).toInt(&ok);
        if (ok)
            body.insert(QStringLiteral("TemplateId"), tId);
    }

    // 开区时间
    body.insert(QStringLiteral("UseDate"), m_dateTimeEdit->dateTime().toString(Qt::ISODate));

    // 脚本命令类型
    int cmdType = m_cmdTypeGroup->checkedId();
    if (cmdType < 0) cmdType = 2; // 默认全部更新
    body.insert(QStringLiteral("PartitionCmdType"), cmdType);

    // 元宝蛋
    body.insert(QStringLiteral("YbEgg"), m_ybEggCheck->isChecked());

    // 分组列表
    QJsonArray groupsArray;
    for (int i = 0; i < m_groupTable->rowCount(); ++i) {
        if (m_groupTable->item(i, 0)->checkState() == Qt::Checked) {
            QJsonObject g;
            bool ok = false;
            int gId = m_groupTable->item(i, 0)->text().toInt(&ok);
            if (ok) {
                g.insert(QStringLiteral("Id"), gId);
                g.insert(QStringLiteral("GroupId"), gId);
            }
            groupsArray.append(g);
        }
    }
    body.insert(QStringLiteral("Groups"), groupsArray);

    // 机器码（新安装时需要）
    if (!IsEditMode()) {
        MachineCode machineCode;
        body.insert(QStringLiteral("MachineCode"), machineCode.GetRNum().trimmed());
        // Type = 1 (Rxcq - 热血传奇，与老网关保持一致)
        body.insert(QStringLiteral("Type"), 1);
        body.insert(QStringLiteral("IsCreate"), true);
        body.insert(QStringLiteral("IsChangeInTime"), false);
    }

    return body;
}
