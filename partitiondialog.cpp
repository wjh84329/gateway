#include "partitiondialog.h"

#include "appconfig.h"
#include "machinecode.h"
#include "pageutils.h"

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
#include <QSet>
#include <QCalendarWidget>

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
}

// 暗红主题样式 - 与主窗口一致
static const char *kButtonPrimaryStyle = R"(
QPushButton {
    color: #ffffff;
    background: #8b4a53;
    border: none;
    border-radius: 4px;
    font-size: 13px;
    font-weight: 600;
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
    font-weight: 600;
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
    setFixedSize(580, 680);
    setModal(true);
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_DeleteOnClose, false);

    SetupUi();

    if (IsEditMode()) {
        PopulateFields();
    } else {
        m_radioNoUpdate->setChecked(true);
        m_dateTimeEdit->setDateTime(QDateTime::currentDateTime());
    }
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
        "color: #ffffff; font-size: 15px; font-weight: 600; background: transparent;"));
    titleLayout->addWidget(titleLabel);
    titleLayout->addStretch();

    auto *closeButton = new QPushButton(QStringLiteral("✕"), m_titleWidget);
    closeButton->setFixedSize(32, 32);
    closeButton->setCursor(Qt::PointingHandCursor);
    closeButton->setStyleSheet(QStringLiteral(
        "QPushButton { color: #ffffff; background: transparent; border: none; border-radius: 16px; font-size: 14px; }"
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
    formWidget->setStyleSheet(QStringLiteral("background: #ffffff;"));
    auto *formLayout = new QVBoxLayout(formWidget);
    formLayout->setContentsMargins(24, 18, 24, 14);

    // 通用小标签样式 - 更现代简洁
    const QString sectionCardStyle = QStringLiteral(
        "background: #fcf7f9;"
        "border: 1px solid #efe4e7;"
        "border-radius: 10px;");
    const QString sectionTitleStyle = QStringLiteral(
        "color: #5a3a41;"
        "font-size: 14px;"
        "font-weight: 700;"
        "background: transparent;");
    const QString sectionHintStyle = QStringLiteral(
        "color: #ad8e95;"
        "font-size: 11px;"
        "background: transparent;");
    const QString labelStyle = QStringLiteral(
        "color: #6a4b52;"
        "font-size: 12px;"
        "font-weight: 600;"
        "background: transparent;"
    );

    auto createSection = [&](const QString &title, const QString &hint) {
        auto *card = new QFrame(formWidget);
        card->setStyleSheet(sectionCardStyle);
        auto *layout = new QVBoxLayout(card);
        layout->setContentsMargins(16, 12, 16, 16);

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

    // 1. 分区名称
    {
        auto section = createSection(QStringLiteral("基础信息"), QStringLiteral("分区名称、模板与开区时间"));
        auto *lbl = new QLabel(QStringLiteral("分区名称"), section.first);
        lbl->setStyleSheet(labelStyle);
        m_nameEdit = new QLineEdit(section.first);
        m_nameEdit->setPlaceholderText(QStringLiteral("请输入分区名称"));
        m_nameEdit->setStyleSheet(QLatin1StringView(kInputFieldStyle));
        section.second->addWidget(lbl);
        section.second->addWidget(m_nameEdit);

        auto *templateLbl = new QLabel(QStringLiteral("使用模板"), section.first);
        templateLbl->setStyleSheet(labelStyle);
        m_templateCombo = new QComboBox(section.first);
        m_templateCombo->setStyleSheet(QLatin1StringView(kInputFieldStyle));
        for (int i = 0; i < m_templates.size(); ++i) {
            const QJsonObject t = m_templates.at(i).toObject();
            const QString display = QStringLiteral("[%1] %2").arg(ReadField(t, {"Id", "id"}), ReadField(t, {"Name", "name"}));
            m_templateCombo->addItem(display);
            m_templateCombo->setItemData(i, ReadField(t, {"Id", "id"}), Qt::UserRole);
        }
        section.second->addWidget(templateLbl);
        section.second->addWidget(m_templateCombo);

        auto *timeLbl = new QLabel(QStringLiteral("开区时间"), section.first);
        timeLbl->setStyleSheet(labelStyle);
        m_dateTimeEdit = new QDateTimeEdit(QDateTime::currentDateTime(), section.first);
        m_dateTimeEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        m_dateTimeEdit->setCalendarPopup(true);
        // 合并输入框与日历弹窗样式，让年月日清晰可见
        m_dateTimeEdit->setStyleSheet(QStringLiteral(
            "QDateTimeEdit { min-height: 38px; border: 1px solid #d9c8cb; border-radius: 8px; padding: 0 12px; font-size: 13px; color: #3f3135; background: #fffdfd; }"
            "QDateTimeEdit:focus { border: 1px solid #a35a66; background: #ffffff; }"
            "QCalendarWidget { background: #ffffff; }"
            "QCalendarWidget QToolButton { color: #4a2d34; font-size: 14px; font-weight: 700; background: #fcf7f8; border: none; padding: 6px; }"
            "QCalendarWidget QToolButton:hover { background: #f4e8ea; }"
            "QCalendarWidget QMenu { background: #ffffff; }"
            "QCalendarWidget QSpinBox { color: #4a2d34; font-size: 13px; font-weight: 600; background: #ffffff; }"
            "QCalendarWidget QWidget#qt_calendar_navigationbar { background: #fcf7f8; }"
        ));
        section.second->addWidget(timeLbl);
        section.second->addWidget(m_dateTimeEdit);

        // 服务器 IP
        auto *serverIpLbl = new QLabel(QStringLiteral("服务器 IP"), section.first);
        serverIpLbl->setStyleSheet(labelStyle);
        m_serverIpEdit = new QLineEdit(section.first);
        m_serverIpEdit->setPlaceholderText(QStringLiteral("如：127.0.0.1"));
        m_serverIpEdit->setStyleSheet(QLatin1StringView(kInputFieldStyle));
        section.second->addWidget(serverIpLbl);
        section.second->addWidget(m_serverIpEdit);

        // 服务器端口
        auto *serverPortLbl = new QLabel(QStringLiteral("服务器端口"), section.first);
        serverPortLbl->setStyleSheet(labelStyle);
        m_serverPortEdit = new QLineEdit(section.first);
        m_serverPortEdit->setPlaceholderText(QStringLiteral("如：7000"));
        m_serverPortEdit->setStyleSheet(QLatin1StringView(kInputFieldStyle));
        section.second->addWidget(serverPortLbl);
        section.second->addWidget(m_serverPortEdit);

        formLayout->addWidget(section.first);
    }

    // 2. 分组选择
    {
        auto section = createSection(QStringLiteral("分组绑定"), QStringLiteral("支持多选，勾选后将同步到该分区"));
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
            "border: 1px solid #e8dbdd;"
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
            "font-weight: 600;"
            "border: none;"
            "border-bottom: 1px solid #ede1e4;"
            "padding: 8px;"
            "}"));
        m_groupTable->setColumnWidth(0, 70);
        m_groupTable->setFixedHeight(150);
        section.second->addWidget(lbl);
        section.second->addWidget(m_groupTable);
        formLayout->addWidget(section.first);
    }

    // 3. 安装路径
    {
        auto section = createSection(QStringLiteral("部署设置"), QStringLiteral("安装路径与脚本更新策略"));
        auto *lbl = new QLabel(QStringLiteral("安装路径"), section.first);
        lbl->setStyleSheet(labelStyle);
        auto *pathLayout = new QHBoxLayout();
        pathLayout->setSpacing(8);
        m_pathEdit = new QLineEdit(section.first);
        m_pathEdit->setPlaceholderText(QStringLiteral("请选择安装路径或手动输入"));
        m_pathEdit->setStyleSheet(QLatin1StringView(kInputFieldStyle));
        m_browseButton = new QPushButton(QStringLiteral("浏览"), section.first);
        m_browseButton->setFixedSize(76, 38);
        m_browseButton->setCursor(Qt::PointingHandCursor);
        m_browseButton->setStyleSheet(kButtonPrimaryStyle);
        connect(m_browseButton, &QPushButton::clicked, this, &PartitionDialog::OnBrowsePath);
        pathLayout->addWidget(m_pathEdit, 1);
        pathLayout->addWidget(m_browseButton);
        section.second->addWidget(lbl);
        section.second->addLayout(pathLayout);

        auto *lblCmd = new QLabel(QStringLiteral("脚本命令"), section.first);
        lblCmd->setStyleSheet(labelStyle);
        lbl->setStyleSheet(labelStyle);
        auto *radioWidget = new QWidget(section.first);
        radioWidget->setStyleSheet(QStringLiteral("background: transparent;"));
        auto *radioLayout = new QHBoxLayout(radioWidget);
        radioLayout->setContentsMargins(0, 0, 0, 0);
        radioLayout->setSpacing(16);

        m_radioNoUpdate = new QRadioButton(QStringLiteral("不更新脚本"), radioWidget);
        m_radioOnlyRecharge = new QRadioButton(QStringLiteral("仅更新充值"), radioWidget);
        m_radioAllUpdate = new QRadioButton(QStringLiteral("全部更新"), radioWidget);

        const QString radioStyle = QStringLiteral(
            "QRadioButton { color: #3f3135; font-size: 13px; font-weight: 600; spacing: 8px; }"
            "QRadioButton:checked { color: #8b4a53; }"
            "QRadioButton::indicator { width: 18px; height: 18px; border-radius: 9px; border: 2px solid #c0aaaa; background: #ffffff; }"
            "QRadioButton::indicator:checked { border: 2px solid #8b4a53; background: #8b4a53; }");
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

        section.second->addWidget(lblCmd);
        section.second->addWidget(radioWidget);

        m_ybEggCheck = new QCheckBox(QStringLiteral("开启元宝蛋"), section.first);
        m_ybEggCheck->setStyleSheet(QStringLiteral(
            "QCheckBox { color: #3f3135; font-size: 13px; font-weight: 600; spacing: 8px; }"
            "QCheckBox:checked { color: #8b4a53; }"
            "QCheckBox::indicator { width: 18px; height: 18px; border-radius: 4px; border: 2px solid #c0aaaa; background: #ffffff; }"
            "QCheckBox::indicator:checked { background: #8b4a53; border-color: #8b4a53; }"
            "QCheckBox::indicator:hover { border-color: #a35a66; }"));
        section.second->addWidget(m_ybEggCheck);
        formLayout->addWidget(section.first);
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
                m_dragOffset = mouseEvent->globalPosition().toPoint() - frameGeometry().topLeft();
                return true;
            }
            break;
        }
        case QEvent::MouseMove: {
            if (m_dragging) {
                auto *mouseEvent = static_cast<QMouseEvent *>(event);
                move(mouseEvent->globalPosition().toPoint() - m_dragOffset);
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

void PartitionDialog::PopulateFields()
{
    // 名称
    m_nameEdit->setText(ReadField(m_partitionObject, {"Name", "name", "PartitionName"}));

    // 路径 - 归一化为 Windows 右斜杠
    QString rawPath = ReadField(m_partitionObject, {"ScriptPath", "scriptPath", "PartitionPath", "partitionPath"});
    rawPath.replace(QLatin1Char('/'), QLatin1Char('\\'));
    m_pathEdit->setText(rawPath);

    // 模板
    const QString templateId = ReadField(m_partitionObject, {"TemplateId", "templateId"});
    for (int i = 0; i < m_templateCombo->count(); ++i) {
        if (m_templateCombo->itemData(i, Qt::UserRole).toString() == templateId) {
            m_templateCombo->setCurrentIndex(i);
            break;
        }
    }

    // 服务器 IP
    m_serverIpEdit->setText(ReadField(m_partitionObject, {"ServerIp", "serverIp", "ServerIP", "ServerAddress"}));

    // 服务器端口
    const int portVal = ReadIntField(m_partitionObject, {"ServerPort", "serverPort", "ServerPORT", "Port", "ServerAddressPort"}, 0);
    if (portVal > 0)
        m_serverPortEdit->setText(QString::number(portVal));

    // 开区时间
    const QString useDate = ReadField(m_partitionObject, {"UseDate", "useDate"});
    if (!useDate.isEmpty()) {
        QDateTime dt = QDateTime::fromString(useDate, Qt::ISODate);
        if (dt.isValid())
            m_dateTimeEdit->setDateTime(dt);
    }

    // 脚本类型
    int cmdType = ReadIntField(m_partitionObject, {"PartitionCmdType", "partitionCmdType"}, 2);
    auto *btn = m_cmdTypeGroup->button(cmdType);
    if (btn) btn->setChecked(true);
    else m_radioAllUpdate->setChecked(true);

    // 元宝蛋
    m_ybEggCheck->setChecked(m_partitionObject.value(QStringLiteral("YbEgg")).toBool()
                             || m_partitionObject.value(QStringLiteral("ybEgg")).toBool());

    // 分组（兼容对象数组 [{"Id":1,...}] 或纯数字数组 [1,2,3] 两种格式）
    QSet<QString> selectedGroupIds;
    const QJsonValue groupsVal = m_partitionObject.value(QStringLiteral("Groups"));
    if (groupsVal.isArray()) {
        const QJsonArray groupsArr = groupsVal.toArray();
        for (int j = 0; j < groupsArr.size(); ++j) {
            if (groupsArr.at(j).isObject()) {
                const QJsonObject sg = groupsArr.at(j).toObject();
                const QString gid = ReadField(sg, {"GroupId", "groupId", "Id", "id"});
                if (!gid.isEmpty())
                    selectedGroupIds.insert(gid);
            } else {
                // 纯数字 ID
                selectedGroupIds.insert(groupsArr.at(j).toVariant().toString());
            }
        }
    }

    for (int i = 0; i < m_groupTable->rowCount(); ++i) {
        const QString rowId = m_groupTable->item(i, 0)->text();
        if (selectedGroupIds.contains(rowId))
            m_groupTable->item(i, 0)->setCheckState(Qt::Checked);
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
    if (m_pathEdit->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("安装路径不能为空"));
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
        int id = m_partitionId.toInt(&ok);
        if (ok) body.insert(QStringLiteral("Id"), id);
    }

    body.insert(QStringLiteral("Name"), m_nameEdit->text().trimmed());
    body.insert(QStringLiteral("ScriptPath"), m_pathEdit->text().trimmed());

    const int templateIndex = m_templateCombo->currentIndex();
    if (templateIndex >= 0) {
        bool ok = false;
        int tId = m_templateCombo->itemData(templateIndex, Qt::UserRole).toInt(&ok);
        if (ok) body.insert(QStringLiteral("TemplateId"), tId);
    }

    body.insert(QStringLiteral("UseDate"), m_dateTimeEdit->dateTime().toString(Qt::ISODate));

    // 服务器 IP 和端口
    const QString serverIp = m_serverIpEdit->text().trimmed();
    if (!serverIp.isEmpty())
        body.insert(QStringLiteral("ServerIp"), serverIp);
    const QString serverPort = m_serverPortEdit->text().trimmed();
    if (!serverPort.isEmpty()) {
        bool ok = false;
        int port = serverPort.toInt(&ok);
        if (ok && port > 0 && port <= 65535)
            body.insert(QStringLiteral("ServerPort"), port);
    }

    int cmdType = m_cmdTypeGroup->checkedId();
    if (cmdType < 0) cmdType = 2;
    body.insert(QStringLiteral("PartitionCmdType"), cmdType);
    body.insert(QStringLiteral("YbEgg"), m_ybEggCheck->isChecked());

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

    {
        MachineCode machineCode;
        body.insert(QStringLiteral("MachineCode"), machineCode.GetRNum().trimmed());
        body.insert(QStringLiteral("Type"), 1);
    }

    if (!IsEditMode()) {
        body.insert(QStringLiteral("IsCreate"), true);
        body.insert(QStringLiteral("IsChangeInTime"), false);
    }

    return body;
}
