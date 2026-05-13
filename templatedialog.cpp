#include "templatedialog.h"

#include "applogger.h"
#include "gatewayapiclient.h"
#include "pageutils.h"
#include "checkboxstyle.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QEvent>
#include <QFormLayout>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QList>
#include <QPalette>
#include <QSizePolicy>
#include <QWheelEvent>

namespace {

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
void SetFormLayoutRowVisible(QFormLayout *layout, QWidget *field, bool visible)
{
    if (!layout || !field)
        return;
    if (QWidget *lb = layout->labelForField(field))
        lb->setVisible(visible);
    field->setVisible(visible);
}
#else
void SetFormLayoutRowVisible(QFormLayout *layout, QWidget *field, bool visible)
{
    if (!layout || !field)
        return;
    layout->setRowVisible(field, visible);
}
#endif

/// 模板对话框内：未展开时不吞滚轮（交给外层 QScrollArea）；展开后列表可滚轮选项
/// showPopup 后提升弹出窗层级，减轻父级 QGraphicsDropShadowEffect 下列表点不到的问题
class TemplateDialogComboBox final : public QComboBox
{
public:
    explicit TemplateDialogComboBox(QWidget *parent = nullptr)
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

void AppendLabeledOptionRows(QVBoxLayout *vl, const QList<QPair<QString, QWidget *>> &rows)
{
    if (!vl)
        return;
    for (const auto &pr : rows) {
        if (!pr.second)
            continue;
        auto *row = new QWidget;
        auto *hl = new QHBoxLayout(row);
        hl->setContentsMargins(0, 4, 0, 4);
        hl->setSpacing(10);
        auto *lb = new QLabel(pr.first + QStringLiteral("："));
        lb->setStyleSheet(QStringLiteral("color: #5a3a41; font-size: 12px; font-weight: normal;"));
        lb->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        lb->setMinimumWidth(120);
        hl->addWidget(lb, 0, Qt::AlignTop);
        hl->addWidget(pr.second, 1);
        vl->addWidget(row);
    }
}


// 与 PartitionDialog / 主窗口一致的暗红主题
static const char *kBtnPrimary = R"(
QPushButton {
    color: #ffffff;
    background: #8b4a53;
    border: none;
    border-radius: 4px;
    font-size: 13px;
    font-weight: normal;
    padding: 6px 22px;
}
QPushButton:hover { background: #9a5660; }
QPushButton:pressed { background: #7a3f49; }
)";
static const char *kBtnSecondary = R"(
QPushButton {
    color: #8b4a53;
    background: #ffffff;
    border: 1px solid #dccbce;
    border-radius: 4px;
    font-size: 13px;
    font-weight: normal;
    padding: 6px 22px;
}
QPushButton:hover { background: #f5edef; }
QPushButton:pressed { background: #efe4e7; }
)";
static const char *kInputFieldStyle = R"(
QLineEdit, QComboBox {
    min-height: 38px;
    border: 1px solid #d9c8cb;
    border-radius: 8px;
    padding: 0 12px;
    font-size: 13px;
    color: #3f3135;
    background: #fffdfd;
    selection-background-color: #c98b95;
}
QLineEdit:focus, QComboBox:focus {
    border: 1px solid #a35a66;
    background: #ffffff;
}
QLineEdit::placeholder { color: #af9ca0; }
QComboBox::drop-down {
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
static QString TemplateDialogMainCheckStyle()
{
    return QStringLiteral(
               "QCheckBox { color: #3f3135; font-size: 12px; font-weight: 600; spacing: 8px; }\n")
        + GatewayCheckboxStyle::indicatorRules(16);
}
/// 赠送类 Tab 顶部开关条：与 Tab 内容区留出内边距，弱化贴边感
static const char *kGiftTabHeaderBarQss = R"(
#giftTabHeaderBar {
    background: #faf3f4;
    border: none;
    border-radius: 10px;
    padding: 8px 12px;
}
)";
/// 表格单元格内控件：避免沿用 kInputFieldStyle 的 min-height:38px 撑破行高导致多行重叠
static const char *kTableComboStyle = R"(
QComboBox {
    min-height: 26px;
    max-height: 30px;
    border: 1px solid #d9c8cb;
    border-radius: 4px;
    padding: 0 8px;
    font-size: 13px;
    color: #3f3135;
    background: #fffdfd;
}
QComboBox:focus { border: 1px solid #a35a66; background: #ffffff; }
QComboBox::drop-down {
    subcontrol-origin: padding;
    subcontrol-position: top right;
    width: 22px;
    border-left: 1px solid #e4d8da;
    background: #f8f1f2;
    border-top-right-radius: 4px;
    border-bottom-right-radius: 4px;
}
)";
static QString TemplateDialogTableCheckStyle()
{
    return QStringLiteral(
               "QCheckBox { color: #3f3135; font-size: 12px; font-weight: 600; spacing: 6px; margin: 2px 4px; }\n")
        + GatewayCheckboxStyle::indicatorRules(14);
}

QString JsonStr(const QJsonObject &o, const QStringList &keys)
{
    for (const QString &k : keys) {
        if (!o.contains(k))
            continue;
        const QJsonValue v = o.value(k);
        if (v.isString())
            return v.toString();
        if (v.isDouble())
            return QString::number(static_cast<qint64>(v.toDouble()));
        if (v.isBool())
            return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    }
    return {};
}

int JsonInt(const QJsonObject &o, const QStringList &keys, int defaultValue = 0)
{
    for (const QString &k : keys) {
        if (!o.contains(k))
            continue;
        const QJsonValue v = o.value(k);
        if (v.isDouble())
            return int(v.toDouble());
        if (v.isBool())
            return v.toBool() ? 1 : 0;
        if (v.isString()) {
            bool ok = false;
            const int n = v.toString().toInt(&ok);
            if (ok)
                return n;
        }
    }
    return defaultValue;
}

bool JsonBool(const QJsonObject &o, const QStringList &keys, bool defaultValue = false)
{
    for (const QString &k : keys) {
        if (!o.contains(k))
            continue;
        const QJsonValue v = o.value(k);
        if (v.isBool())
            return v.toBool();
        if (v.isDouble())
            return v.toInt() != 0;
        if (v.isString()) {
            const QString s = v.toString().toLower();
            if (s == QLatin1String("true") || s == QLatin1String("1"))
                return true;
            if (s == QLatin1String("false") || s == QLatin1String("0"))
                return false;
        }
    }
    return defaultValue;
}

QJsonArray JsonArr(const QJsonObject &o, const QStringList &keys)
{
    for (const QString &k : keys) {
        if (!o.contains(k))
            continue;
        const QJsonValue v = o.value(k);
        if (v.isArray())
            return v.toArray();
    }
    return {};
}

void SetupTable(QTableWidget *t, const QStringList &headers)
{
    t->setColumnCount(headers.size());
    t->setHorizontalHeaderLabels(headers);
    auto *hh = t->horizontalHeader();
    hh->setMinimumSectionSize(52);
    hh->setDefaultSectionSize(88);
    hh->setStretchLastSection(false);
    for (int i = 0; i < headers.size(); ++i)
        hh->setSectionResizeMode(i, QHeaderView::Interactive);
    if (!headers.isEmpty())
        hh->setSectionResizeMode(headers.size() - 1, QHeaderView::Stretch);
    t->verticalHeader()->setVisible(false);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::SelectedClicked);
    t->setShowGrid(true);
}

/// 数据写入后根据内容调整列宽，避免空表阶段列宽为 0 导致「竖线 / 感叹号」类绘制异常
void StretchLastVisibleColumn(QTableWidget *t)
{
    if (!t || t->columnCount() == 0)
        return;
    int visLast = -1;
    for (int c = t->columnCount() - 1; c >= 0; --c) {
        if (!t->isColumnHidden(c)) {
            visLast = c;
            break;
        }
    }
    if (visLast >= 0)
        t->horizontalHeader()->setSectionResizeMode(visLast, QHeaderView::Stretch);
}

void PolishDataTable(QTableWidget *t)
{
    if (!t || t->columnCount() == 0 || t->rowCount() == 0)
        return;
    // 附加/积分表 3~5 列为控件，避免 resizeColumnsToContents 把下拉压成一条线
    const bool hasGiveWidgets = t->columnCount() >= 7 && t->cellWidget(0, 3) != nullptr;
    if (hasGiveWidgets) {
        for (int c : {0, 1, 2, 6}) {
            if (!t->isColumnHidden(c))
                t->resizeColumnToContents(c);
        }
        // 与商户端 partinstallmod：赠送方式 | 网站 | 游戏显示
        t->setColumnWidth(3, qMax(t->columnWidth(3), 200));
        t->setColumnWidth(4, qMax(t->columnWidth(4), 72));
        t->setColumnWidth(5, qMax(t->columnWidth(5), 104));
        StretchLastVisibleColumn(t);
        for (int r = 0; r < t->rowCount(); ++r)
            t->resizeRowToContents(r);
        return;
    }
    t->resizeColumnsToContents();
    StretchLastVisibleColumn(t);
}

/// 编辑界面不展示 Id，提交/加载仍使用末列存 Id
void HideTemplateTableIdColumn(QTableWidget *t, int idCol)
{
    if (!t || idCol < 0 || idCol >= t->columnCount())
        return;
    t->setColumnHidden(idCol, true);
}

QString TableText(const QTableWidget *t, int row, int col)
{
    const QTableWidgetItem *it = t->item(row, col);
    return it ? it->text().trimmed() : QString();
}

void SetTableText(QTableWidget *t, int row, int col, const QString &text)
{
    if (!t->item(row, col))
        t->setItem(row, col, new QTableWidgetItem(text));
    else
        t->item(row, col)->setText(text);
}

bool InstallTemplateResponseOk(const QString &response)
{
    const QString r = response.trimmed();
    return r.compare(QStringLiteral("模板添加成功"), Qt::CaseInsensitive) == 0
        || r.compare(QStringLiteral("操作成功"), Qt::CaseInsensitive) == 0;
}

void FillIdNameCombo(QComboBox *cb, const QJsonArray &arr)
{
    cb->clear();
    cb->addItem(QStringLiteral("（无）"), 0);
    for (const auto &v : arr) {
        if (!v.isObject())
            continue;
        const QJsonObject o = v.toObject();
        const int id = JsonInt(o, {QStringLiteral("Id"), QStringLiteral("id")}, 0);
        if (id <= 0)
            continue;
        const QString name = JsonStr(o, {QStringLiteral("Name"), QStringLiteral("name")});
        cb->addItem(QStringLiteral("[%1] %2").arg(id).arg(name.isEmpty() ? QStringLiteral("未命名") : name), id);
    }
}

void SelectComboById(QComboBox *cb, int id)
{
    cb->setCurrentIndex(0);
    if (id <= 0)
        return;
    for (int i = 0; i < cb->count(); ++i) {
        if (cb->itemData(i).toInt() == id) {
            cb->setCurrentIndex(i);
            return;
        }
    }
    cb->addItem(QStringLiteral("[%1] %2").arg(id).arg(QStringLiteral("（未在列表）")), id);
    cb->setCurrentIndex(cb->count() - 1);
}

/// 与商户端 partinstallmod.vue `attachInfo.options` 一致
void FillAttachGiveTypeCombo(QComboBox *cb)
{
    cb->clear();
    const struct {
        const char *label;
        int v;
    } items[] = {
        {"关闭赠送", 0},
        {"按充值金额赠送", 1},
        {"充值金额 + 渠道赠送", 2},
        {"充值金额 + 激励赠送", 3},
        {"充值金额 + 激励赠送 + 渠道赠送", 4},
    };
    for (const auto &it : items)
        cb->addItem(QString::fromUtf8(it.label), it.v);
}

/// 游戏内展示：显示 / 部分显示 / 不显示（对应 Show 字段 0/1/2）
void FillGameShowCombo(QComboBox *cb)
{
    cb->clear();
    cb->addItem(QStringLiteral("显示"), 0);
    cb->addItem(QStringLiteral("部分显示"), 1);
    cb->addItem(QStringLiteral("不显示"), 2);
}

void SetComboCurrentByData(QComboBox *cb, int data)
{
    if (!cb)
        return;
    for (int i = 0; i < cb->count(); ++i) {
        if (cb->itemData(i).toInt() == data) {
            cb->setCurrentIndex(i);
            return;
        }
    }
    cb->setCurrentIndex(0);
}

QComboBox *FindComboInCellWidget(QWidget *cellWidget)
{
    if (!cellWidget) {
        return nullptr;
    }
    if (auto *cb = qobject_cast<QComboBox *>(cellWidget)) {
        return cb;
    }
    return cellWidget->findChild<QComboBox *>();
}

QCheckBox *FindCheckInCellWidget(QWidget *cellWidget)
{
    if (!cellWidget) {
        return nullptr;
    }
    if (auto *chk = qobject_cast<QCheckBox *>(cellWidget)) {
        return chk;
    }
    return cellWidget->findChild<QCheckBox *>();
}

/// 表格单元格内垂直居中，减轻下拉/勾选与格线错位
void AttachAlignedCellWidget(QTableWidget *table, int row, int col, QWidget *control)
{
    if (!table || !control) {
        return;
    }
    auto *wrap = new QWidget(table);
    auto *lay = new QHBoxLayout(wrap);
    lay->setContentsMargins(2, 0, 2, 0);
    lay->setSpacing(0);
    control->setParent(wrap);
    control->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    lay->addWidget(control, 0, Qt::AlignVCenter);
    table->setCellWidget(row, col, wrap);
}

int TableComboIntData(const QTableWidget *t, int row, int col, int fallback = 0)
{
    if (auto *w = t->cellWidget(row, col)) {
        if (QComboBox *cb = FindComboInCellWidget(w)) {
            return cb->currentData().toInt();
        }
    }
    const QString s = TableText(t, row, col);
    bool ok = false;
    const int n = s.toInt(&ok);
    return ok ? n : fallback;
}

bool TableCheckBool(const QTableWidget *t, int row, int col)
{
    if (auto *w = t->cellWidget(row, col)) {
        if (QCheckBox *chk = FindCheckInCellWidget(w)) {
            return chk->isChecked();
        }
    }
    const QString s = TableText(t, row, col);
    return s.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0 || s == QLatin1String("1");
}

} // namespace

TemplateDialog::TemplateDialog(int templateId, const QJsonObject &preloadTemplate, QWidget *parent)
    : QDialog(parent)
    , m_templateId(templateId)
    , m_sourceTemplate(preloadTemplate)
{
    setModal(true);
    setWindowFlags(Qt::Window | Qt::Dialog | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground, false);
    setAutoFillBackground(true);
    {
        QPalette pal = palette();
        pal.setColor(QPalette::Window, QColor(QStringLiteral("#e8dfe1")));
        setPalette(pal);
    }
    setObjectName(QStringLiteral("templateDialogRoot"));
    setStyleSheet(QStringLiteral("#templateDialogRoot { background-color: #e8dfe1; }"));
    setWindowTitle(m_templateId > 0 ? QStringLiteral("编辑模板") : QStringLiteral("新建模板"));
    setMinimumSize(920, 640);
    resize(980, 700);

    BuildUi();
    if (m_titleWidget)
        m_titleWidget->installEventFilter(this);

    LoadEngines();
    LoadInstallScanModelOptions();

    if (m_templateId > 0 && !preloadTemplate.isEmpty()) {
        LoadFromJson(preloadTemplate);
        LoadTempRatesIntoChannelTable(GatewayApiClient().GetTempProductRates(m_templateId, nullptr));
    } else {
        ApplyNewTemplateDefaults();
        LoadProductsIntoChannelTable();
    }
}

TemplateDialog::~TemplateDialog() = default;

void TemplateDialog::BuildUi()
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(10, 10, 10, 10);
    outer->setSpacing(0);

    auto *mainFrame = new QWidget(this);
    mainFrame->setObjectName(QStringLiteral("templateDialogMain"));
    mainFrame->setStyleSheet(QStringLiteral(
        "QWidget#templateDialogMain { background: #ffffff; border-radius: 12px; }"));
    auto *shadowEffect = new QGraphicsDropShadowEffect(mainFrame);
    shadowEffect->setBlurRadius(28);
    shadowEffect->setOffset(0, 8);
    shadowEffect->setColor(QColor(88, 44, 52, 60));
    mainFrame->setGraphicsEffect(shadowEffect);

    auto *mainLay = new QVBoxLayout(mainFrame);
    mainLay->setContentsMargins(0, 0, 0, 0);
    mainLay->setSpacing(0);

    m_titleWidget = new QWidget(mainFrame);
    m_titleWidget->setFixedHeight(48);
    m_titleWidget->setObjectName(QStringLiteral("templateDialogTitleBar"));
    m_titleWidget->setStyleSheet(QStringLiteral(
        "QWidget#templateDialogTitleBar {"
        "background: qlineargradient(x1:0,y1:0,x2:1,y2:0, stop:0 #8b4a53, stop:1 #9a5b66);"
        "border-top-left-radius: 12px;"
        "border-top-right-radius: 12px;"
        "}"));
    auto *titleLay = new QHBoxLayout(m_titleWidget);
    titleLay->setContentsMargins(16, 0, 8, 0);
    auto *titleLbl = new QLabel(m_templateId > 0 ? QStringLiteral("编辑模板") : QStringLiteral("新建模板"), m_titleWidget);
    titleLbl->setStyleSheet(QStringLiteral("color: #ffffff; font-size: 15px; font-weight: normal; background: transparent;"));
    titleLay->addWidget(titleLbl);
    titleLay->addStretch();
    auto *closeBtn = new QPushButton(QStringLiteral("✕"), m_titleWidget);
    closeBtn->setFixedSize(32, 32);
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setStyleSheet(QStringLiteral(
        "QPushButton { color: #ffffff; background: transparent; border: none; border-radius: 16px; font-size: 14px; font-weight: normal; }"
        "QPushButton:hover { background: rgba(255,255,255,0.2); }"));
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);
    titleLay->addWidget(closeBtn);
    mainLay->addWidget(m_titleWidget);

    m_tabs = new QTabWidget(mainFrame);
    m_tabs->setDocumentMode(true);
    m_tabs->setStyleSheet(QStringLiteral(
        "QTabWidget::pane { border: none; background: #ffffff; margin: 2px 14px 12px 14px; "
        "border-radius: 0 0 10px 10px; }"
        "QTabBar::tab { background: #f0e6e8; color: #6a4b52; padding: 8px 18px; margin-right: 2px; "
        "border-top-left-radius: 8px; border-top-right-radius: 8px; font-weight: normal; }"
        "QTabBar::tab:selected { background: #ffffff; color: #8b4a53; }"
        "QTabBar { alignment: left; margin-left: 14px; margin-top: 8px; }"));

    // ----- 基础 -----
    m_basicTab = new QWidget;
    auto *basicScroll = new QScrollArea;
    basicScroll->setWidgetResizable(true);
    basicScroll->setFrameShape(QFrame::NoFrame);
    basicScroll->setStyleSheet(QStringLiteral(
        "QScrollArea { background: #ffffff; border: none; }"
        "QScrollBar:vertical { width: 6px; background: #f5f5f5; }"
        "QScrollBar::handle:vertical { background: #d0c0c0; border-radius: 3px; min-height: 20px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }"));
    auto *basicInner = new QWidget;
    basicInner->setStyleSheet(QStringLiteral(
        "QWidget#basicInnerForm { background: #ffffff; }"
        "QWidget#basicInnerForm QLabel { color: #5a3a41; font-size: 12px; font-weight: normal; }"));
    basicInner->setObjectName(QStringLiteral("basicInnerForm"));
    auto *form = new QFormLayout(basicInner);
    m_basicFormLayout = form;
    form->setHorizontalSpacing(14);
    form->setVerticalSpacing(10);

    m_nameEdit = new QLineEdit;
    m_gameNameEdit = new QLineEdit;
    m_gameNameEdit->setPlaceholderText(QStringLiteral("模板类型为「通用 SQL」等时填写游戏名称"));
    m_typeCombo = new TemplateDialogComboBox;
    m_typeCombo->addItem(QStringLiteral("热血传奇 (Rxcq)"), 1);
    m_typeCombo->addItem(QStringLiteral("传世 (Cs)"), 2);
    m_typeCombo->addItem(QStringLiteral("传奇3 (Cq3)"), 3);
    m_typeCombo->addItem(QStringLiteral("通用 SQL (Ty)"), 4);
    m_typeCombo->addItem(QStringLiteral("Web (Web)"), 5);
    m_typeCombo->addItem(QStringLiteral("奇迹 (QJ)"), 6);

    m_currencyCombo = new TemplateDialogComboBox;
    m_currencyCombo->addItem(QStringLiteral("元宝"), QStringLiteral("GAMEGOLD +"));
    m_currencyCombo->addItem(QStringLiteral("金币"), QStringLiteral("give 金币"));
    m_currencyCombo->addItem(QStringLiteral("自定义"), QString());
    connect(m_currencyCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &TemplateDialog::OnCurrencyChanged);

    m_scriptCommandEdit = new QLineEdit;
    m_ratioEdit = new QLineEdit;
    m_minAmountEdit = new QLineEdit;
    m_maxAmountEdit = new QLineEdit;
    m_engineCombo = new TemplateDialogComboBox;
    connect(m_engineCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &TemplateDialog::OnEngineChanged);
    m_browserCommandEdit = new QLineEdit;
    m_payDirEdit = new QLineEdit;
    m_tongQuDirEdit = new QLineEdit;
    m_dirEdit = new QLineEdit;
    m_isTongQuCheck = new QCheckBox(QStringLiteral("通区"));
    m_dirModeCombo = new TemplateDialogComboBox;
    m_dirModeCombo->addItem(QStringLiteral("同盘符"), 0);
    m_dirModeCombo->addItem(QStringLiteral("不同盘符"), 1);
    m_isTestCheck = new QCheckBox(QStringLiteral("有测试区"));
    m_isBetchCheck = new QCheckBox(QStringLiteral("额外补发只补发主货币"));
    m_betchEdit = new QLineEdit;
    m_safetyMoneyEdit = new QLineEdit;
    m_rechargeWayCombo = new TemplateDialogComboBox;
    m_rechargeWayCombo->addItem(QStringLiteral("游戏账号"), QStringLiteral("游戏账号"));
    m_rechargeWayCombo->addItem(QStringLiteral("游戏角色"), QStringLiteral("游戏角色"));

    m_equipMethodCombo = new TemplateDialogComboBox;
    m_equipMethodCombo->addItem(QStringLiteral("关闭赠送"), 0);
    m_equipMethodCombo->addItem(QStringLiteral("按充值金额赠送"), 1);
    m_equipMethodCombo->addItem(QStringLiteral("充值+渠道"), 2);
    m_equipMethodCombo->addItem(QStringLiteral("充值+激励"), 3);
    m_equipMethodCombo->addItem(QStringLiteral("充值+激励+渠道"), 4);

    m_showAdditionalCheck = new QCheckBox(QStringLiteral("显示附加赠送信息"));
    m_showEquipCheck = new QCheckBox(QStringLiteral("网站显示"));
    m_isShowCheck = new QCheckBox(QStringLiteral("游戏显示"));
    m_showIntegralCheck = new QCheckBox(QStringLiteral("显示积分赠送信息"));
    m_giveStateCheck = new QCheckBox(QStringLiteral("开启充值赠送(渠道)"));
    // 与商户端 inciteInfo.checked → isContains（开启激励赠送）
    m_isContainsCheck = new QCheckBox(QStringLiteral("开启激励赠送"));

    m_templateColorCombo = new TemplateDialogComboBox;
    for (int c = 0; c <= 5; ++c)
        m_templateColorCombo->addItem(QStringLiteral("模板颜色 %1").arg(c), c);

    m_installModelCombo = new TemplateDialogComboBox;
    m_scanModelCombo = new TemplateDialogComboBox;
    m_isWxmbCheck = new QCheckBox(QStringLiteral("开启微信密保功能"));
    m_isScanCheck = new QCheckBox(QStringLiteral("开启游戏内扫码支付"));
    m_isShowGlodCombo = new TemplateDialogComboBox;
    m_isShowGlodCombo->addItem(QStringLiteral("完全显示"), 0);
    m_isShowGlodCombo->addItem(QStringLiteral("部分显示"), 1);
    m_isShowGlodCombo->addItem(QStringLiteral("不显示"), 2);
    m_giveOptionStateCombo = new TemplateDialogComboBox;
    m_giveOptionStateCombo->addItem(QStringLiteral("按充值金额计算"), 0);
    m_giveOptionStateCombo->addItem(QStringLiteral("充值金额+渠道赠送"), 1);
    m_giveOptionCombo = new TemplateDialogComboBox;
    m_giveOptionCombo->addItem(QStringLiteral("只送最大金额"), 0);
    m_giveOptionCombo->addItem(QStringLiteral("送所有符合的金额"), 1);

    m_giveWayCombo = new TemplateDialogComboBox;
    m_giveWayCombo->addItem(QStringLiteral("按固定比例"), 0);
    m_giveWayCombo->addItem(QStringLiteral("按充值金额"), 1);

    m_redPacketStateCheck = new QCheckBox(QStringLiteral("红包赠送"));
    m_redPacketAdditionalCheck = new QCheckBox(QStringLiteral("红包-附加"));
    m_redPacketEquipCheck = new QCheckBox(QStringLiteral("红包-装备"));
    m_redPacketIntegralCheck = new QCheckBox(QStringLiteral("红包-积分"));

    form->addRow(QStringLiteral("模板名称"), m_nameEdit);
    form->addRow(QStringLiteral("游戏名称"), m_gameNameEdit);
    form->addRow(QStringLiteral("模板类型"), m_typeCombo);
    form->addRow(QStringLiteral("游戏币类型"), m_currencyCombo);
    form->addRow(QStringLiteral("脚本命令"), m_scriptCommandEdit);
    form->addRow(QStringLiteral("兑换比例"), m_ratioEdit);
    form->addRow(QStringLiteral("最小金额(元)"), m_minAmountEdit);
    form->addRow(QStringLiteral("最大金额(元)"), m_maxAmountEdit);
    form->addRow(QStringLiteral("游戏引擎"), m_engineCombo);
    form->addRow(QStringLiteral("浏览器指令"), m_browserCommandEdit);
    // 通区相关：与商户端 partinstallmod 一致——仅「通区=是」时显示目录类别/测试区/通区目录等；「有测试区=是」时显示补发
    auto *flags = new QWidget;
    auto *fl = new QHBoxLayout(flags);
    fl->setContentsMargins(0, 0, 0, 0);
    fl->addWidget(m_isTongQuCheck);
    fl->addStretch();
    form->addRow(QStringLiteral("通区"), flags);
    form->addRow(QStringLiteral("目录类别"), m_dirModeCombo);
    form->addRow(QStringLiteral("有测试区"), m_isTestCheck);
    m_betchEdit->setPlaceholderText(QStringLiteral("如 20"));
    form->addRow(QStringLiteral("额外补发(%)"), m_betchEdit);
    form->addRow(QStringLiteral("补发选项"), m_isBetchCheck);
    form->addRow(QStringLiteral("通区目录"), m_tongQuDirEdit);
    form->addRow(QStringLiteral("通区盘符"), m_dirEdit);
    form->addRow(QStringLiteral("充值脚本路径"), m_payDirEdit);
    form->addRow(QStringLiteral("风控金额"), m_safetyMoneyEdit);
    m_safetyMoneyEdit->setPlaceholderText(QStringLiteral("额外赠送金额，0 为不赠送"));
    form->addRow(QStringLiteral("游戏中展示"), m_isShowGlodCombo);
    form->addRow(QStringLiteral("扫码支付"), m_isScanCheck);
    form->addRow(QStringLiteral("扫码模板"), m_scanModelCombo);
    form->addRow(QStringLiteral("微信密保"), m_isWxmbCheck);
    form->addRow(QStringLiteral("密保模板"), m_installModelCombo);
    form->addRow(QStringLiteral("模板颜色"), m_templateColorCombo);

    form->addRow(QStringLiteral("充值方式"), m_rechargeWayCombo);

    basicScroll->setWidget(basicInner);
    auto *bv = new QVBoxLayout(m_basicTab);
    bv->addWidget(basicScroll);
    m_tabs->addTab(m_basicTab, QStringLiteral("基础"));

    // ----- NPC -----
    m_npcTab = new QWidget;
    auto *nv = new QVBoxLayout(m_npcTab);
    nv->setContentsMargins(14, 12, 14, 14);
    nv->setSpacing(10);
    m_npcTable = new QTableWidget;
    // 与商户端 partinstallmod「充值 NPC」列顺序；末列存 Id，界面隐藏
    SetupTable(m_npcTable, {QStringLiteral("NPC名称"), QStringLiteral("地图"), QStringLiteral("外观"), QStringLiteral("X坐标"), QStringLiteral("Y坐标"), QString()});
    auto *nb = new QHBoxLayout;
    nb->setSpacing(10);
    auto *addNpc = new QPushButton(QStringLiteral("添加行"));
    auto *rmNpc = new QPushButton(QStringLiteral("删除选中行"));
    addNpc->setMinimumWidth(84);
    rmNpc->setMinimumWidth(108);
    addNpc->setStyleSheet(QLatin1String(kBtnPrimary));
    rmNpc->setStyleSheet(QLatin1String(kBtnSecondary));
    connect(addNpc, &QPushButton::clicked, this, [this]() {
        const int r = m_npcTable->rowCount();
        m_npcTable->insertRow(r);
        SetTableText(m_npcTable, r, 0, QString());
        SetTableText(m_npcTable, r, 1, QStringLiteral("3"));
        SetTableText(m_npcTable, r, 2, QStringLiteral("12"));
        SetTableText(m_npcTable, r, 3, QStringLiteral("343"));
        SetTableText(m_npcTable, r, 4, QStringLiteral("383"));
        SetTableText(m_npcTable, r, 5, QStringLiteral("0"));
    });
    connect(rmNpc, &QPushButton::clicked, this, [this]() {
        const QList<QTableWidgetItem *> sel = m_npcTable->selectedItems();
        if (sel.isEmpty())
            return;
        QSet<int> rows;
        for (auto *i : sel)
            rows.insert(i->row());
        const QList<int> rl = rows.values();
        for (int k = rl.size() - 1; k >= 0; --k)
            m_npcTable->removeRow(rl.at(k));
    });
    nb->addWidget(addNpc);
    nb->addWidget(rmNpc);
    nb->addStretch();
    auto *npcHint = new QLabel(QStringLiteral("每行填写 NPC 名称、地图与坐标（多个 NPC 时请区分名称）"));
    npcHint->setStyleSheet(QStringLiteral("color: #6a4b52; font-size: 12px; font-weight: normal;"));
    nv->addWidget(npcHint);
    nv->addWidget(m_npcTable);
    nv->addLayout(nb);
    m_tabs->addTab(m_npcTab, QStringLiteral("NPC"));

    const auto makeTableTab = [this](const QString &title, QTableWidget **outTable, const QStringList &headers,
                                     const QList<QCheckBox *> &headerChecks = {},
                                     const QList<QPair<QString, QWidget *>> &optionRows = {},
                                     QWidget **outBodyShell = nullptr) -> QWidget * {
        auto *w = new QWidget;
        auto *vl = new QVBoxLayout(w);
        vl->setContentsMargins(14, 12, 14, 14);
        vl->setSpacing(12);
        if (!headerChecks.isEmpty()) {
            auto *headOuter = new QWidget(w);
            auto *headOuterLay = new QHBoxLayout(headOuter);
            headOuterLay->setContentsMargins(10, 0, 10, 0);
            headOuterLay->setSpacing(0);
            auto *headWrap = new QWidget(headOuter);
            headWrap->setObjectName(QStringLiteral("giftTabHeaderBar"));
            headWrap->setStyleSheet(QLatin1String(kGiftTabHeaderBarQss));
            auto *hl = new QHBoxLayout(headWrap);
            hl->setContentsMargins(0, 0, 0, 0);
            hl->setSpacing(18);
            for (QCheckBox *cb : headerChecks) {
                if (cb)
                    hl->addWidget(cb);
            }
            hl->addStretch();
            headOuterLay->addWidget(headWrap);
            vl->addWidget(headOuter);
        }
        QVBoxLayout *contentLay = vl;
        if (outBodyShell) {
            *outBodyShell = new QWidget(w);
            auto *bl = new QVBoxLayout(*outBodyShell);
            bl->setContentsMargins(0, 0, 0, 0);
            bl->setSpacing(10);
            contentLay = bl;
            vl->addWidget(*outBodyShell, 1);
        }
        AppendLabeledOptionRows(contentLay, optionRows);
        *outTable = new QTableWidget;
        SetupTable(*outTable, headers);
        auto *hb = new QHBoxLayout;
        hb->setSpacing(10);
        auto *add = new QPushButton(QStringLiteral("添加行"));
        auto *rm = new QPushButton(QStringLiteral("删除选中行"));
        add->setMinimumWidth(84);
        rm->setMinimumWidth(108);
        add->setStyleSheet(QLatin1String(kBtnPrimary));
        rm->setStyleSheet(QLatin1String(kBtnSecondary));
        connect(add, &QPushButton::clicked, this, [this, outTable, headers]() {
            const int r = (*outTable)->rowCount();
            (*outTable)->insertRow(r);
            for (int c = 0; c < headers.size(); ++c)
                SetTableText(*outTable, r, c, QString());
            if (*outTable == m_additionalTable || *outTable == m_integralTable)
                SetupAdditionalIntegralRowWidgets(*outTable, r);
        });
        connect(rm, &QPushButton::clicked, this, [outTable]() {
            QList<QTableWidgetItem *> sel = (*outTable)->selectedItems();
            QSet<int> rows;
            for (auto *i : sel)
                rows.insert(i->row());
            const QList<int> rl = rows.values();
            for (int k = rl.size() - 1; k >= 0; --k)
                (*outTable)->removeRow(rl.at(k));
        });
        hb->addWidget(add);
        hb->addWidget(rm);
        hb->addStretch();
        contentLay->addWidget(*outTable, 1);
        contentLay->addLayout(hb);
        m_tabs->addTab(w, title);
        return w;
    };

    m_additionalGiftTabPage = makeTableTab(QStringLiteral("附加赠送"), &m_additionalTable,
                                           {QStringLiteral("附加奖励"), QStringLiteral("脚本命令"), QStringLiteral("赠送比例"), QStringLiteral("赠送方式"), QStringLiteral("网站显示"), QStringLiteral("游戏显示"), QString()},
                                           {m_showAdditionalCheck});
    m_integralGiftTabPage = makeTableTab(QStringLiteral("积分赠送"), &m_integralTable,
                                         {QStringLiteral("附加奖励"), QStringLiteral("文件路径"), QStringLiteral("赠送比例"), QStringLiteral("赠送方式"), QStringLiteral("网站显示"), QStringLiteral("游戏显示"), QString()},
                                         {m_showIntegralCheck});
    // 列顺序与商户端装备表一致：金额、脚本、赠送装备名称
    m_equipGiftTabPage = makeTableTab(QStringLiteral("装备赠送"), &m_equipTable,
                                      {QStringLiteral("金额(元)"), QStringLiteral("脚本命令"), QStringLiteral("赠送装备"), QString()},
                                      {m_showEquipCheck, m_isShowCheck},
                                      {{QStringLiteral("计算选项"), m_equipMethodCombo}, {QStringLiteral("赠送选项"), m_giveOptionCombo}},
                                      &m_equipTabBody);
    m_incentiveGiftTabPage = makeTableTab(QStringLiteral("激励赠送"), &m_incentiveTable,
                                          {QStringLiteral("满额(元)"), QStringLiteral("赠送金额(元)"), QString()},
                                          {m_isContainsCheck},
                                          {{QStringLiteral("赠送选项"), m_giveOptionStateCombo}},
                                          &m_incentiveTabBody);
    m_channelGiftTabPage = makeTableTab(QStringLiteral("渠道赠送"), &m_channelTable,
                                        {QStringLiteral("产品"), QStringLiteral("赠送比例"), QString(), QString()},
                                        {m_giveStateCheck},
                                        {{QStringLiteral("赠送选项"), m_giveWayCombo}},
                                        &m_channelTabBody);

    // ----- 红包赠送（NPC Type=false + RedPacketGives）-----
    m_redPacketTab = new QWidget;
    auto *rv = new QVBoxLayout(m_redPacketTab);
    rv->setContentsMargins(14, 12, 14, 14);
    rv->setSpacing(12);
    {
        auto *headOuter = new QWidget(m_redPacketTab);
        auto *headOuterLay = new QHBoxLayout(headOuter);
        headOuterLay->setContentsMargins(10, 0, 10, 0);
        headOuterLay->setSpacing(0);
        auto *headWrap = new QWidget(headOuter);
        headWrap->setObjectName(QStringLiteral("giftTabHeaderBar"));
        headWrap->setStyleSheet(QLatin1String(kGiftTabHeaderBarQss));
        auto *hl = new QHBoxLayout(headWrap);
        hl->setContentsMargins(0, 0, 0, 0);
        hl->setSpacing(18);
        for (auto *cb : {m_redPacketStateCheck, m_redPacketAdditionalCheck, m_redPacketEquipCheck, m_redPacketIntegralCheck}) {
            if (cb)
                hl->addWidget(cb);
        }
        hl->addStretch();
        headOuterLay->addWidget(headWrap);
        rv->addWidget(headOuter);
    }
    m_redPacketTabBody = new QWidget(m_redPacketTab);
    auto *rbl = new QVBoxLayout(m_redPacketTabBody);
    rbl->setContentsMargins(0, 6, 0, 0);
    rbl->setSpacing(14);
    auto *redNpcHint = new QLabel(QStringLiteral("红包 NPC（与充值 NPC 分开维护）"));
    redNpcHint->setStyleSheet(QStringLiteral("color: #6a4b52; font-size: 12px; font-weight: normal;"));
    rbl->addWidget(redNpcHint);
    m_redNpcTable = new QTableWidget;
    SetupTable(m_redNpcTable, {QStringLiteral("NPC名称"), QStringLiteral("地图"), QStringLiteral("外观"), QStringLiteral("X坐标"), QStringLiteral("Y坐标"), QString()});
    auto *rNpcHb = new QHBoxLayout;
    auto *addRedNpc = new QPushButton(QStringLiteral("添加行"));
    auto *rmRedNpc = new QPushButton(QStringLiteral("删除选中行"));
    addRedNpc->setMinimumWidth(84);
    rmRedNpc->setMinimumWidth(108);
    addRedNpc->setStyleSheet(QLatin1String(kBtnPrimary));
    rmRedNpc->setStyleSheet(QLatin1String(kBtnSecondary));
    connect(addRedNpc, &QPushButton::clicked, this, [this]() {
        const int r = m_redNpcTable->rowCount();
        m_redNpcTable->insertRow(r);
        SetTableText(m_redNpcTable, r, 0, QString());
        SetTableText(m_redNpcTable, r, 1, QStringLiteral("3"));
        SetTableText(m_redNpcTable, r, 2, QStringLiteral("12"));
        SetTableText(m_redNpcTable, r, 3, QStringLiteral("343"));
        SetTableText(m_redNpcTable, r, 4, QStringLiteral("383"));
        SetTableText(m_redNpcTable, r, 5, QStringLiteral("0"));
    });
    connect(rmRedNpc, &QPushButton::clicked, this, [this]() {
        const QList<QTableWidgetItem *> sel = m_redNpcTable->selectedItems();
        if (sel.isEmpty())
            return;
        QSet<int> rows;
        for (auto *i : sel)
            rows.insert(i->row());
        const QList<int> rl = rows.values();
        for (int k = rl.size() - 1; k >= 0; --k)
            m_redNpcTable->removeRow(rl.at(k));
    });
    rNpcHb->setSpacing(10);
    rNpcHb->setContentsMargins(0, 0, 0, 0);
    rNpcHb->addWidget(addRedNpc);
    rNpcHb->addWidget(rmRedNpc);
    rNpcHb->addStretch();
    rbl->addWidget(m_redNpcTable, 1);
    rbl->addLayout(rNpcHb);

    rbl->addSpacing(10);

    auto *rDetHb = new QHBoxLayout;
    rDetHb->setSpacing(10);
    rDetHb->setContentsMargins(0, 0, 0, 0);
    auto *addDet = new QPushButton(QStringLiteral("添加行"));
    auto *rmDet = new QPushButton(QStringLiteral("删除选中行"));
    addDet->setMinimumWidth(84);
    rmDet->setMinimumWidth(108);
    addDet->setStyleSheet(QLatin1String(kBtnPrimary));
    rmDet->setStyleSheet(QLatin1String(kBtnSecondary));
    connect(addDet, &QPushButton::clicked, this, [this]() {
        const int r = m_redPacketDetailTable->rowCount();
        m_redPacketDetailTable->insertRow(r);
        SetTableText(m_redPacketDetailTable, r, 0, QStringLiteral("0"));
        SetTableText(m_redPacketDetailTable, r, 1, QStringLiteral("0"));
        SetTableText(m_redPacketDetailTable, r, 2, QStringLiteral("0"));
        SetTableText(m_redPacketDetailTable, r, 3, QStringLiteral("0"));
    });
    connect(rmDet, &QPushButton::clicked, this, [this]() {
        const QList<QTableWidgetItem *> sel = m_redPacketDetailTable->selectedItems();
        if (sel.isEmpty())
            return;
        QSet<int> rows;
        for (auto *i : sel)
            rows.insert(i->row());
        const QList<int> rl = rows.values();
        for (int k = rl.size() - 1; k >= 0; --k)
            m_redPacketDetailTable->removeRow(rl.at(k));
    });
    rDetHb->addWidget(addDet);
    rDetHb->addWidget(rmDet);
    rDetHb->addStretch();
    rbl->addLayout(rDetHb);

    auto *redDetHint = new QLabel(QStringLiteral("红包赠送详情（金额区间）"));
    redDetHint->setStyleSheet(QStringLiteral("color: #6a4b52; font-size: 12px; font-weight: normal; margin-top: 2px;"));
    rbl->addWidget(redDetHint);

    m_redPacketDetailTable = new QTableWidget;
    SetupTable(m_redPacketDetailTable,
               {QStringLiteral("赠送金额"), QStringLiteral("起始金额(元)"), QStringLiteral("截止金额(元)"), QString()});
    rbl->addWidget(m_redPacketDetailTable, 1);
    rv->addWidget(m_redPacketTabBody, 1);
    m_tabs->addTab(m_redPacketTab, QStringLiteral("红包赠送"));

    HideTemplateTableIdColumn(m_npcTable, 5);
    HideTemplateTableIdColumn(m_redNpcTable, 5);
    HideTemplateTableIdColumn(m_additionalTable, 6);
    HideTemplateTableIdColumn(m_integralTable, 6);
    HideTemplateTableIdColumn(m_equipTable, 3);
    HideTemplateTableIdColumn(m_incentiveTable, 2);
    HideTemplateTableIdColumn(m_channelTable, 2);
    HideTemplateTableIdColumn(m_channelTable, 3);
    HideTemplateTableIdColumn(m_redPacketDetailTable, 3);

    mainLay->addWidget(m_tabs, 1);

    auto *footer = new QWidget(mainFrame);
    footer->setFixedHeight(64);
    footer->setStyleSheet(QStringLiteral(
        "QWidget { background: #faf5f6; border-top: 1px solid #ede1e4; "
        "border-bottom-left-radius: 12px; border-bottom-right-radius: 12px; }"));
    auto *fLay = new QHBoxLayout(footer);
    fLay->setContentsMargins(18, 12, 18, 12);
    fLay->setSpacing(10);
    m_cancelBtn = new QPushButton(QStringLiteral("取消"), footer);
    m_confirmBtn = new QPushButton(QStringLiteral("保存"), footer);
    m_cancelBtn->setFixedHeight(36);
    m_confirmBtn->setFixedHeight(36);
    m_cancelBtn->setCursor(Qt::PointingHandCursor);
    m_confirmBtn->setCursor(Qt::PointingHandCursor);
    m_cancelBtn->setStyleSheet(QLatin1String(kBtnSecondary));
    m_confirmBtn->setStyleSheet(QLatin1String(kBtnPrimary));
    connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_confirmBtn, &QPushButton::clicked, this, &TemplateDialog::OnConfirm);
    fLay->addStretch();
    fLay->addWidget(m_cancelBtn);
    fLay->addWidget(m_confirmBtn);
    mainLay->addWidget(footer);

    outer->addWidget(mainFrame);

    connect(m_isWxmbCheck, &QCheckBox::toggled, this, &TemplateDialog::SyncWxmbScanWidgets);
    connect(m_isScanCheck, &QCheckBox::toggled, this, &TemplateDialog::SyncWxmbScanWidgets);
    connect(m_isTongQuCheck, &QCheckBox::toggled, this, &TemplateDialog::SyncTongQuWidgets);
    connect(m_isTestCheck, &QCheckBox::toggled, this, &TemplateDialog::SyncTongQuWidgets);
    connect(m_isContainsCheck, &QCheckBox::toggled, this, &TemplateDialog::SyncGiftTabEnabledStates);
    connect(m_giveStateCheck, &QCheckBox::toggled, this, &TemplateDialog::SyncGiftTabEnabledStates);
    connect(m_showEquipCheck, &QCheckBox::toggled, this, &TemplateDialog::SyncGiftTabEnabledStates);
    connect(m_redPacketStateCheck, &QCheckBox::toggled, this, &TemplateDialog::SyncGiftTabEnabledStates);
    SyncWxmbScanWidgets();
    SyncTongQuWidgets();
    SyncGiftTabEnabledStates();

    ApplyThemedFieldStyles();
}

void TemplateDialog::LoadEngines()
{
    m_engineCombo->clear();
    QString err;
    const QJsonArray engines = GatewayApiClient().GetEngines(&err);
    for (const auto &e : engines) {
        if (!e.isObject())
            continue;
        const QJsonObject eo = e.toObject();
        const QString name = JsonStr(eo, {QStringLiteral("Engine"), QStringLiteral("engine")});
        const QString cmd = JsonStr(eo, {QStringLiteral("Command"), QStringLiteral("command")});
        if (name.isEmpty())
            continue;
        m_engineCombo->addItem(name, QVariantMap{{QStringLiteral("name"), name}, {QStringLiteral("cmd"), cmd}});
    }
    if (m_engineCombo->count() == 0) {
        m_engineCombo->addItem(QStringLiteral("(无引擎列表)"), QVariantMap{{QStringLiteral("name"), QString()}, {QStringLiteral("cmd"), QString()}});
    }
}

void TemplateDialog::OnCurrencyChanged(int index)
{
    Q_UNUSED(index);
    const QString cmd = m_currencyCombo->currentData().toString();
    if (!cmd.isEmpty())
        m_scriptCommandEdit->setText(cmd);
}

void TemplateDialog::OnEngineChanged(int index)
{
    const QVariantMap m = m_engineCombo->itemData(index).toMap();
    m_browserCommandEdit->setText(m.value(QStringLiteral("cmd")).toString());
}

void TemplateDialog::ApplyNewTemplateDefaults()
{
    m_nameEdit->clear();
    m_gameNameEdit->clear();
    m_typeCombo->setCurrentIndex(0);
    m_currencyCombo->setCurrentIndex(0);
    OnCurrencyChanged(0);
    m_ratioEdit->setText(QStringLiteral("100"));
    m_minAmountEdit->setText(QStringLiteral("1"));
    m_maxAmountEdit->setText(QStringLiteral("100000"));
    if (m_engineCombo->count() > 0) {
        m_engineCombo->setCurrentIndex(0);
        OnEngineChanged(0);
    }
    m_payDirEdit->setText(QStringLiteral("7XPAY充值"));
    m_tongQuDirEdit->setText(QStringLiteral("D:\\通区目录"));
    m_dirEdit->setText(QStringLiteral("D"));
    m_isTongQuCheck->setChecked(false);
    if (m_dirModeCombo)
        m_dirModeCombo->setCurrentIndex(0);
    m_isTestCheck->setChecked(true);
    m_isBetchCheck->setChecked(false);
    m_betchEdit->setText(QStringLiteral("20"));
    m_safetyMoneyEdit->setText(QStringLiteral("0"));
    m_rechargeWayCombo->setCurrentIndex(0);
    m_equipMethodCombo->setCurrentIndex(0);
    m_showAdditionalCheck->setChecked(true);
    m_showEquipCheck->setChecked(true);
    m_isShowCheck->setChecked(true);
    m_showIntegralCheck->setChecked(false);
    m_giveStateCheck->setChecked(false);
    m_isContainsCheck->setChecked(false);
    if (m_templateColorCombo)
        m_templateColorCombo->setCurrentIndex(0);
    if (m_installModelCombo)
        m_installModelCombo->setCurrentIndex(0);
    if (m_scanModelCombo)
        m_scanModelCombo->setCurrentIndex(0);
    if (m_isWxmbCheck)
        m_isWxmbCheck->setChecked(false);
    if (m_isScanCheck)
        m_isScanCheck->setChecked(false);
    if (m_isShowGlodCombo)
        SetComboCurrentByData(m_isShowGlodCombo, 0);
    if (m_giveOptionCombo)
        SetComboCurrentByData(m_giveOptionCombo, 0);
    if (m_giveOptionStateCombo)
        SetComboCurrentByData(m_giveOptionStateCombo, 0);
    if (m_giveWayCombo)
        SetComboCurrentByData(m_giveWayCombo, 0);
    SyncWxmbScanWidgets();
    SyncTongQuWidgets();
    m_redPacketStateCheck->setChecked(false);
    m_redPacketAdditionalCheck->setChecked(false);
    m_redPacketEquipCheck->setChecked(false);
    m_redPacketIntegralCheck->setChecked(false);

    m_npcTable->setRowCount(0);
    m_npcTable->insertRow(0);
    SetTableText(m_npcTable, 0, 0, QStringLiteral("充值使者"));
    SetTableText(m_npcTable, 0, 1, QStringLiteral("3"));
    SetTableText(m_npcTable, 0, 2, QStringLiteral("12"));
    SetTableText(m_npcTable, 0, 3, QStringLiteral("343"));
    SetTableText(m_npcTable, 0, 4, QStringLiteral("383"));
    SetTableText(m_npcTable, 0, 5, QStringLiteral("0"));
    m_npcTable->insertRow(1);
    SetTableText(m_npcTable, 1, 0, QStringLiteral("充值使者"));
    SetTableText(m_npcTable, 1, 1, QStringLiteral("0"));
    SetTableText(m_npcTable, 1, 2, QStringLiteral("12"));
    SetTableText(m_npcTable, 1, 3, QStringLiteral("332"));
    SetTableText(m_npcTable, 1, 4, QStringLiteral("364"));
    SetTableText(m_npcTable, 1, 5, QStringLiteral("0"));

    const QList<QStringList> addRows = {
        {QStringLiteral("金刚石"), QStringLiteral("GAMEDIAMOND +"), QStringLiteral("1"), QStringLiteral("0")},
        {QStringLiteral("荣誉点"), QStringLiteral("GAMEGLORY +"), QStringLiteral("1"), QStringLiteral("0")},
        {QStringLiteral("游戏点"), QStringLiteral("GAMEPOINT +"), QStringLiteral("1"), QStringLiteral("0")},
        {QStringLiteral("声望"), QStringLiteral("GAMEGIRD +"), QStringLiteral("1"), QStringLiteral("0")},
        {QStringLiteral("灵符"), QStringLiteral("CREDITPOINT +"), QStringLiteral("1"), QStringLiteral("0")},
    };
    m_additionalTable->setRowCount(0);
    for (int i = 0; i < addRows.size(); ++i) {
        m_additionalTable->insertRow(i);
        SetTableText(m_additionalTable, i, 0, addRows.at(i).at(0));
        SetTableText(m_additionalTable, i, 1, addRows.at(i).at(1));
        SetTableText(m_additionalTable, i, 2, addRows.at(i).at(2));
        SetTableText(m_additionalTable, i, 6, addRows.at(i).at(3));
        // 与商户端默认：游戏显示「不显示」、赠送方式「关闭赠送」、网站不勾选
        SetupAdditionalIntegralRowWidgets(m_additionalTable, i, 2, 0, 0);
    }

    m_integralTable->setRowCount(0);
    m_equipTable->setRowCount(0);
    m_incentiveTable->setRowCount(0);
    if (m_redNpcTable)
        m_redNpcTable->setRowCount(0);
    if (m_redPacketDetailTable)
        m_redPacketDetailTable->setRowCount(0);

    PolishDataTable(m_npcTable);
    PolishDataTable(m_additionalTable);
    if (m_redNpcTable)
        PolishDataTable(m_redNpcTable);
    if (m_redPacketDetailTable)
        PolishDataTable(m_redPacketDetailTable);
    SyncGiftTabEnabledStates();
}

void TemplateDialog::LoadProductsIntoChannelTable()
{
    m_channelTable->setRowCount(0);
    QString err;
    const QJsonArray products = GatewayApiClient().GetProducts(&err);
    int row = 0;
    for (const auto &p : products) {
        if (!p.isObject())
            continue;
        const QJsonObject po = p.toObject();
        const QString name = JsonStr(po, {QStringLiteral("Name"), QStringLiteral("name")});
        const int pid = JsonInt(po, {QStringLiteral("Id"), QStringLiteral("id")}, 0);
        if (pid <= 0)
            continue;
        m_channelTable->insertRow(row);
        SetTableText(m_channelTable, row, 0, name);
        SetTableText(m_channelTable, row, 1, QStringLiteral("0"));
        SetTableText(m_channelTable, row, 2, QString::number(pid));
        SetTableText(m_channelTable, row, 3, QStringLiteral("0"));
        ++row;
    }
    PolishDataTable(m_channelTable);
    SyncGiftTabEnabledStates();
}

void TemplateDialog::LoadTempRatesIntoChannelTable(const QJsonArray &rates)
{
    m_channelTable->setRowCount(0);
    int row = 0;
    for (const auto &r : rates) {
        if (!r.isObject())
            continue;
        const QJsonObject o = r.toObject();
        const QString pname = JsonStr(o, {QStringLiteral("ProductName"), QStringLiteral("productName"), QStringLiteral("Name"), QStringLiteral("name")});
        const int rate = JsonInt(o, {QStringLiteral("Rate"), QStringLiteral("rate")}, 0);
        const int pid = JsonInt(o, {QStringLiteral("ProductId"), QStringLiteral("productId")}, 0);
        const int id = JsonInt(o, {QStringLiteral("Id"), QStringLiteral("id")}, 0);
        m_channelTable->insertRow(row);
        SetTableText(m_channelTable, row, 0, pname);
        SetTableText(m_channelTable, row, 1, QString::number(rate));
        SetTableText(m_channelTable, row, 2, QString::number(pid));
        SetTableText(m_channelTable, row, 3, QString::number(id));
        ++row;
    }
    PolishDataTable(m_channelTable);
    SyncGiftTabEnabledStates();
}

void TemplateDialog::LoadFromJson(const QJsonObject &o)
{
    m_nameEdit->setText(JsonStr(o, {QStringLiteral("Name"), QStringLiteral("name")}));
    m_gameNameEdit->setText(JsonStr(o, {QStringLiteral("GameName"), QStringLiteral("gameName")}));
    const int typeVal = JsonInt(o, {QStringLiteral("Type"), QStringLiteral("type")}, 1);
    for (int i = 0; i < m_typeCombo->count(); ++i) {
        if (m_typeCombo->itemData(i).toInt() == typeVal) {
            m_typeCombo->setCurrentIndex(i);
            break;
        }
    }

    const QString sc = JsonStr(o, {QStringLiteral("ScriptCommand"), QStringLiteral("scriptCommand")});
    m_scriptCommandEdit->setText(sc);
    if (sc.contains(QLatin1String("GAMEGOLD"))) {
        m_currencyCombo->setCurrentIndex(0);
    } else if (sc.contains(QLatin1String("give"))) {
        m_currencyCombo->setCurrentIndex(1);
    } else {
        m_currencyCombo->setCurrentIndex(2);
        m_scriptCommandEdit->setText(sc);
    }

    m_ratioEdit->setText(QString::number(JsonInt(o, {QStringLiteral("Ratio"), QStringLiteral("ratio")}, 100)));
    m_minAmountEdit->setText(QString::number(JsonInt(o, {QStringLiteral("MinAmount"), QStringLiteral("minAmount")}, 1)));
    m_maxAmountEdit->setText(QString::number(JsonInt(o, {QStringLiteral("MaxAmount"), QStringLiteral("maxAmount")}, 100000)));

    const QString engineKey = JsonStr(o, {QStringLiteral("GameEngine"), QStringLiteral("gameEngine")});
    for (int i = 0; i < m_engineCombo->count(); ++i) {
        const QVariantMap m = m_engineCombo->itemData(i).toMap();
        if (m.value(QStringLiteral("name")).toString() == engineKey) {
            m_engineCombo->setCurrentIndex(i);
            break;
        }
    }
    m_browserCommandEdit->setText(JsonStr(o, {QStringLiteral("BrowserCommand"), QStringLiteral("browserCommand")}));
    m_payDirEdit->setText(JsonStr(o, {QStringLiteral("PayDir"), QStringLiteral("payDir")}));
    m_tongQuDirEdit->setText(JsonStr(o, {QStringLiteral("TongQuDir"), QStringLiteral("tongQuDir")}));
    m_dirEdit->setText(JsonStr(o, {QStringLiteral("Dir"), QStringLiteral("dir")}));
    m_isTongQuCheck->setChecked(JsonBool(o, {QStringLiteral("IsTongQu"), QStringLiteral("isTongQu")}));
    if (m_dirModeCombo)
        SetComboCurrentByData(m_dirModeCombo, JsonInt(o, {QStringLiteral("IsDir"), QStringLiteral("isDir")}, 0));
    m_isTestCheck->setChecked(JsonBool(o, {QStringLiteral("IsTest"), QStringLiteral("isTest")}));
    m_isBetchCheck->setChecked(JsonBool(o, {QStringLiteral("IsBetch"), QStringLiteral("isBetch")}));
    m_betchEdit->setText(QString::number(JsonInt(o, {QStringLiteral("Betch"), QStringLiteral("betch")}, 0)));
    m_safetyMoneyEdit->setText(QString::number(JsonInt(o, {QStringLiteral("SafetyMoney"), QStringLiteral("safetyMoney")}, 0)));

    const QString rw = JsonStr(o, {QStringLiteral("RechargeWay"), QStringLiteral("rechargeWay")});
    for (int i = 0; i < m_rechargeWayCombo->count(); ++i) {
        if (m_rechargeWayCombo->itemData(i).toString() == rw) {
            m_rechargeWayCombo->setCurrentIndex(i);
            break;
        }
    }

    const int equipType = JsonInt(o, {QStringLiteral("EquipType"), QStringLiteral("equipType")}, 0);
    for (int i = 0; i < m_equipMethodCombo->count(); ++i) {
        if (m_equipMethodCombo->itemData(i).toInt() == equipType) {
            m_equipMethodCombo->setCurrentIndex(i);
            break;
        }
    }

    m_showAdditionalCheck->setChecked(JsonBool(o, {QStringLiteral("ShowAdditional"), QStringLiteral("showAdditional")}));
    m_showEquipCheck->setChecked(JsonBool(o, {QStringLiteral("ShowEquip"), QStringLiteral("showEquip")}));
    m_isShowCheck->setChecked(JsonBool(o, {QStringLiteral("IsShow"), QStringLiteral("isShow")}));
    m_showIntegralCheck->setChecked(JsonBool(o, {QStringLiteral("ShowIntegral"), QStringLiteral("showIntegral")}));
    m_giveStateCheck->setChecked(JsonBool(o, {QStringLiteral("GiveState"), QStringLiteral("giveState")}));
    m_isContainsCheck->setChecked(JsonBool(o, {QStringLiteral("IsContains"), QStringLiteral("isContains")}));

    if (m_templateColorCombo)
        SetComboCurrentByData(m_templateColorCombo, JsonInt(o, {QStringLiteral("TemplateColor"), QStringLiteral("templateColor")}, 0));
    SelectComboById(m_installModelCombo, JsonInt(o, {QStringLiteral("InstallModel"), QStringLiteral("installModel")}, 0));
    SelectComboById(m_scanModelCombo, JsonInt(o, {QStringLiteral("ScanModel"), QStringLiteral("scanModel")}, 0));
    if (m_isWxmbCheck)
        m_isWxmbCheck->setChecked(JsonInt(o, {QStringLiteral("IsWxmb"), QStringLiteral("isWxmb")}, 0) != 0);
    if (m_isScanCheck)
        m_isScanCheck->setChecked(JsonInt(o, {QStringLiteral("IsScan"), QStringLiteral("isScan")}, 0) != 0);
    if (m_isShowGlodCombo)
        SetComboCurrentByData(m_isShowGlodCombo, JsonInt(o, {QStringLiteral("IsShowGlod"), QStringLiteral("isShowGlod")}, 0));
    if (m_giveOptionCombo)
        SetComboCurrentByData(m_giveOptionCombo, JsonInt(o, {QStringLiteral("GiveOption"), QStringLiteral("giveOption")}, 0));
    if (m_giveOptionStateCombo)
        SetComboCurrentByData(m_giveOptionStateCombo, JsonInt(o, {QStringLiteral("GiveOptionState"), QStringLiteral("giveOptionState")}, 0));
    if (m_giveWayCombo)
        SetComboCurrentByData(m_giveWayCombo, JsonInt(o, {QStringLiteral("GiveWay"), QStringLiteral("giveWay")}, 0));
    SyncWxmbScanWidgets();
    SyncTongQuWidgets();

    m_redPacketStateCheck->setChecked(JsonBool(o, {QStringLiteral("RedPacketState"), QStringLiteral("redPacketState")}));
    m_redPacketAdditionalCheck->setChecked(JsonBool(o, {QStringLiteral("RedPacketAdditional"), QStringLiteral("redPacketAdditional")}));
    m_redPacketEquipCheck->setChecked(JsonBool(o, {QStringLiteral("RedPacketEquip"), QStringLiteral("redPacketEquip")}));
    m_redPacketIntegralCheck->setChecked(JsonBool(o, {QStringLiteral("RedPacketIntegral"), QStringLiteral("redPacketIntegral")}));

    const QJsonArray npcs = JsonArr(o, {QStringLiteral("Npcs"), QStringLiteral("npcs")});
    m_npcTable->setRowCount(0);
    if (m_redNpcTable)
        m_redNpcTable->setRowCount(0);
    for (const auto &n : npcs) {
        if (!n.isObject())
            continue;
        const QJsonObject no = n.toObject();
        const bool isChargeNpc = JsonBool(no, {QStringLiteral("Type"), QStringLiteral("type")}, true);
        QTableWidget *target = isChargeNpc ? m_npcTable : m_redNpcTable;
        if (!target)
            continue;
        const int r = target->rowCount();
        target->insertRow(r);
        SetTableText(target, r, 0, JsonStr(no, {QStringLiteral("Name"), QStringLiteral("name")}));
        SetTableText(target, r, 1, JsonStr(no, {QStringLiteral("Map"), QStringLiteral("map")}));
        SetTableText(target, r, 2, QString::number(JsonInt(no, {QStringLiteral("Looks"), QStringLiteral("looks")}, 0)));
        SetTableText(target, r, 3, QString::number(JsonInt(no, {QStringLiteral("XAxis"), QStringLiteral("xAxis")}, 0)));
        SetTableText(target, r, 4, QString::number(JsonInt(no, {QStringLiteral("YAxis"), QStringLiteral("yAxis")}, 0)));
        SetTableText(target, r, 5, QString::number(JsonInt(no, {QStringLiteral("Id"), QStringLiteral("id")}, 0)));
    }
    if (m_npcTable->rowCount() == 0) {
        m_npcTable->insertRow(0);
        SetTableText(m_npcTable, 0, 0, QStringLiteral("充值使者"));
        SetTableText(m_npcTable, 0, 1, QStringLiteral("3"));
        SetTableText(m_npcTable, 0, 2, QStringLiteral("12"));
        SetTableText(m_npcTable, 0, 3, QStringLiteral("343"));
        SetTableText(m_npcTable, 0, 4, QStringLiteral("383"));
        SetTableText(m_npcTable, 0, 5, QStringLiteral("0"));
    }

    if (m_redPacketDetailTable) {
        m_redPacketDetailTable->setRowCount(0);
        const QJsonArray rpArr = JsonArr(o, {QStringLiteral("RedPacketGives"), QStringLiteral("redPacketGives")});
        for (const auto &item : rpArr) {
            if (!item.isObject())
                continue;
            const QJsonObject ro = item.toObject();
            const int rr = m_redPacketDetailTable->rowCount();
            m_redPacketDetailTable->insertRow(rr);
            SetTableText(m_redPacketDetailTable, rr, 0, QString::number(JsonInt(ro, {QStringLiteral("Amount"), QStringLiteral("amount")}, 0)));
            SetTableText(m_redPacketDetailTable, rr, 1, QString::number(JsonInt(ro, {QStringLiteral("StartAmount"), QStringLiteral("startAmount")}, 0)));
            SetTableText(m_redPacketDetailTable, rr, 2, QString::number(JsonInt(ro, {QStringLiteral("EndAmount"), QStringLiteral("endAmount")}, 0)));
            SetTableText(m_redPacketDetailTable, rr, 3, QString::number(JsonInt(ro, {QStringLiteral("Id"), QStringLiteral("id")}, 0)));
        }
    }

    const QJsonArray adds = JsonArr(o, {QStringLiteral("AdditionalGives"), QStringLiteral("additionalGives")});
    m_additionalTable->setRowCount(0);
    for (const auto &a : adds) {
        if (!a.isObject())
            continue;
        const QJsonObject ao = a.toObject();
        const int r = m_additionalTable->rowCount();
        m_additionalTable->insertRow(r);
        SetTableText(m_additionalTable, r, 0, JsonStr(ao, {QStringLiteral("Name"), QStringLiteral("name")}));
        SetTableText(m_additionalTable, r, 1, JsonStr(ao, {QStringLiteral("Command"), QStringLiteral("command")}));
        SetTableText(m_additionalTable, r, 2, QString::number(JsonInt(ao, {QStringLiteral("Ratio"), QStringLiteral("ratio")}, 1)));
        SetTableText(m_additionalTable, r, 6, QString::number(JsonInt(ao, {QStringLiteral("Id"), QStringLiteral("id")}, 0)));
        const int showV = JsonInt(ao, {QStringLiteral("Show"), QStringLiteral("show")}, 2);
        const int typeV = JsonInt(ao, {QStringLiteral("Type"), QStringLiteral("type")}, 0);
        const int webV = JsonBool(ao, {QStringLiteral("IsShow"), QStringLiteral("isShow")}) ? 1 : 0;
        SetupAdditionalIntegralRowWidgets(m_additionalTable, r, showV, typeV, webV);
    }

    const QJsonArray ints = JsonArr(o, {QStringLiteral("IntegralGives"), QStringLiteral("integralGives")});
    m_integralTable->setRowCount(0);
    for (const auto &a : ints) {
        if (!a.isObject())
            continue;
        const QJsonObject io = a.toObject();
        const int r = m_integralTable->rowCount();
        m_integralTable->insertRow(r);
        SetTableText(m_integralTable, r, 0, JsonStr(io, {QStringLiteral("Name"), QStringLiteral("name")}));
        SetTableText(m_integralTable, r, 1, JsonStr(io, {QStringLiteral("File"), QStringLiteral("file")}));
        SetTableText(m_integralTable, r, 2, QString::number(JsonInt(io, {QStringLiteral("Ratio"), QStringLiteral("ratio")}, 0)));
        SetTableText(m_integralTable, r, 6, QString::number(JsonInt(io, {QStringLiteral("Id"), QStringLiteral("id")}, 0)));
        const int showVi = JsonInt(io, {QStringLiteral("Show"), QStringLiteral("show")}, 2);
        const int typeVi = JsonInt(io, {QStringLiteral("Type"), QStringLiteral("type")}, 0);
        const int webVi = JsonBool(io, {QStringLiteral("IsShow"), QStringLiteral("isShow")}) ? 1 : 0;
        SetupAdditionalIntegralRowWidgets(m_integralTable, r, showVi, typeVi, webVi);
    }

    const QJsonArray eqs = JsonArr(o, {QStringLiteral("EquipGives"), QStringLiteral("equipGives")});
    m_equipTable->setRowCount(0);
    for (const auto &a : eqs) {
        if (!a.isObject())
            continue;
        const QJsonObject eo = a.toObject();
        const int r = m_equipTable->rowCount();
        m_equipTable->insertRow(r);
        SetTableText(m_equipTable, r, 0, QString::number(JsonInt(eo, {QStringLiteral("Amount"), QStringLiteral("amount")}, 0)));
        SetTableText(m_equipTable, r, 1, JsonStr(eo, {QStringLiteral("Command"), QStringLiteral("command")}));
        SetTableText(m_equipTable, r, 2, JsonStr(eo, {QStringLiteral("Name"), QStringLiteral("name")}));
        SetTableText(m_equipTable, r, 3, QString::number(JsonInt(eo, {QStringLiteral("Id"), QStringLiteral("id")}, 0)));
    }

    const QJsonArray incs = JsonArr(o, {QStringLiteral("Incentives"), QStringLiteral("incentives")});
    m_incentiveTable->setRowCount(0);
    for (const auto &a : incs) {
        if (!a.isObject())
            continue;
        const QJsonObject io = a.toObject();
        const int r = m_incentiveTable->rowCount();
        m_incentiveTable->insertRow(r);
        SetTableText(m_incentiveTable, r, 0, QString::number(JsonInt(io, {QStringLiteral("Amount"), QStringLiteral("amount")}, 0)));
        SetTableText(m_incentiveTable, r, 1, QString::number(JsonInt(io, {QStringLiteral("GiveAmount"), QStringLiteral("giveAmount")}, 0)));
        SetTableText(m_incentiveTable, r, 2, QString::number(JsonInt(io, {QStringLiteral("Id"), QStringLiteral("id")}, 0)));
    }

    PolishDataTable(m_npcTable);
    if (m_redNpcTable)
        PolishDataTable(m_redNpcTable);
    PolishDataTable(m_additionalTable);
    PolishDataTable(m_integralTable);
    PolishDataTable(m_equipTable);
    PolishDataTable(m_incentiveTable);
    PolishDataTable(m_channelTable);
    if (m_redPacketDetailTable)
        PolishDataTable(m_redPacketDetailTable);
    SyncGiftTabEnabledStates();
}

QJsonObject TemplateDialog::BuildSubmitJson()
{
    QJsonObject o;
    if (m_templateId > 0) {
        o.insert(QStringLiteral("Id"), m_templateId);
        o.insert(QStringLiteral("ApplicationUserId"),
                 JsonInt(m_sourceTemplate, {QStringLiteral("ApplicationUserId"), QStringLiteral("applicationUserId")}, 0));
    } else {
        o.insert(QStringLiteral("Id"), 0);
        o.insert(QStringLiteral("ApplicationUserId"), 0);
    }

    o.insert(QStringLiteral("Name"), m_nameEdit->text().trimmed());
    o.insert(QStringLiteral("GameName"), m_gameNameEdit->text().trimmed());
    o.insert(QStringLiteral("Type"), m_typeCombo->currentData().toInt());
    o.insert(QStringLiteral("ScriptCommand"), m_scriptCommandEdit->text().trimmed());
    {
        static const QString kCur[] = {QStringLiteral("元宝"), QStringLiteral("金币"), QStringLiteral("自定义")};
        const int ci = qBound(0, m_currencyCombo->currentIndex(), 2);
        o.insert(QStringLiteral("CurrencyName"), kCur[ci]);
    }
    o.insert(QStringLiteral("Ratio"), m_ratioEdit->text().trimmed().toInt());
    o.insert(QStringLiteral("MinAmount"), static_cast<double>(m_minAmountEdit->text().trimmed().toInt()));
    o.insert(QStringLiteral("MaxAmount"), static_cast<double>(m_maxAmountEdit->text().trimmed().toInt()));
    o.insert(QStringLiteral("GameEngine"), m_engineCombo->currentData().toMap().value(QStringLiteral("name")).toString());
    o.insert(QStringLiteral("BrowserCommand"), m_browserCommandEdit->text().trimmed());
    o.insert(QStringLiteral("PayDir"), m_payDirEdit->text().trimmed());
    o.insert(QStringLiteral("TongQuDir"), m_tongQuDirEdit->text().trimmed());
    o.insert(QStringLiteral("Dir"), m_dirEdit->text().trimmed());
    o.insert(QStringLiteral("IsTongQu"), m_isTongQuCheck->isChecked());
    o.insert(QStringLiteral("IsDir"), m_dirModeCombo ? m_dirModeCombo->currentData().toInt() : 0);
    o.insert(QStringLiteral("IsTest"), m_isTestCheck->isChecked());
    o.insert(QStringLiteral("IsBetch"), m_isBetchCheck->isChecked());
    o.insert(QStringLiteral("Betch"), m_betchEdit->text().trimmed().toInt());
    o.insert(QStringLiteral("SafetyMoney"), static_cast<double>(m_safetyMoneyEdit->text().trimmed().toDouble()));
    o.insert(QStringLiteral("RechargeWay"), m_rechargeWayCombo->currentData().toString());
    o.insert(QStringLiteral("EquipType"), m_equipMethodCombo->currentData().toInt());
    o.insert(QStringLiteral("ShowAdditional"), m_showAdditionalCheck->isChecked());
    o.insert(QStringLiteral("ShowEquip"), m_showEquipCheck->isChecked());
    o.insert(QStringLiteral("IsShow"), m_isShowCheck->isChecked());
    o.insert(QStringLiteral("ShowIntegral"), m_showIntegralCheck->isChecked());
    o.insert(QStringLiteral("GiveState"), m_giveStateCheck->isChecked());
    o.insert(QStringLiteral("IsContains"), m_isContainsCheck->isChecked());
    o.insert(QStringLiteral("GiveWay"), m_giveWayCombo ? m_giveWayCombo->currentData().toInt() : 0);
    o.insert(QStringLiteral("FixedAmountGroup"), QString());
    o.insert(QStringLiteral("TemplateColor"), m_templateColorCombo ? m_templateColorCombo->currentData().toInt() : 0);
    o.insert(QStringLiteral("InstallModel"), m_installModelCombo->currentData().toInt());
    o.insert(QStringLiteral("ScanModel"), m_scanModelCombo->currentData().toInt());
    o.insert(QStringLiteral("IsWxmb"), (m_isWxmbCheck && m_isWxmbCheck->isChecked()) ? 1 : 0);
    o.insert(QStringLiteral("IsScan"), (m_isScanCheck && m_isScanCheck->isChecked()) ? 1 : 0);
    o.insert(QStringLiteral("IsShowGlod"), m_isShowGlodCombo ? m_isShowGlodCombo->currentData().toInt() : 0);
    o.insert(QStringLiteral("GiveOption"), m_giveOptionCombo ? m_giveOptionCombo->currentData().toInt() : 0);
    o.insert(QStringLiteral("GiveOptionState"), m_giveOptionStateCombo ? m_giveOptionStateCombo->currentData().toInt() : 0);
    o.insert(QStringLiteral("RedPacketState"), m_redPacketStateCheck->isChecked());
    o.insert(QStringLiteral("RedPacketAdditional"), m_redPacketAdditionalCheck->isChecked());
    o.insert(QStringLiteral("RedPacketEquip"), m_redPacketEquipCheck->isChecked());
    o.insert(QStringLiteral("RedPacketIntegral"), m_redPacketIntegralCheck->isChecked());

    QJsonArray npcArr;
    for (int r = 0; r < m_npcTable->rowCount(); ++r) {
        QJsonObject n;
        n.insert(QStringLiteral("Name"), TableText(m_npcTable, r, 0));
        n.insert(QStringLiteral("Map"), TableText(m_npcTable, r, 1));
        n.insert(QStringLiteral("Looks"), TableText(m_npcTable, r, 2).toInt());
        n.insert(QStringLiteral("XAxis"), TableText(m_npcTable, r, 3).toInt());
        n.insert(QStringLiteral("YAxis"), TableText(m_npcTable, r, 4).toInt());
        n.insert(QStringLiteral("Id"), TableText(m_npcTable, r, 5).toInt());
        n.insert(QStringLiteral("Type"), true);
        if (m_templateId > 0)
            n.insert(QStringLiteral("TemplateId"), m_templateId);
        npcArr.append(n);
    }
    if (m_redNpcTable) {
        for (int r = 0; r < m_redNpcTable->rowCount(); ++r) {
            QJsonObject n;
            n.insert(QStringLiteral("Name"), TableText(m_redNpcTable, r, 0));
            n.insert(QStringLiteral("Map"), TableText(m_redNpcTable, r, 1));
            n.insert(QStringLiteral("Looks"), TableText(m_redNpcTable, r, 2).toInt());
            n.insert(QStringLiteral("XAxis"), TableText(m_redNpcTable, r, 3).toInt());
            n.insert(QStringLiteral("YAxis"), TableText(m_redNpcTable, r, 4).toInt());
            n.insert(QStringLiteral("Id"), TableText(m_redNpcTable, r, 5).toInt());
            n.insert(QStringLiteral("Type"), false);
            if (m_templateId > 0)
                n.insert(QStringLiteral("TemplateId"), m_templateId);
            npcArr.append(n);
        }
    }
    o.insert(QStringLiteral("Npcs"), npcArr);

    QJsonArray addArr;
    for (int r = 0; r < m_additionalTable->rowCount(); ++r) {
        QJsonObject a;
        a.insert(QStringLiteral("Name"), TableText(m_additionalTable, r, 0));
        a.insert(QStringLiteral("Command"), TableText(m_additionalTable, r, 1));
        a.insert(QStringLiteral("Ratio"), TableText(m_additionalTable, r, 2).toInt());
        a.insert(QStringLiteral("Type"), TableComboIntData(m_additionalTable, r, 3, 0));
        a.insert(QStringLiteral("IsShow"), TableCheckBool(m_additionalTable, r, 4));
        a.insert(QStringLiteral("Show"), TableComboIntData(m_additionalTable, r, 5, 0));
        a.insert(QStringLiteral("Id"), TableText(m_additionalTable, r, 6).toInt());
        if (m_templateId > 0)
            a.insert(QStringLiteral("TemplateId"), m_templateId);
        addArr.append(a);
    }
    o.insert(QStringLiteral("AdditionalGives"), addArr);

    QJsonArray intArr;
    for (int r = 0; r < m_integralTable->rowCount(); ++r) {
        QJsonObject a;
        a.insert(QStringLiteral("Name"), TableText(m_integralTable, r, 0));
        a.insert(QStringLiteral("File"), TableText(m_integralTable, r, 1));
        a.insert(QStringLiteral("Ratio"), TableText(m_integralTable, r, 2).toInt());
        a.insert(QStringLiteral("Type"), TableComboIntData(m_integralTable, r, 3, 0));
        a.insert(QStringLiteral("IsShow"), TableCheckBool(m_integralTable, r, 4));
        a.insert(QStringLiteral("Show"), TableComboIntData(m_integralTable, r, 5, 0));
        a.insert(QStringLiteral("Id"), TableText(m_integralTable, r, 6).toInt());
        if (m_templateId > 0)
            a.insert(QStringLiteral("TemplateId"), m_templateId);
        intArr.append(a);
    }
    o.insert(QStringLiteral("IntegralGives"), intArr);

    QJsonArray eqArr;
    for (int r = 0; r < m_equipTable->rowCount(); ++r) {
        QJsonObject a;
        a.insert(QStringLiteral("Amount"), TableText(m_equipTable, r, 0).toInt());
        a.insert(QStringLiteral("Command"), TableText(m_equipTable, r, 1));
        a.insert(QStringLiteral("Name"), TableText(m_equipTable, r, 2));
        a.insert(QStringLiteral("Id"), TableText(m_equipTable, r, 3).toInt());
        if (m_templateId > 0)
            a.insert(QStringLiteral("TemplateId"), m_templateId);
        eqArr.append(a);
    }
    o.insert(QStringLiteral("EquipGives"), eqArr);

    QJsonArray incArr;
    for (int r = 0; r < m_incentiveTable->rowCount(); ++r) {
        QJsonObject a;
        a.insert(QStringLiteral("Amount"), TableText(m_incentiveTable, r, 0).toInt());
        a.insert(QStringLiteral("GiveAmount"), TableText(m_incentiveTable, r, 1).toInt());
        a.insert(QStringLiteral("Id"), TableText(m_incentiveTable, r, 2).toInt());
        if (m_templateId > 0)
            a.insert(QStringLiteral("TemplateId"), m_templateId);
        incArr.append(a);
    }
    o.insert(QStringLiteral("Incentives"), incArr);

    QJsonArray chArr;
    for (int r = 0; r < m_channelTable->rowCount(); ++r) {
        QJsonObject a;
        a.insert(QStringLiteral("Rate"), TableText(m_channelTable, r, 1).toInt());
        a.insert(QStringLiteral("ProductId"), TableText(m_channelTable, r, 2).toInt());
        a.insert(QStringLiteral("Id"), TableText(m_channelTable, r, 3).toInt());
        if (m_templateId > 0)
            a.insert(QStringLiteral("TemplateId"), m_templateId);
        chArr.append(a);
    }
    o.insert(QStringLiteral("TemplateProducts"), chArr);

    QJsonArray redGiveArr;
    if (m_redPacketDetailTable) {
        for (int r = 0; r < m_redPacketDetailTable->rowCount(); ++r) {
            QJsonObject rg;
            rg.insert(QStringLiteral("Amount"), TableText(m_redPacketDetailTable, r, 0).toInt());
            rg.insert(QStringLiteral("StartAmount"), static_cast<double>(TableText(m_redPacketDetailTable, r, 1).toDouble()));
            rg.insert(QStringLiteral("EndAmount"), static_cast<double>(TableText(m_redPacketDetailTable, r, 2).toDouble()));
            rg.insert(QStringLiteral("Id"), TableText(m_redPacketDetailTable, r, 3).toInt());
            if (m_templateId > 0)
                rg.insert(QStringLiteral("TemplateId"), m_templateId);
            redGiveArr.append(rg);
        }
    }
    o.insert(QStringLiteral("RedPacketGives"), redGiveArr);

    const int gct = m_currencyCombo->currentIndex();
    // Entities.GameCurrencyType: Yb=0, GoldCoin=1, CustomDefined=3
    int gctEnum = 3;
    if (gct == 0)
        gctEnum = 0;
    else if (gct == 1)
        gctEnum = 1;
    o.insert(QStringLiteral("GameCurrencyType"), gctEnum);

    return o;
}

void TemplateDialog::SetupAdditionalIntegralRowWidgets(QTableWidget *table, int row, int showVal, int typeVal, int webAsInt)
{
    if (table != m_additionalTable && table != m_integralTable)
        return;
    const int show = (showVal < 0) ? 2 : showVal;
    const int typ = (typeVal < 0) ? 0 : typeVal;
    const bool web = (webAsInt < 0) ? false : (webAsInt != 0);
    for (int c : {3, 4, 5}) {
        if (auto *w = table->cellWidget(row, c)) {
            table->removeCellWidget(row, c);
            w->deleteLater();
        }
        delete table->takeItem(row, c);
    }
    auto *cbType = new TemplateDialogComboBox(table);
    FillAttachGiveTypeCombo(cbType);
    SetComboCurrentByData(cbType, typ);
    cbType->setStyleSheet(QLatin1String(kTableComboStyle));
    AttachAlignedCellWidget(table, row, 3, cbType);

    auto *chk = new QCheckBox(QStringLiteral("网站"), table);
    chk->setChecked(web);
    chk->setStyleSheet(TemplateDialogTableCheckStyle());
    AttachAlignedCellWidget(table, row, 4, chk);

    auto *cbShow = new TemplateDialogComboBox(table);
    FillGameShowCombo(cbShow);
    SetComboCurrentByData(cbShow, show);
    cbShow->setStyleSheet(QLatin1String(kTableComboStyle));
    AttachAlignedCellWidget(table, row, 5, cbShow);

    table->resizeRowToContents(row);
}

void TemplateDialog::SyncWxmbScanWidgets()
{
    if (m_installModelCombo)
        m_installModelCombo->setEnabled(m_isWxmbCheck && m_isWxmbCheck->isChecked());
    if (m_scanModelCombo)
        m_scanModelCombo->setEnabled(m_isScanCheck && m_isScanCheck->isChecked());
}

void TemplateDialog::SyncTongQuWidgets()
{
    const bool tong = m_isTongQuCheck && m_isTongQuCheck->isChecked();
    const bool test = tong && m_isTestCheck && m_isTestCheck->isChecked();

    // 与商户端 partinstallmod：v-if="isTongQu" / v-if="isTongQu && isTest"
    if (m_basicFormLayout) {
        if (m_dirModeCombo)
            SetFormLayoutRowVisible(m_basicFormLayout, m_dirModeCombo, tong);
        if (m_isTestCheck)
            SetFormLayoutRowVisible(m_basicFormLayout, m_isTestCheck, tong);
        if (m_betchEdit)
            SetFormLayoutRowVisible(m_basicFormLayout, m_betchEdit, test);
        if (m_isBetchCheck)
            SetFormLayoutRowVisible(m_basicFormLayout, m_isBetchCheck, test);
        if (m_tongQuDirEdit)
            SetFormLayoutRowVisible(m_basicFormLayout, m_tongQuDirEdit, tong);
        if (m_dirEdit)
            SetFormLayoutRowVisible(m_basicFormLayout, m_dirEdit, tong);
    }

    if (m_dirModeCombo)
        m_dirModeCombo->setEnabled(tong);
    if (m_tongQuDirEdit)
        m_tongQuDirEdit->setEnabled(tong);
    if (m_dirEdit)
        m_dirEdit->setEnabled(tong);
    if (m_isTestCheck)
        m_isTestCheck->setEnabled(tong);
    if (m_isBetchCheck)
        m_isBetchCheck->setEnabled(test);
    if (m_betchEdit)
        m_betchEdit->setEnabled(test);
}

void TemplateDialog::SyncGiftTabEnabledStates()
{
    auto syncBody = [](QWidget *body, bool enabled) {
        if (body)
            body->setEnabled(enabled);
    };
    syncBody(m_equipTabBody, m_showEquipCheck && m_showEquipCheck->isChecked());
    syncBody(m_incentiveTabBody, m_isContainsCheck && m_isContainsCheck->isChecked());
    syncBody(m_channelTabBody, m_giveStateCheck && m_giveStateCheck->isChecked());
    // 红包页需在未勾选「红包赠送」时仍可编辑/添加行；开关仅影响提交的 RedPacketState 等字段
    syncBody(m_redPacketTabBody, true);
}

void TemplateDialog::LoadInstallScanModelOptions()
{
    QString errWx;
    QString errScan;
    const QJsonArray wxArr = GatewayApiClient().GetClientWxmbTemplates(&errWx);
    const QJsonArray scanArr = GatewayApiClient().GetClientScanTemplates(&errScan);
    FillIdNameCombo(m_installModelCombo, wxArr);
    FillIdNameCombo(m_scanModelCombo, scanArr);
    if (!errWx.isEmpty())
        AppLogger::WriteLog(QStringLiteral("加载密保模板列表：%1").arg(errWx));
    if (!errScan.isEmpty())
        AppLogger::WriteLog(QStringLiteral("加载扫码模板列表：%1").arg(errScan));
}

void TemplateDialog::ApplyThemedFieldStyles()
{
    const QString hdr = QStringLiteral(
        "QHeaderView::section { background: #f0dedf; color: #3a2529; border: none; "
        "border-right: 1px solid #e8cfcf; border-bottom: 1px solid #e5c9cc; "
        "padding: 10px 12px; font-weight: normal; font-size: 12px; min-height: 32px; }");
    const QString tbl = QStringLiteral(
        "QTableWidget { border: 1px solid #ead7d7; border-radius: 6px; gridline-color: #ead7d7; background: #ffffff; color: #222222; }"
        "QTableWidget::item { border: none; padding: 8px 12px; }"
        "QTableWidget::item:selected { background: #f0e4e6; color: #3f3135; }");

    for (auto *e : {m_nameEdit,
                    m_gameNameEdit,
                    m_scriptCommandEdit,
                    m_ratioEdit,
                    m_minAmountEdit,
                    m_maxAmountEdit,
                    m_browserCommandEdit,
                    m_payDirEdit,
                    m_tongQuDirEdit,
                    m_dirEdit,
                    m_betchEdit,
                    m_safetyMoneyEdit}) {
        if (e)
            e->setStyleSheet(QLatin1String(kInputFieldStyle));
    }
    for (auto *c : {m_typeCombo,
                    m_currencyCombo,
                    m_engineCombo,
                    m_rechargeWayCombo,
                    m_equipMethodCombo,
                    m_giveWayCombo,
                    m_installModelCombo,
                    m_scanModelCombo,
                    m_dirModeCombo,
                    m_templateColorCombo,
                    m_isShowGlodCombo,
                    m_giveOptionCombo,
                    m_giveOptionStateCombo}) {
        if (c)
            c->setStyleSheet(QLatin1String(kInputFieldStyle));
    }
    for (auto *k : {m_isTongQuCheck,
                    m_isWxmbCheck,
                    m_isScanCheck,
                    m_isTestCheck,
                    m_isBetchCheck,
                    m_showAdditionalCheck,
                    m_showEquipCheck,
                    m_isShowCheck,
                    m_showIntegralCheck,
                    m_giveStateCheck,
                    m_isContainsCheck,
                    m_redPacketStateCheck,
                    m_redPacketAdditionalCheck,
                    m_redPacketEquipCheck,
                    m_redPacketIntegralCheck}) {
        if (k)
            k->setStyleSheet(TemplateDialogMainCheckStyle());
    }
    for (auto *t : {m_npcTable,
                    m_redNpcTable,
                    m_redPacketDetailTable,
                    m_additionalTable,
                    m_integralTable,
                    m_equipTable,
                    m_incentiveTable,
                    m_channelTable}) {
        if (!t)
            continue;
        t->horizontalHeader()->setStyleSheet(hdr);
        t->setStyleSheet(tbl);
    }
}

bool TemplateDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_titleWidget) {
        switch (event->type()) {
        case QEvent::MouseButtonPress: {
            auto *me = static_cast<QMouseEvent *>(event);
            if (me->button() == Qt::LeftButton) {
                m_dragging = true;
                m_dragOffset = UiHelpers::GlobalPosFromMouseEvent(me) - frameGeometry().topLeft();
                return true;
            }
            break;
        }
        case QEvent::MouseMove: {
            if (m_dragging) {
                auto *me = static_cast<QMouseEvent *>(event);
                move(UiHelpers::GlobalPosFromMouseEvent(me) - m_dragOffset);
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

void TemplateDialog::OnConfirm()
{
    if (m_npcTable->rowCount() == 0) {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("请至少保留一行充值 NPC"));
        return;
    }
    for (int r = 0; r < m_npcTable->rowCount(); ++r) {
        if (TableText(m_npcTable, r, 0).isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("提示"),
                                 QStringLiteral("第 %1 行「NPC名称」不能为空").arg(r + 1));
            return;
        }
    }
    if (m_redPacketStateCheck && m_redPacketStateCheck->isChecked() && m_redNpcTable) {
        for (int r = 0; r < m_redNpcTable->rowCount(); ++r) {
            if (TableText(m_redNpcTable, r, 0).isEmpty()) {
                QMessageBox::warning(this, QStringLiteral("提示"),
                                     QStringLiteral("红包 NPC 第 %1 行「NPC名称」不能为空").arg(r + 1));
                return;
            }
        }
    }
    if (m_nameEdit->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("模板名称不能为空"));
        return;
    }
    const int typeVal = m_typeCombo->currentData().toInt();
    if ((typeVal == 1 || typeVal == 2) && m_browserCommandEdit->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("当前模板类型需要填写「浏览器指令」"));
        return;
    }
    if (typeVal == 4 && m_gameNameEdit->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("通用 SQL 模板需要填写「游戏名称」"));
        return;
    }

    const QJsonObject body = BuildSubmitJson();
    GatewayApiClient client;
    QString err;

    if (m_templateId > 0) {
        if (!client.EditClientTemplate(body, &err)) {
            UiHelpers::ShowStyledMessageBox(this, QMessageBox::Warning, QStringLiteral("编辑失败"),
                                            err.isEmpty() ? QStringLiteral("模板编辑失败") : err);
            return;
        }
        UiHelpers::ShowOverlayMessage(this, QMessageBox::Information, QStringLiteral("模板编辑成功"));
        accept();
        return;
    }

    const QString r = client.InstallClientTemplate(body, &err);
    if (!InstallTemplateResponseOk(r)) {
        UiHelpers::ShowStyledMessageBox(this, QMessageBox::Warning, QStringLiteral("添加失败"),
                                        !r.isEmpty() ? r : (err.isEmpty() ? QStringLiteral("未知错误") : err));
        return;
    }
    UiHelpers::ShowOverlayMessage(this, QMessageBox::Information, QStringLiteral("模板添加成功"));
    accept();
}
