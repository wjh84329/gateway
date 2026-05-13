#include "partitiondialog.h"

#include "appconfig.h"
#include "machinecode.h"
#include "pageutils.h"
#include "checkboxstyle.h"
#include "radiostyle.h"

#include <QFrame>
#include <QEvent>
#include <QGraphicsDropShadowEffect>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMouseEvent>
#include <QPainterPath>
#include <QMessageBox>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QJsonDocument>
#include <QApplication>
#include <QAbstractItemView>
#include <QSet>
#include <QCalendarWidget>
#include <QWheelEvent>

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

QString PartitionRadioStyleSheet()
{
    return QStringLiteral(
               "QRadioButton { color: #3f3135; font-size: 13px; font-weight: 600; spacing: 8px; }"
               "QRadioButton:checked { color: #8b4a53; }\n")
        + GatewayRadioStyle::indicatorRules(18);
}

void InsertNormalizedGroupId(QSet<QString> *set, const QString &rawId)
{
    if (!set || rawId.trimmed().isEmpty()) {
        return;
    }
    const QString t = rawId.trimmed();
    set->insert(t);
    bool ok = false;
    const qint64 n = t.toLongLong(&ok);
    if (ok) {
        set->insert(QString::number(n));
    }
}

bool GroupIdSetContainsRowId(const QSet<QString> &set, const QString &rowId)
{
    const QString r = rowId.trimmed();
    if (set.contains(r)) {
        return true;
    }
    bool ok = false;
    const qint64 n = r.toLongLong(&ok);
    return ok && set.contains(QString::number(n));
}

/// TenantServer GetPartitions 序列化多为 camelCase（groups）；实体为 PartitionGroups
QJsonValue ExtractGroupsArrayValue(const QJsonObject &partitionObject)
{
    static const QStringList kKeys = {
        QStringLiteral("Groups"),
        QStringLiteral("groups"),
        QStringLiteral("PartitionGroups"),
        QStringLiteral("partitionGroups"),
    };
    for (const auto &key : kKeys) {
        const QJsonValue v = partitionObject.value(key);
        if (v.isArray()) {
            return v;
        }
    }
    return {};
}

/// 与分区模板对话框一致：未展开下拉时不处理滚轮（交给外层 QScrollArea），展开后列表可用滚轮改选项
class PartitionDialogComboBox final : public QComboBox
{
public:
    explicit PartitionDialogComboBox(QWidget *parent = nullptr)
        : QComboBox(parent)
    {
        setMaxVisibleItems(20);
        setFocusPolicy(Qt::StrongFocus);
    }

protected:
    void wheelEvent(QWheelEvent *e) override
    {
        if (!view()->isVisible()) {
            e->ignore();
            return;
        }
        QComboBox::wheelEvent(e);
    }

    void showPopup() override
    {
        QComboBox::showPopup();
        if (QAbstractItemView *v = view()) {
            if (QWidget *w = v->window()) {
                w->raise();
            }
        }
    }
};

}

// 暗红主题样式 - 与主窗口一致
static const char *kButtonPrimaryStyle = R"(
QPushButton {
    color: #ffffff;
    background: #8b4a53;
    border: none;
    border-radius: 4px;
    font-size: 13px;
    font-weight: normal;
    padding: 6px 24px;
}
QPushButton:hover { background: #9a5660; }
QPushButton:pressed { background: #7a3f49; }
)";

static const char *kButtonSecondaryStyle = R"(
QPushButton {
    color: #8b4a53;
    background: #ffffff;
    border: 1px solid #dccbce;
    border-radius: 4px;
    font-size: 13px;
    font-weight: normal;
    padding: 6px 24px;
}
QPushButton:hover { background: #f5edef; }
QPushButton:pressed { background: #efe4e7; }
)";

static const char *kInputFieldStyle = R"(
QLineEdit, QComboBox, QDateTimeEdit {
    min-height: 38px;
    border: 1px solid #d9c8cb;
    border-radius: 8px;
    padding: 0 12px;
    font-size: 13px;
    color: #3f3135;
    background: #fffdfd;
    selection-background-color: #c98b95;
}
QLineEdit:focus, QComboBox:focus, QDateTimeEdit:focus {
    border: 1px solid #a35a66;
    background: #ffffff;
}
QLineEdit::placeholder {
    color: #af9ca0;
}
QComboBox::drop-down, QDateTimeEdit::drop-down {
    subcontrol-origin: padding;
    subcontrol-position: top right;
    width: 28px;
    border-left: 1px solid #e4d8da;
    background: #f8f1f2;
    border-top-right-radius: 8px;
    border-bottom-right-radius: 8px;
}
QComboBox QAbstractItemView {
    border: 1px solid #dbcacc;
    padding: 4px;
    outline: none;
    background: #ffffff;
    selection-background-color: #f4e4e7;
    selection-color: #513a40;
}
)";

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
    setFixedSize(680, 820);
    setModal(true);
    setWindowFlags(Qt::Window | Qt::Dialog | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_DeleteOnClose, false);

    SetupUi();
    connect(m_templateCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PartitionDialog::OnTemplateIndexChanged);

    if (IsEditMode()) {
        PopulateFields();
    } else {
        m_partitionType = TemplateTypeForComboIndex(0);
        m_radioNoUpdate->setChecked(true);
        const QDateTime now = QDateTime::currentDateTime();
        m_dateTimeEdit->setDateTime(now);
        m_deleteDateTimeEdit->setDateTime(now);
        m_changeDateTimeEdit->setDateTime(now);
        m_scanWebRadio->setChecked(true);
        m_ybEggOffRadio->setChecked(true);
        m_renameFieldsWrap->setVisible(false);
    }
    OnTemplateIndexChanged(m_templateCombo->currentIndex());
}

PartitionDialog::~PartitionDialog() = default;

void PartitionDialog::SetupUi()
{
    // 根布局 - 无间距无边框
    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    // 主框架
    auto *mainWidget = new QWidget(this);
    mainWidget->setObjectName(QStringLiteral("partitionDialogMainFrame"));
    mainWidget->setStyleSheet(QStringLiteral(
        "QWidget#partitionDialogMainFrame {"
        "background: #ffffff;"
        "border-radius: 12px;"
        "}"));
    auto *shadowEffect = new QGraphicsDropShadowEffect(mainWidget);
    shadowEffect->setBlurRadius(28);
    shadowEffect->setOffset(0, 8);
    shadowEffect->setColor(QColor(88, 44, 52, 60));
    mainWidget->setGraphicsEffect(shadowEffect);

    auto *mainLayout = new QVBoxLayout(mainWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // === 标题栏（简约深色条，支持拖动）===
    m_titleWidget = new QWidget(mainWidget);
    m_titleWidget->setFixedHeight(48);
    m_titleWidget->setObjectName(QStringLiteral("partitionDialogTitleBar"));
    m_titleWidget->setStyleSheet(QStringLiteral(
        "QWidget#partitionDialogTitleBar {"
        "background: qlineargradient(x1:0,y1:0,x2:1,y2:0, stop:0 #8b4a53, stop:1 #9a5b66);"
        "border-top-left-radius: 12px;"
        "border-top-right-radius: 12px;"
        "}"));
    m_titleWidget->installEventFilter(this);

    auto *titleLayout = new QHBoxLayout(m_titleWidget);
    titleLayout->setContentsMargins(16, 0, 8, 0);

    auto *titleLabel = new QLabel(IsEditMode() ? QStringLiteral("编辑分区") : QStringLiteral("添加分区"), m_titleWidget);
    titleLabel->setStyleSheet(QStringLiteral(
        "color: #ffffff; font-size: 15px; font-weight: normal; background: transparent;"));
    titleLayout->addWidget(titleLabel);
    titleLayout->addStretch();

    auto *closeButton = new QPushButton(QStringLiteral("✕"), m_titleWidget);
    closeButton->setFixedSize(32, 32);
    closeButton->setCursor(Qt::PointingHandCursor);
    closeButton->setStyleSheet(QStringLiteral(
        "QPushButton { color: #ffffff; background: transparent; border: none; border-radius: 16px; font-size: 14px; font-weight: normal; }"
        "QPushButton:hover { background: rgba(255,255,255,0.2); }"));
    connect(closeButton, &QPushButton::clicked, this, &QDialog::reject);
    titleLayout->addWidget(closeButton);

    mainLayout->addWidget(m_titleWidget);

    // === 表单滚动区域 ===
    auto *scrollArea = new QScrollArea(mainWidget);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setStyleSheet(QStringLiteral(
        "QScrollArea { background: #ffffff; border: none; }"
        "QScrollBar:vertical { width: 6px; background: #f5f5f5; }"
        "QScrollBar::handle:vertical { background: #d0c0c0; border-radius: 3px; min-height: 20px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }"));

    auto *formWidget = new QWidget(scrollArea);
    formWidget->setStyleSheet(QStringLiteral("background: #ffffff; border: none;"));
    scrollArea->viewport()->setStyleSheet(QStringLiteral("background: #ffffff; border: none;"));
    auto *formLayout = new QVBoxLayout(formWidget);
    formLayout->setContentsMargins(32, 26, 32, 22);
    formLayout->setSpacing(22);

    // 通用小标签样式 - 更现代简洁
    const QString sectionCardStyle = QStringLiteral(
        "background: #fcf7f9;"
        "border: none;"
        "border-radius: 12px;");
    const QString sectionTitleStyle = QStringLiteral(
        "color: #5a3a41;"
        "font-size: 14px;"
        "font-weight: normal;"
        "background: transparent;");
    const QString sectionHintStyle = QStringLiteral(
        "color: #ad8e95;"
        "font-size: 11px;"
        "background: transparent;");
    const QString labelStyle = QStringLiteral(
        "color: #6a4b52;"
        "font-size: 12px;"
        "font-weight: normal;"
        "background: transparent;"
    );

    auto createSection = [&](const QString &title, const QString &hint) {
        auto *card = new QFrame(formWidget);
        card->setFrameShape(QFrame::NoFrame);
        card->setStyleSheet(sectionCardStyle);
        auto *layout = new QVBoxLayout(card);
        layout->setContentsMargins(20, 16, 20, 18);
        layout->setSpacing(12);

        auto *titleRow = new QHBoxLayout();
        titleRow->setContentsMargins(0, 0, 0, 0);
        titleRow->setSpacing(8);

        auto *dot = new QLabel(card);
        dot->setFixedSize(8, 8);
        dot->setStyleSheet(QStringLiteral("background: #9a5b66; border-radius: 4px;"));
        titleRow->addWidget(dot, 0, Qt::AlignVCenter);

        auto *sectionTitle = new QLabel(title, card);
        sectionTitle->setStyleSheet(sectionTitleStyle);
        titleRow->addWidget(sectionTitle, 0, Qt::AlignVCenter);

        if (!hint.isEmpty()) {
            auto *hintLabel = new QLabel(hint, card);
            hintLabel->setStyleSheet(sectionHintStyle);
            titleRow->addWidget(hintLabel, 0, Qt::AlignVCenter);
        }
        titleRow->addStretch();
        layout->addLayout(titleRow);

        return qMakePair(card, layout);
    };

    const QString dateTimeFieldStyle = QStringLiteral(
        "QDateTimeEdit { min-height: 38px; border: 1px solid #d9c8cb; border-radius: 8px; padding: 0 12px; font-size: 13px; color: #3f3135; background: #fffdfd; }"
        "QDateTimeEdit:focus { border: 1px solid #a35a66; background: #ffffff; }"
        "QCalendarWidget { background: #ffffff; }"
        "QCalendarWidget QToolButton { color: #4a2d34; font-size: 14px; font-weight: normal; background: #fcf7f8; border: none; padding: 6px; }"
        "QCalendarWidget QToolButton:hover { background: #f4e8ea; }"
        "QCalendarWidget QMenu { background: #ffffff; }"
        "QCalendarWidget QSpinBox { color: #4a2d34; font-size: 13px; font-weight: 600; background: #ffffff; }"
        "QCalendarWidget QWidget#qt_calendar_navigationbar { background: #fcf7f8; }");

    // 1. 基础信息
    {
        auto section = createSection(QStringLiteral("基础信息"), QString());
        QWidget *card = section.first;
        QVBoxLayout *vl = section.second;

        auto *nameLbl = new QLabel(QStringLiteral("分区名称"), card);
        nameLbl->setStyleSheet(labelStyle);
        m_nameEdit = new QLineEdit(card);
        m_nameEdit->setPlaceholderText(QStringLiteral("请输入分区名称"));
        m_nameEdit->setStyleSheet(QLatin1String(kInputFieldStyle));
        vl->addWidget(nameLbl);
        vl->addWidget(m_nameEdit);
        auto *nameTip = new QLabel(QStringLiteral("3～30 个字符，请勿输入特殊符号"), card);
        nameTip->setStyleSheet(sectionHintStyle);
        vl->addWidget(nameTip);

        m_changeNameInTimeCheck = new QCheckBox(QStringLiteral("在指定时间更改分区名称"), card);
        m_changeNameInTimeCheck->setStyleSheet(
            QStringLiteral(
                "QCheckBox { color: #3f3135; font-size: 13px; font-weight: 600; spacing: 8px; }"
                "QCheckBox:checked { color: #8b4a53; }\n")
            + GatewayCheckboxStyle::indicatorRules(18));
        vl->addWidget(m_changeNameInTimeCheck);

        m_renameFieldsWrap = new QWidget(card);
        m_renameFieldsWrap->setAttribute(Qt::WA_StyledBackground, true);
        m_renameFieldsWrap->setStyleSheet(QStringLiteral("background: transparent; border: none;"));
        auto *rw = new QVBoxLayout(m_renameFieldsWrap);
        rw->setContentsMargins(8, 4, 0, 4);
        rw->setSpacing(10);
        auto *cnLbl = new QLabel(QStringLiteral("更改名称"), m_renameFieldsWrap);
        cnLbl->setStyleSheet(labelStyle);
        m_changeNameEdit = new QLineEdit(m_renameFieldsWrap);
        m_changeNameEdit->setPlaceholderText(QStringLiteral("定时切换后的分区名称"));
        m_changeNameEdit->setStyleSheet(QLatin1String(kInputFieldStyle));
        rw->addWidget(cnLbl);
        rw->addWidget(m_changeNameEdit);
        auto *ctLbl = new QLabel(QStringLiteral("更改时间"), m_renameFieldsWrap);
        ctLbl->setStyleSheet(labelStyle);
        m_changeDateTimeEdit = new QDateTimeEdit(QDateTime::currentDateTime(), m_renameFieldsWrap);
        m_changeDateTimeEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        m_changeDateTimeEdit->setCalendarPopup(true);
        m_changeDateTimeEdit->setStyleSheet(dateTimeFieldStyle);
        rw->addWidget(ctLbl);
        rw->addWidget(m_changeDateTimeEdit);
        vl->addWidget(m_renameFieldsWrap);
        connect(m_changeNameInTimeCheck, &QCheckBox::toggled, m_renameFieldsWrap, &QWidget::setVisible);

        formLayout->addWidget(card);
    }

    // 2. 分组选择
    {
        auto section = createSection(QStringLiteral("分组绑定"), QString());
        auto *lbl = new QLabel(QStringLiteral("所属分组"), section.first);
        lbl->setStyleSheet(labelStyle);
        m_groupTable = new QTableWidget(section.first);
        m_groupTable->setColumnCount(2);
        m_groupTable->setHorizontalHeaderLabels({QStringLiteral("编号"), QStringLiteral("分组名称")});

        // 填充分组
        m_groupTable->setRowCount(m_groups.size());
        for (int i = 0; i < m_groups.size(); ++i) {
            const QJsonObject g = m_groups.at(i).toObject();
            auto *idItem = new QTableWidgetItem(ReadField(g, {"Id", "id"}));
            idItem->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            idItem->setCheckState(Qt::Unchecked);
            m_groupTable->setItem(i, 0, idItem);
            m_groupTable->setItem(i, 1, new QTableWidgetItem(ReadField(g, {"Name", "name"})));
        }

        m_groupTable->setSelectionMode(QAbstractItemView::NoSelection);
        m_groupTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_groupTable->verticalHeader()->setVisible(false);
        m_groupTable->horizontalHeader()->setDefaultAlignment(Qt::AlignCenter);
        m_groupTable->setShowGrid(false);
        m_groupTable->setAlternatingRowColors(true);
        m_groupTable->setFocusPolicy(Qt::NoFocus);
        m_groupTable->horizontalHeader()->setStretchLastSection(true);
        m_groupTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
        m_groupTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
        m_groupTable->setStyleSheet(QStringLiteral(
            "QTableWidget {"
            "border: none;"
            "border-radius: 8px;"
            "gridline-color: transparent;"
            "font-size: 13px;"
            "color: #403237;"
            "background: #ffffff;"
            "alternate-background-color: #fcf7f8;"
            "}"
            "QTableWidget::item { padding: 6px 8px; }"
            "QHeaderView::section {"
            "background: #f8f1f3;"
            "color: #6b4b52;"
            "font-weight: normal;"
            "border: none;"
            "border-bottom: 1px solid #ede1e4;"
            "padding: 8px;"
            "}"));
        m_groupTable->setColumnWidth(0, 70);
        m_groupTable->setFixedHeight(160);
        section.second->addWidget(lbl);
        section.second->addWidget(m_groupTable);
        formLayout->addWidget(section.first);
    }

    // 3. 安装路径（MachineCode 由接口层自动携带，不在此填写）
    {
        auto section = createSection(QStringLiteral("安装路径"), QString());
        QWidget *card = section.first;
        m_machinePathCard = card;
        QVBoxLayout *vl = section.second;

        auto *pathLbl = new QLabel(QStringLiteral("安装路径"), card);
        pathLbl->setStyleSheet(labelStyle);
        auto *pathLayout = new QHBoxLayout();
        pathLayout->setSpacing(8);
        m_pathEdit = new QLineEdit(card);
        m_pathEdit->setPlaceholderText(QStringLiteral("分区安装脚本目录或 INI 文件存放路径"));
        m_pathEdit->setStyleSheet(QLatin1String(kInputFieldStyle));
        m_browseButton = new QPushButton(QStringLiteral("浏览"), card);
        m_browseButton->setFixedSize(76, 38);
        m_browseButton->setCursor(Qt::PointingHandCursor);
        m_browseButton->setStyleSheet(kButtonPrimaryStyle);
        connect(m_browseButton, &QPushButton::clicked, this, &PartitionDialog::OnBrowsePath);
        pathLayout->addWidget(m_pathEdit, 1);
        pathLayout->addWidget(m_browseButton);
        vl->addWidget(pathLbl);
        vl->addLayout(pathLayout);

        formLayout->addWidget(card);
    }

    // 4. 模板与运营设置
    {
        auto section = createSection(QStringLiteral("模板与运营"), QString());
        QWidget *card = section.first;
        QVBoxLayout *vl = section.second;

        auto *templateLbl = new QLabel(QStringLiteral("模板"), card);
        templateLbl->setStyleSheet(labelStyle);
        m_templateCombo = new PartitionDialogComboBox(card);
        m_templateCombo->setStyleSheet(QLatin1String(kInputFieldStyle));
        for (int i = 0; i < m_templates.size(); ++i) {
            const QJsonObject t = m_templates.at(i).toObject();
            const QString display = QStringLiteral("[%1] %2").arg(ReadField(t, {QStringLiteral("Id"), QStringLiteral("id")}),
                                                                 ReadField(t, {QStringLiteral("Name"), QStringLiteral("name")}));
            m_templateCombo->addItem(display);
            m_templateCombo->setItemData(i, ReadField(t, {QStringLiteral("Id"), QStringLiteral("id")}), Qt::UserRole);
        }
        vl->addWidget(templateLbl);
        vl->addWidget(m_templateCombo);

        m_ybEggWidget = new QWidget(card);
        auto *eggLay = new QVBoxLayout(m_ybEggWidget);
        eggLay->setContentsMargins(0, 0, 0, 0);
        eggLay->setSpacing(8);
        auto *eggLbl = new QLabel(QStringLiteral("元宝蛋"), m_ybEggWidget);
        eggLbl->setStyleSheet(labelStyle);
        auto *eggRow = new QWidget(m_ybEggWidget);
        auto *eggH = new QHBoxLayout(eggRow);
        eggH->setContentsMargins(0, 0, 0, 0);
        eggH->setSpacing(16);
        const QString radioStyle = PartitionRadioStyleSheet();
        m_ybEggOnRadio = new QRadioButton(QStringLiteral("开启"), eggRow);
        m_ybEggOffRadio = new QRadioButton(QStringLiteral("关闭"), eggRow);
        m_ybEggOnRadio->setStyleSheet(radioStyle);
        m_ybEggOffRadio->setStyleSheet(radioStyle);
        eggH->addWidget(m_ybEggOnRadio);
        eggH->addWidget(m_ybEggOffRadio);
        eggH->addStretch();
        m_ybEggGroup = new QButtonGroup(this);
        m_ybEggGroup->addButton(m_ybEggOnRadio, 1);
        m_ybEggGroup->addButton(m_ybEggOffRadio, 0);
        eggLay->addWidget(eggLbl);
        eggLay->addWidget(eggRow);
        vl->addWidget(m_ybEggWidget);

        auto *scanLbl = new QLabel(QStringLiteral("充值方式（扫码）"), card);
        scanLbl->setStyleSheet(labelStyle);
        auto *scanRow = new QWidget(card);
        auto *scanH = new QHBoxLayout(scanRow);
        scanH->setContentsMargins(0, 0, 0, 0);
        scanH->setSpacing(16);
        m_scanWebRadio = new QRadioButton(QStringLiteral("网页扫码"), scanRow);
        m_scanGameRadio = new QRadioButton(QStringLiteral("游戏内扫码"), scanRow);
        m_scanBothRadio = new QRadioButton(QStringLiteral("以上两者"), scanRow);
        for (auto *rb : {m_scanWebRadio, m_scanGameRadio, m_scanBothRadio})
            rb->setStyleSheet(radioStyle);
        scanH->addWidget(m_scanWebRadio);
        scanH->addWidget(m_scanGameRadio);
        scanH->addWidget(m_scanBothRadio);
        scanH->addStretch();
        m_scanButtonGroup = new QButtonGroup(this);
        m_scanButtonGroup->addButton(m_scanWebRadio, 2);
        m_scanButtonGroup->addButton(m_scanGameRadio, 0);
        m_scanButtonGroup->addButton(m_scanBothRadio, 1);
        vl->addWidget(scanLbl);
        vl->addWidget(scanRow);

        formLayout->addWidget(card);
    }

    // 5. 定时开区 / 删区 / 脚本策略
    {
        auto section = createSection(QStringLiteral("开区与脚本"), QString());
        QWidget *card = section.first;
        QVBoxLayout *vl = section.second;

        auto *timeLbl = new QLabel(QStringLiteral("定时开区"), card);
        timeLbl->setStyleSheet(labelStyle);
        m_dateTimeEdit = new QDateTimeEdit(QDateTime::currentDateTime(), card);
        m_dateTimeEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        m_dateTimeEdit->setCalendarPopup(true);
        m_dateTimeEdit->setStyleSheet(dateTimeFieldStyle);
        vl->addWidget(timeLbl);
        vl->addWidget(m_dateTimeEdit);
        auto *timeTip = new QLabel(QStringLiteral("到达设定时间后开放充值；开区前会按策略加载脚本。"), card);
        timeTip->setStyleSheet(sectionHintStyle);
        timeTip->setWordWrap(true);
        vl->addWidget(timeTip);

        m_scheduledDeleteCheck = new QCheckBox(QStringLiteral("系统将在您指定的时间删除分区"), card);
        m_scheduledDeleteCheck->setStyleSheet(
            QStringLiteral(
                "QCheckBox { color: #3f3135; font-size: 13px; font-weight: 600; spacing: 8px; }"
                "QCheckBox:checked { color: #8b4a53; }\n")
            + GatewayCheckboxStyle::indicatorRules(18));
        vl->addWidget(m_scheduledDeleteCheck);
        auto *delLbl = new QLabel(QStringLiteral("删区时间"), card);
        delLbl->setStyleSheet(labelStyle);
        m_deleteDateTimeEdit = new QDateTimeEdit(QDateTime::currentDateTime(), card);
        m_deleteDateTimeEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        m_deleteDateTimeEdit->setCalendarPopup(true);
        m_deleteDateTimeEdit->setStyleSheet(dateTimeFieldStyle);
        vl->addWidget(delLbl);
        vl->addWidget(m_deleteDateTimeEdit);

        m_scriptCmdWidget = new QWidget(card);
        m_scriptCmdWidget->setAttribute(Qt::WA_StyledBackground, true);
        m_scriptCmdWidget->setStyleSheet(QStringLiteral("background: transparent; border: none;"));
        auto *scvl = new QVBoxLayout(m_scriptCmdWidget);
        scvl->setContentsMargins(0, 0, 0, 0);
        scvl->setSpacing(8);
        auto *lblCmd = new QLabel(QStringLiteral("脚本更新（热血/传世）"), m_scriptCmdWidget);
        lblCmd->setStyleSheet(labelStyle);
        auto *radioWidget = new QWidget(m_scriptCmdWidget);
        radioWidget->setAttribute(Qt::WA_StyledBackground, true);
        radioWidget->setStyleSheet(QStringLiteral("background: transparent; border: none;"));
        auto *radioLayout = new QHBoxLayout(radioWidget);
        radioLayout->setContentsMargins(0, 0, 0, 0);
        radioLayout->setSpacing(16);

        const QString radioStyle = PartitionRadioStyleSheet();
        m_radioNoUpdate = new QRadioButton(QStringLiteral("不更新脚本"), radioWidget);
        m_radioOnlyRecharge = new QRadioButton(QStringLiteral("仅更新充值脚本"), radioWidget);
        m_radioAllUpdate = new QRadioButton(QStringLiteral("全部更新"), radioWidget);
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

        scvl->addWidget(lblCmd);
        scvl->addWidget(radioWidget);
        auto *cmdTip = new QLabel(QStringLiteral("可与定时开区一并生效；也可稍后在分区管理中加载脚本。"), m_scriptCmdWidget);
        cmdTip->setStyleSheet(sectionHintStyle);
        cmdTip->setWordWrap(true);
        scvl->addWidget(cmdTip);
        vl->addWidget(m_scriptCmdWidget);

        formLayout->addWidget(card);
    }

    formLayout->addStretch();
    scrollArea->setWidget(formWidget);
    mainLayout->addWidget(scrollArea, 1);

    // === 按钮栏 ===
    auto *buttonBar = new QWidget(mainWidget);
    buttonBar->setFixedHeight(60);
    buttonBar->setStyleSheet(QStringLiteral("background: #faf5f6; border-top: 1px solid #ede1e4; border-bottom-left-radius: 12px; border-bottom-right-radius: 12px;"));
    auto *buttonLayout = new QHBoxLayout(buttonBar);
    buttonLayout->setContentsMargins(18, 10, 18, 10);
    buttonLayout->setSpacing(10);

    m_cancelButton = new QPushButton(QStringLiteral("取消"), buttonBar);
    m_cancelButton->setFixedSize(88, 36);
    m_cancelButton->setCursor(Qt::PointingHandCursor);
    m_cancelButton->setStyleSheet(kButtonSecondaryStyle);
    connect(m_cancelButton, &QPushButton::clicked, this, &QDialog::reject);

    m_confirmButton = new QPushButton(IsEditMode() ? QStringLiteral("保存") : QStringLiteral("确定添加"), buttonBar);
    m_confirmButton->setFixedSize(108, 36);
    m_confirmButton->setCursor(Qt::PointingHandCursor);
    m_confirmButton->setStyleSheet(kButtonPrimaryStyle);
    connect(m_confirmButton, &QPushButton::clicked, this, &PartitionDialog::OnConfirm);

    buttonLayout->addStretch();
    buttonLayout->addWidget(m_cancelButton);
    buttonLayout->addWidget(m_confirmButton);
    mainLayout->addWidget(buttonBar);

    rootLayout->addWidget(mainWidget);
}

bool PartitionDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_titleWidget) {
        switch (event->type()) {
        case QEvent::MouseButtonPress: {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            if (mouseEvent->button() == Qt::LeftButton) {
                m_dragging = true;
                m_dragOffset = UiHelpers::GlobalPosFromMouseEvent(mouseEvent) - frameGeometry().topLeft();
                return true;
            }
            break;
        }
        case QEvent::MouseMove: {
            if (m_dragging) {
                auto *mouseEvent = static_cast<QMouseEvent *>(event);
                move(UiHelpers::GlobalPosFromMouseEvent(mouseEvent) - m_dragOffset);
                return true;
            }
            break;
        }
        case QEvent::MouseButtonRelease:
            m_dragging = false;
            break;
        default:
            break;
        }
    }

    return QDialog::eventFilter(watched, event);
}

int PartitionDialog::TemplateTypeForComboIndex(int index) const
{
    if (index < 0 || !m_templateCombo || index >= m_templateCombo->count())
        return 1;
    const QString tid = m_templateCombo->itemData(index, Qt::UserRole).toString().trimmed();
    for (const QJsonValue &v : m_templates) {
        if (!v.isObject())
            continue;
        const QJsonObject o = v.toObject();
        if (ReadField(o, {QStringLiteral("Id"), QStringLiteral("id")}) == tid)
            return ReadIntField(o, {QStringLiteral("Type"), QStringLiteral("type")}, 1);
    }
    return 1;
}

void PartitionDialog::SyncPartitionKindUi()
{
    const int t = m_partitionType;
    const bool showMir = (t == 1 || t == 2 || t == 3 || t == 6);
    if (m_machinePathCard)
        m_machinePathCard->setVisible(showMir);
    if (m_ybEggWidget)
        m_ybEggWidget->setVisible(t == 1 || t == 2);
    if (m_scriptCmdWidget)
        m_scriptCmdWidget->setVisible(t == 1 || t == 2);
    if (m_pathEdit) {
        if (t == 4 || t == 5)
            m_pathEdit->setPlaceholderText(QStringLiteral("SQL / Web 等类型可留空"));
        else
            m_pathEdit->setPlaceholderText(QStringLiteral("分区安装脚本目录或 INI 文件存放路径"));
    }
}

void PartitionDialog::OnTemplateIndexChanged(int index)
{
    Q_UNUSED(index);
    if (!IsEditMode())
        m_partitionType = TemplateTypeForComboIndex(m_templateCombo->currentIndex());
    SyncPartitionKindUi();
}

void PartitionDialog::PopulateFields()
{
    const int rawType = ReadIntField(m_partitionObject, {QStringLiteral("Type"), QStringLiteral("type")}, -1);
    m_partitionType = (rawType >= 0) ? rawType : TemplateTypeForComboIndex(m_templateCombo->currentIndex());

    m_nameEdit->setText(ReadField(m_partitionObject,
                                  {QStringLiteral("Name"), QStringLiteral("name"), QStringLiteral("PartitionName")}));

    QString rawPath = ReadField(m_partitionObject,
                                {QStringLiteral("ScriptPath"), QStringLiteral("scriptPath"), QStringLiteral("PartitionPath"),
                                 QStringLiteral("partitionPath")});
    rawPath.replace(QLatin1Char('/'), QLatin1Char('\\'));
    m_pathEdit->setText(rawPath);

    QString templateIdStr = ReadField(m_partitionObject, {QStringLiteral("TemplateId"), QStringLiteral("templateId")});
    const int templateIdNum = ReadIntField(m_partitionObject, {QStringLiteral("TemplateId"), QStringLiteral("templateId")}, -1);
    if (templateIdStr.isEmpty() && templateIdNum >= 0)
        templateIdStr = QString::number(templateIdNum);
    for (int i = 0; i < m_templateCombo->count(); ++i) {
        const QVariant data = m_templateCombo->itemData(i, Qt::UserRole);
        bool ok = false;
        const int tid = data.toInt(&ok);
        if (ok && templateIdNum >= 0 && tid == templateIdNum) {
            m_templateCombo->setCurrentIndex(i);
            break;
        }
        if (data.toString() == templateIdStr) {
            m_templateCombo->setCurrentIndex(i);
            break;
        }
    }

    const bool changeInTime = m_partitionObject.value(QStringLiteral("IsChangeInTime")).toBool()
                              || m_partitionObject.value(QStringLiteral("isChangeInTime")).toBool();
    m_changeNameInTimeCheck->setChecked(changeInTime);
    m_changeNameEdit->setText(ReadField(m_partitionObject, {QStringLiteral("UseName"), QStringLiteral("useName")}));
    const QString changeDate = ReadField(m_partitionObject, {QStringLiteral("ChangeDate"), QStringLiteral("changeDate")});
    if (!changeDate.isEmpty()) {
        QDateTime dt = QDateTime::fromString(changeDate, Qt::ISODateWithMs);
        if (!dt.isValid())
            dt = QDateTime::fromString(changeDate, Qt::ISODate);
        if (!dt.isValid())
            dt = QDateTime::fromString(changeDate, QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        if (!dt.isValid())
            dt = QDateTime::fromString(changeDate, QStringLiteral("yyyy-MM-ddTHH:mm:ss"));
        if (dt.isValid())
            m_changeDateTimeEdit->setDateTime(dt);
    }
    m_renameFieldsWrap->setVisible(changeInTime);

    const int scanVal = ReadIntField(m_partitionObject, {QStringLiteral("Scan"), QStringLiteral("scan")}, 2);
    if (auto *b = m_scanButtonGroup->button(scanVal))
        b->setChecked(true);
    else
        m_scanWebRadio->setChecked(true);

    const bool yb = m_partitionObject.value(QStringLiteral("YbEgg")).toBool()
                    || m_partitionObject.value(QStringLiteral("ybEgg")).toBool();
    if (yb)
        m_ybEggOnRadio->setChecked(true);
    else
        m_ybEggOffRadio->setChecked(true);

    const QString useDate = ReadField(m_partitionObject, {QStringLiteral("UseDate"), QStringLiteral("useDate")});
    if (!useDate.isEmpty()) {
        QDateTime dt = QDateTime::fromString(useDate, Qt::ISODateWithMs);
        if (!dt.isValid())
            dt = QDateTime::fromString(useDate, Qt::ISODate);
        if (!dt.isValid())
            dt = QDateTime::fromString(useDate, QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        if (!dt.isValid())
            dt = QDateTime::fromString(useDate, QStringLiteral("yyyy-MM-ddTHH:mm:ss"));
        if (dt.isValid())
            m_dateTimeEdit->setDateTime(dt);
    }

    const int isDel = ReadIntField(m_partitionObject, {QStringLiteral("IsDel"), QStringLiteral("isDel")}, 0);
    m_scheduledDeleteCheck->setChecked(isDel != 0);
    const QString delDate = ReadField(m_partitionObject, {QStringLiteral("DelDate"), QStringLiteral("delDate")});
    if (!delDate.isEmpty()) {
        QDateTime dt = QDateTime::fromString(delDate, Qt::ISODateWithMs);
        if (!dt.isValid())
            dt = QDateTime::fromString(delDate, QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        if (!dt.isValid())
            dt = QDateTime::fromString(delDate, QStringLiteral("yyyy-MM-ddTHH:mm:ss"));
        if (dt.isValid())
            m_deleteDateTimeEdit->setDateTime(dt);
    }

    int cmdType = ReadIntField(m_partitionObject, {QStringLiteral("PartitionCmdType"), QStringLiteral("partitionCmdType")}, 2);
    if (auto *btn = m_cmdTypeGroup->button(cmdType))
        btn->setChecked(true);
    else
        m_radioAllUpdate->setChecked(true);

    // 分组：接口为 List<PartitionGroup>，JSON 多为 groups + groupId（业务分组 Id）；勿用关联表 Id 优先匹配
    QSet<QString> selectedGroupIds;
    const QJsonValue groupsVal = ExtractGroupsArrayValue(m_partitionObject);
    if (groupsVal.isArray()) {
        const QJsonArray groupsArr = groupsVal.toArray();
        for (int j = 0; j < groupsArr.size(); ++j) {
            if (groupsArr.at(j).isObject()) {
                const QJsonObject sg = groupsArr.at(j).toObject();
                const QString businessGid = ReadField(sg, {QStringLiteral("GroupId"), QStringLiteral("groupId")});
                if (!businessGid.isEmpty()) {
                    InsertNormalizedGroupId(&selectedGroupIds, businessGid);
                } else {
                    const QString fallbackId = ReadField(sg, {QStringLiteral("Id"), QStringLiteral("id")});
                    if (!fallbackId.isEmpty()) {
                        InsertNormalizedGroupId(&selectedGroupIds, fallbackId);
                    }
                }
            } else {
                InsertNormalizedGroupId(&selectedGroupIds, groupsArr.at(j).toVariant().toString());
            }
        }
    }

    for (int i = 0; i < m_groupTable->rowCount(); ++i) {
        QTableWidgetItem *const idItem = m_groupTable->item(i, 0);
        if (!idItem) {
            continue;
        }
        const QString rowId = idItem->text();
        if (GroupIdSetContainsRowId(selectedGroupIds, rowId)) {
            idItem->setCheckState(Qt::Checked);
        }
    }
}

void PartitionDialog::OnBrowsePath()
{
    QString startPath = m_pathEdit->text().trimmed();
    if (startPath.isEmpty()) {
        const AppConfigValues values = AppConfig::Load();
        if (!values.installStartPath.isEmpty())
            startPath = values.installStartPath;
    }

    const QString dir = QFileDialog::getExistingDirectory(this,
        QStringLiteral("选择安装路径"), startPath,
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (!dir.isEmpty())
        m_pathEdit->setText(dir);
}

void PartitionDialog::OnConfirm()
{
    if (m_nameEdit->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("分区名称不能为空"));
        m_nameEdit->setFocus();
        return;
    }
    if (m_changeNameInTimeCheck->isChecked() && m_changeNameEdit->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("已勾选「在指定时间更改分区名称」时，更改名称不能为空"));
        m_changeNameEdit->setFocus();
        return;
    }

    const int t = m_partitionType;
    if (t != 4 && t != 5 && m_pathEdit->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("安装路径不能为空"));
        m_pathEdit->setFocus();
        return;
    }

    if (m_templateCombo->count() <= 0) {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("未获取到分区模板，请确认网络与接口后重试"));
        return;
    }
    {
        const QVariant v = m_templateCombo->itemData(m_templateCombo->currentIndex(), Qt::UserRole);
        bool ok = false;
        v.toInt(&ok);
        if (!ok) {
            const QString s = v.toString().trimmed();
            s.toInt(&ok);
        }
        if (!ok) {
            QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("请选择一个有效的分区模板"));
            m_templateCombo->setFocus();
            return;
        }
    }

    bool hasGroup = false;
    for (int i = 0; i < m_groupTable->rowCount(); ++i) {
        if (auto *it = m_groupTable->item(i, 0); it && it->checkState() == Qt::Checked) {
            hasGroup = true;
            break;
        }
    }
    if (!hasGroup) {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("请至少选择一个分组"));
        return;
    }

    accept();
}

QJsonObject PartitionDialog::GetRequestBody() const
{
    QJsonObject body;

    if (IsEditMode()) {
        bool ok = false;
        const int id = m_partitionId.toInt(&ok);
        if (ok)
            body.insert(QStringLiteral("Id"), id);
    }

    body.insert(QStringLiteral("Type"), m_partitionType);
    body.insert(QStringLiteral("Name"), m_nameEdit->text().trimmed());

    QString scriptPath = m_pathEdit->text().trimmed();
    if (m_partitionType == 4 || m_partitionType == 5)
        scriptPath = QString();
    body.insert(QStringLiteral("ScriptPath"), scriptPath);

    const int templateIndex = m_templateCombo->currentIndex();
    if (templateIndex >= 0) {
        const QVariant v = m_templateCombo->itemData(templateIndex, Qt::UserRole);
        bool ok = false;
        int tId = v.toInt(&ok);
        if (!ok)
            tId = v.toString().toInt(&ok);
        if (ok)
            body.insert(QStringLiteral("TemplateId"), tId);
    }

    const QString useDateStr = m_dateTimeEdit->dateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    body.insert(QStringLiteral("UseDate"), useDateStr);

    body.insert(QStringLiteral("IsChangeInTime"), m_changeNameInTimeCheck->isChecked());
    if (m_changeNameInTimeCheck->isChecked()) {
        body.insert(QStringLiteral("UseName"), m_changeNameEdit->text().trimmed());
        body.insert(QStringLiteral("ChangeDate"),
                    m_changeDateTimeEdit->dateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
    }

    int scanId = m_scanButtonGroup->checkedId();
    if (scanId < 0)
        scanId = 2;
    body.insert(QStringLiteral("Scan"), scanId);

    // 设备实例标识由网关对外 IP（可空则用本机首选 IPv4）、HTTP 端口与商户 Uuid 派生，无需用户填写
    {
        MachineCode machineCode;
        body.insert(QStringLiteral("MachineCode"), machineCode.GetRNum().trimmed());
    }

    body.insert(QStringLiteral("IsDel"), m_scheduledDeleteCheck->isChecked() ? 1 : 0);
    body.insert(QStringLiteral("DelDate"),
                m_deleteDateTimeEdit->dateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));

    int cmdType = m_cmdTypeGroup->checkedId();
    if (cmdType < 0)
        cmdType = 2;
    body.insert(QStringLiteral("PartitionCmdType"), cmdType);
    body.insert(QStringLiteral("YbEgg"), m_ybEggOnRadio->isChecked());

    QJsonArray groupsArray;
    for (int i = 0; i < m_groupTable->rowCount(); ++i) {
        QTableWidgetItem *idCell = m_groupTable->item(i, 0);
        if (!idCell || idCell->checkState() != Qt::Checked)
            continue;
        QJsonObject g;
        bool ok = false;
        const int gId = idCell->text().toInt(&ok);
        if (ok) {
            g.insert(QStringLiteral("Id"), gId);
            g.insert(QStringLiteral("GroupId"), gId);
        }
        groupsArray.append(g);
    }
    body.insert(QStringLiteral("Groups"), groupsArray);

    if (IsEditMode()) {
        if (m_partitionType == 4) {
            if (m_partitionObject.contains(QStringLiteral("DbInfo")))
                body.insert(QStringLiteral("DbInfo"), m_partitionObject.value(QStringLiteral("DbInfo")));
            else if (m_partitionObject.contains(QStringLiteral("dbInfo")))
                body.insert(QStringLiteral("DbInfo"), m_partitionObject.value(QStringLiteral("dbInfo")));
        } else if (m_partitionType == 5) {
            static const QStringList kWebKeys = {QStringLiteral("WebUrl"),      QStringLiteral("webUrl"),
                                                 QStringLiteral("SuccessMark"), QStringLiteral("successMark"),
                                                 QStringLiteral("DataFormat"),  QStringLiteral("dataFormat")};
            for (const QString &k : kWebKeys) {
                if (m_partitionObject.contains(k))
                    body.insert(k, m_partitionObject.value(k));
            }
        }
    }

    if (!IsEditMode()) {
        body.insert(QStringLiteral("IsCreate"), true);
    }

    return body;
}
