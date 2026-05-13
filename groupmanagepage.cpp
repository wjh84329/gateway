#include "groupmanagepage.h"
#include "ui_groupmanagepage.h"

#include "applogger.h"
#include "gatewayapiclient.h"
#include "pageutils.h"

#include <QAbstractItemView>
#include <QCursor>
#include <QDialog>
#include <QEvent>
#include <QFileDialog>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonObject>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QMenu>
#include <QMouseEvent>
#include <QModelIndex>
#include <QEvent>
#include <QShortcut>
#include <QStyledItemDelegate>
#include <QToolButton>

namespace {

/// 与 GroupManageTableWidget::edit 禁用配合，双保险禁止内联编辑。
class ReadOnlyTableItemDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QWidget *createEditor(QWidget *parent,
                          const QStyleOptionViewItem &option,
                          const QModelIndex &index) const override
    {
        Q_UNUSED(parent);
        Q_UNUSED(option);
        Q_UNUSED(index);
        return nullptr;
    }
};

QString ReadStringField(const QJsonObject &object, const QStringList &names)
{
    for (const auto &name : names) {
        const QString value = object.value(name).toVariant().toString().trimmed();
        if (!value.isEmpty()) {
            return value;
        }
    }
    return {};
}
}

GroupManageTableWidget::GroupManageTableWidget(QWidget *parent)
    : QTableWidget(parent)
{
}

bool GroupManageTableWidget::edit(const QModelIndex &index, EditTrigger trigger, QEvent *event)
{
    Q_UNUSED(index);
    Q_UNUSED(trigger);
    Q_UNUSED(event);
    return false;
}

GroupManagePage::GroupManagePage(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::GroupManagePage)
{
    ui->setupUi(this);

    ui->addGroupButton->setProperty("partitionAction", true);
    ui->addGroupButton->setFixedSize(100, 30);
    ui->editGroupButton->setProperty("partitionAction", true);
    ui->editGroupButton->setFixedSize(100, 30);
    ui->deleteGroupButton->setProperty("partitionAction", true);
    ui->deleteGroupButton->setFixedSize(100, 30);
    ui->refreshGroupButton->setProperty("partitionAction", true);
    ui->refreshGroupButton->setFixedSize(100, 30);
    connect(ui->addGroupButton, &QPushButton::clicked, this, &GroupManagePage::OnAddGroup);
    connect(ui->editGroupButton, &QPushButton::clicked, this, &GroupManagePage::OnEditGroup);
    connect(ui->deleteGroupButton, &QPushButton::clicked, this, &GroupManagePage::OnDeleteGroup);
    connect(ui->refreshGroupButton, &QPushButton::clicked, this, &GroupManagePage::LoadGroups);

    ui->groupTable->setObjectName(QStringLiteral("groupManageTable"));
    ui->groupTable->setColumnCount(3);
    ui->groupTable->setHorizontalHeaderLabels({
        QStringLiteral("分组编号"),
        QStringLiteral("分组名称"),
        QStringLiteral("推广下载")
    });
    ui->groupTable->setRowCount(0);

    UiHelpers::ConfigureReadonlyTable(ui->groupTable);
    ui->groupTable->setItemDelegate(new ReadOnlyTableItemDelegate(ui->groupTable));
    ui->groupTable->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->groupTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->groupTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->groupTable->setFocusPolicy(Qt::StrongFocus);
    if (ui->groupTable->selectionModel())
        connect(ui->groupTable->selectionModel(), &QItemSelectionModel::selectionChanged, this, &GroupManagePage::UpdateGroupActionButtons);
    connect(ui->groupTable, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        ui->groupTable->selectRow(row);
        OnEditGroup();
    });
    ui->groupTable->horizontalHeader()->setStretchLastSection(true);
    ui->groupTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    ui->groupTable->setColumnWidth(0, 95);
    ui->groupTable->setColumnWidth(1, 160);
    UiHelpers::CenterTableItems(ui->groupTable);

    ui->groupTable->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->groupTable, &QWidget::customContextMenuRequested, this, &GroupManagePage::OnGroupContextMenu);

    UpdateGroupActionButtons();
    LoadGroups();
}

GroupManagePage::~GroupManagePage()
{
    delete ui;
}

void GroupManagePage::LoadGroups()
{
    ui->refreshGroupButton->setEnabled(false);
    UiHelpers::SetPageLoading(this, true);

    GatewayApiClient client;
    QString errorMessage;
    const QJsonArray groups = client.GetGroups(&errorMessage);
    ui->groupTable->setRowCount(0);

    if (groups.isEmpty()) {
        if (!errorMessage.isEmpty()) {
            AppLogger::WriteLog(QStringLiteral("加载分组列表失败：%1").arg(errorMessage));
            emit statusMessageRequested(QStringLiteral("加载分组列表失败"), 3000);
        }
        ui->refreshGroupButton->setEnabled(true);
        UiHelpers::SetPageLoading(this, false);
        return;
    }

    const auto createDownloadButton = [this](int tableRow, const QString &groupName, const QString &groupUuid) {
        auto *button = new QPushButton(QStringLiteral("推广下载"), this);
        button->setProperty("groupDownload", true);
        button->setProperty("groupTableRow", tableRow);
        button->setFocusPolicy(Qt::NoFocus);
        button->installEventFilter(this);
        connect(button, &QPushButton::clicked, this, [this, groupName, groupUuid] {
            DownloadGroupRechargeFile(groupName, groupUuid);
        });
        return button;
    };

    ui->groupTable->setRowCount(groups.size());
    for (int row = 0; row < groups.size(); ++row) {
        const QJsonObject groupObject = groups.at(row).toObject();
        const QString groupId = ReadStringField(groupObject, {QStringLiteral("Id"), QStringLiteral("id")});
        const QString groupName = ReadStringField(groupObject, {QStringLiteral("Name"), QStringLiteral("name"), QStringLiteral("GroupName"), QStringLiteral("groupName")});
        const QString groupUuid = ReadStringField(groupObject, {QStringLiteral("Uuid"), QStringLiteral("uuid"), QStringLiteral("GroupUuid"), QStringLiteral("groupUuid")});

        auto *idItem = new QTableWidgetItem(groupId);
        idItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
        ui->groupTable->setItem(row, 0, idItem);
        auto *nameItem = new QTableWidgetItem(groupName);
        nameItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
        ui->groupTable->setItem(row, 1, nameItem);
        ui->groupTable->setCellWidget(row, 2, createDownloadButton(row, groupName.isEmpty() ? groupId : groupName, groupUuid));
    }

    UiHelpers::CenterTableItems(ui->groupTable);
    UpdateGroupActionButtons();
    emit statusMessageRequested(QStringLiteral("分组列表已刷新"), 3000);
    ui->refreshGroupButton->setEnabled(true);
    UiHelpers::SetPageLoading(this, false);
}

void GroupManagePage::UpdateGroupActionButtons()
{
    const auto *sel = ui->groupTable->selectionModel();
    const bool hasRow = sel && sel->hasSelection() && !sel->selectedRows().isEmpty();
    ui->editGroupButton->setEnabled(hasRow);
    ui->deleteGroupButton->setEnabled(hasRow);
}

bool GroupManagePage::RunGroupNameDialog(const QString &title, QString *nameOut, const QString &initialName)
{
    if (!nameOut)
        return false;

    QDialog dlg(this);
    dlg.setWindowFlags(Qt::Window | Qt::Dialog | Qt::FramelessWindowHint);
    dlg.setAttribute(Qt::WA_TranslucentBackground, true);
    dlg.setModal(true);
    dlg.setFixedSize(340, 168);
    if (QWidget *host = window()) {
        const QRect fr = host->frameGeometry();
        dlg.move(fr.center() - QPoint(dlg.width() / 2, dlg.height() / 2));
    }

    auto *outer = new QVBoxLayout(&dlg);
    outer->setContentsMargins(0, 0, 0, 0);

    auto *card = new QWidget(&dlg);
    card->setObjectName(QStringLiteral("groupNameDialogCard"));
    card->setStyleSheet(QStringLiteral(
        "#groupNameDialogCard { background: #ffffff; border: 1px solid #e5d2d5; border-radius: 12px; }"
        "QLabel#groupNameFieldLabel { color: #4a3d3f; font-size: 13px; }"
        "QLineEdit { border: 1px solid #caaeb2; border-radius: 6px; padding: 6px 10px; background: #ffffff; color: #222222; font-size: 13px; }"
        "QLineEdit:focus { border: 1px solid #8b4a53; }"
        "QPushButton[groupDialogPrimary=\"true\"] { color: #ffffff; background: #8b4a53; border: none; border-radius: 6px; "
        "min-width: 80px; min-height: 30px; font-size: 12px; font-weight: normal; }"
        "QPushButton[groupDialogPrimary=\"true\"]:hover { background: #9a5660; }"
        "QPushButton[groupDialogSecondary=\"true\"] { color: #333333; background: #efefef; border: 1px solid #c9c0c1; border-radius: 6px; "
        "min-width: 80px; min-height: 30px; font-size: 12px; font-weight: normal; }"
        "QPushButton[groupDialogSecondary=\"true\"]:hover { background: #f7f3f3; }"
        "QToolButton#groupNameDialogClose { color: #8a6d72; border: none; font-size: 18px; font-weight: normal; padding: 0 4px; background: transparent; }"
        "QToolButton#groupNameDialogClose:hover { color: #8b4a53; }"));

    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(18, 14, 18, 16);
    cardLayout->setSpacing(12);

    auto *titleRow = new QHBoxLayout;
    auto *titleLabel = new QLabel(title, card);
    titleLabel->setStyleSheet(QStringLiteral("font-size: 15px; font-weight: normal; color: #8b4a53; background: transparent; border: none;"));
    auto *closeBtn = new QToolButton(card);
    closeBtn->setObjectName(QStringLiteral("groupNameDialogClose"));
    closeBtn->setText(QStringLiteral("×"));
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setAutoRaise(true);
    titleRow->addWidget(titleLabel);
    titleRow->addStretch();
    titleRow->addWidget(closeBtn, 0, Qt::AlignTop);
    cardLayout->addLayout(titleRow);

    auto *formRow = new QHBoxLayout;
    auto *fieldLabel = new QLabel(QStringLiteral("分组名称"), card);
    fieldLabel->setObjectName(QStringLiteral("groupNameFieldLabel"));
    auto *nameEdit = new QLineEdit(card);
    nameEdit->setText(initialName);
    nameEdit->setPlaceholderText(QStringLiteral("请输入分组名称"));
    formRow->addWidget(fieldLabel);
    formRow->addWidget(nameEdit, 1);
    cardLayout->addLayout(formRow);

    auto *btnRow = new QHBoxLayout;
    btnRow->addStretch();
    auto *okBtn = new QPushButton(QStringLiteral("确定"), card);
    okBtn->setProperty("groupDialogPrimary", true);
    auto *cancelBtn = new QPushButton(QStringLiteral("取消"), card);
    cancelBtn->setProperty("groupDialogSecondary", true);
    okBtn->setDefault(true);
    okBtn->setCursor(Qt::PointingHandCursor);
    cancelBtn->setCursor(Qt::PointingHandCursor);
    btnRow->addWidget(okBtn);
    btnRow->addWidget(cancelBtn);
    cardLayout->addLayout(btnRow);

    outer->addWidget(card);

    QObject::connect(closeBtn, &QToolButton::clicked, &dlg, &QDialog::reject);
    QObject::connect(okBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    QObject::connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);

    auto *escShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), &dlg);
    QObject::connect(escShortcut, &QShortcut::activated, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted)
        return false;

    *nameOut = nameEdit->text().trimmed();
    if (nameOut->isEmpty()) {
        UiHelpers::ShowStyledMessageBox(this, QMessageBox::Warning, title, QStringLiteral("请输入分组名称"));
        return false;
    }
    return true;
}

void GroupManagePage::OnAddGroup()
{
    QString name;
    if (!RunGroupNameDialog(QStringLiteral("添加分组"), &name))
        return;

    UiHelpers::SetPageLoading(this, true, QStringLiteral("提交中"), QStringLiteral("正在添加分组"));
    QString err;
    const bool ok = GatewayApiClient().AddClientGroup(name, &err);
    UiHelpers::SetPageLoading(this, false);

    if (!ok) {
        UiHelpers::ShowStyledMessageBox(this, QMessageBox::Warning, QStringLiteral("添加失败"),
                                        err.isEmpty() ? QStringLiteral("添加分组失败") : err);
        return;
    }
    UiHelpers::ShowOverlayMessage(this, QMessageBox::Information, QStringLiteral("添加成功"));
    emit statusMessageRequested(QStringLiteral("分组已添加"), 3000);
    LoadGroups();
}

void GroupManagePage::OnEditGroup()
{
    if (!ui->groupTable->selectionModel()) {
        UiHelpers::ShowStyledMessageBox(this, QMessageBox::Information, QStringLiteral("提示"), QStringLiteral("请先选择要编辑的分组"));
        return;
    }
    const QList<QModelIndex> rows = ui->groupTable->selectionModel()->selectedRows();
    if (rows.isEmpty()) {
        UiHelpers::ShowStyledMessageBox(this, QMessageBox::Information, QStringLiteral("提示"), QStringLiteral("请先选择要编辑的分组"));
        return;
    }
    const int row = rows.first().row();
    QTableWidgetItem *idItem = ui->groupTable->item(row, 0);
    QTableWidgetItem *nameItem = ui->groupTable->item(row, 1);
    const QString groupId = idItem ? idItem->text().trimmed() : QString();
    const QString oldName = nameItem ? nameItem->text().trimmed() : QString();
    if (groupId.isEmpty()) {
        UiHelpers::ShowStyledMessageBox(this, QMessageBox::Warning, QStringLiteral("编辑分组"), QStringLiteral("无法读取分组编号"));
        return;
    }

    QString name;
    if (!RunGroupNameDialog(QStringLiteral("编辑分组"), &name, oldName))
        return;

    UiHelpers::SetPageLoading(this, true, QStringLiteral("提交中"), QStringLiteral("正在保存分组"));
    QString err;
    const bool ok = GatewayApiClient().EditClientGroup(name, groupId, &err);
    UiHelpers::SetPageLoading(this, false);

    if (!ok) {
        UiHelpers::ShowStyledMessageBox(this, QMessageBox::Warning, QStringLiteral("保存失败"),
                                        err.isEmpty() ? QStringLiteral("编辑分组失败") : err);
        return;
    }
    UiHelpers::ShowOverlayMessage(this, QMessageBox::Information, QStringLiteral("保存成功"));
    emit statusMessageRequested(QStringLiteral("分组已更新"), 3000);
    LoadGroups();
}

void GroupManagePage::OnDeleteGroup()
{
    if (!ui->groupTable->selectionModel()) {
        UiHelpers::ShowStyledMessageBox(this, QMessageBox::Information, QStringLiteral("提示"), QStringLiteral("请先选择要删除的分组"));
        return;
    }
    const QList<QModelIndex> rows = ui->groupTable->selectionModel()->selectedRows();
    if (rows.isEmpty()) {
        UiHelpers::ShowStyledMessageBox(this, QMessageBox::Information, QStringLiteral("提示"), QStringLiteral("请先选择要删除的分组"));
        return;
    }
    const int row = rows.first().row();
    QTableWidgetItem *idItem = ui->groupTable->item(row, 0);
    QTableWidgetItem *nameItem = ui->groupTable->item(row, 1);
    const QString groupId = idItem ? idItem->text().trimmed() : QString();
    const QString groupName = nameItem ? nameItem->text().trimmed() : QString();
    if (groupId.isEmpty()) {
        UiHelpers::ShowStyledMessageBox(this, QMessageBox::Warning, QStringLiteral("删除分组"), QStringLiteral("无法读取分组编号"));
        return;
    }

    const QMessageBox::StandardButton reply = QMessageBox::question(
        this,
        QStringLiteral("确认删除"),
        QStringLiteral("确定要删除分组 \"%1\"（编号 %2）吗？\n若分区仍绑定该分组，服务端可能拒绝删除。")
            .arg(groupName.isEmpty() ? QStringLiteral("(未命名)") : groupName)
            .arg(groupId),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (reply != QMessageBox::Yes)
        return;

    UiHelpers::SetPageLoading(this, true, QStringLiteral("删除中..."), QStringLiteral("正在删除分组"));
    QString err;
    const bool ok = GatewayApiClient().DeleteClientGroup(groupId, &err);
    UiHelpers::SetPageLoading(this, false);

    if (!ok) {
        UiHelpers::ShowStyledMessageBox(this, QMessageBox::Warning, QStringLiteral("删除失败"),
                                        err.isEmpty() ? QStringLiteral("删除分组失败") : err);
        return;
    }
    UiHelpers::ShowOverlayMessage(this, QMessageBox::Information, QStringLiteral("分组已删除"));
    emit statusMessageRequested(QStringLiteral("分组已删除"), 3000);
    LoadGroups();
}

void GroupManagePage::OnGroupContextMenu(const QPoint &pos)
{
    QModelIndex idx = ui->groupTable->indexAt(pos);
    if (!idx.isValid() && ui->groupTable->viewport()) {
        idx = ui->groupTable->indexAt(ui->groupTable->viewport()->mapFrom(ui->groupTable, pos));
    }
    const int row = idx.isValid() ? idx.row() : -1;
    ShowGroupTableContextMenu(row, QCursor::pos());
}

void GroupManagePage::ShowGroupTableContextMenu(int row, const QPoint &globalPos)
{
    QMenu menu(this);
    menu.setStyleSheet(QStringLiteral(
        "QMenu { background: #ffffff; border: 1px solid #e5d2d5; border-radius: 8px; padding: 6px; }"
        "QMenu::item { padding: 8px 24px; border-radius: 4px; color: #4a3d3f; font-size: 13px; }"
        "QMenu::item:selected { background: #f0e4e6; color: #4a3d3f; }"
        "QMenu::separator { height: 1px; background: #e5d2d5; margin: 4px 12px; }"));

    if (row < 0) {
        menu.addAction(QStringLiteral("🔄 刷新列表"), this, &GroupManagePage::LoadGroups);
        menu.addSeparator();
        menu.addAction(QStringLiteral("➕ 添加分组"), this, &GroupManagePage::OnAddGroup);
    } else {
        ui->groupTable->selectRow(row);
        menu.addAction(QStringLiteral("🔄 刷新列表"), this, &GroupManagePage::LoadGroups);
        menu.addSeparator();
        menu.addAction(QStringLiteral("✏️ 编辑分组"), this, &GroupManagePage::OnEditGroup);
        menu.addSeparator();
        menu.addAction(QStringLiteral("🗑️ 删除分组"), this, &GroupManagePage::OnDeleteGroup);
    }
    menu.exec(globalPos);
}

bool GroupManagePage::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonPress) {
        auto *button = qobject_cast<QPushButton *>(watched);
        if (button && button->property("groupTableRow").isValid()) {
            const int row = button->property("groupTableRow").toInt();
            const auto *mouseEvent = static_cast<const QMouseEvent *>(event);
            if (mouseEvent->button() == Qt::LeftButton) {
                ui->groupTable->selectRow(row);
            } else if (mouseEvent->button() == Qt::RightButton) {
                ui->groupTable->selectRow(row);
                ShowGroupTableContextMenu(row, UiHelpers::GlobalPosFromMouseEvent(mouseEvent));
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void GroupManagePage::DownloadGroupRechargeFile(const QString &groupName, const QString &groupUuid)
{
    if (groupUuid.trimmed().isEmpty()) {
        UiHelpers::ShowStyledMessageBox(this, QMessageBox::Warning, QStringLiteral("推广下载"), QStringLiteral("未获取到分组 UUID"));
        emit statusMessageRequested(QStringLiteral("%1缺少分组 UUID").arg(groupName), 3000);
        return;
    }

    GatewayApiClient client;
    UiHelpers::SetPageLoading(this, true, QStringLiteral("下载中..."), QStringLiteral("正在生成推广下载链接"));
    const QString downloadUrl = client.GetRechargeFileDownloadUrl(groupUuid);
    UiHelpers::SetPageLoading(this, false);

    if (downloadUrl.trimmed().isEmpty()) {
        UiHelpers::ShowStyledMessageBox(this, QMessageBox::Warning, QStringLiteral("推广下载"), QStringLiteral("未获取到下载链接"));
        emit statusMessageRequested(QStringLiteral("%1未获取到下载链接").arg(groupName), 3000);
        return;
    }

    const QString saveFilePath = QFileDialog::getSaveFileName(this,
                                                              QStringLiteral("保存推广下载文件"),
                                                              QStringLiteral("payurls.html"),
                                                              QStringLiteral("HTML文件 (*.html);;所有文件 (*.*)"));
    if (saveFilePath.trimmed().isEmpty()) {
        emit statusMessageRequested(QStringLiteral("已取消%1推广下载").arg(groupName), 3000);
        return;
    }

    QString errorMessage;
    UiHelpers::SetPageLoading(this, true, QStringLiteral("下载中..."), QStringLiteral("正在下载推广文件"));
    const bool downloadSucceeded = client.DownloadRechargeFile(downloadUrl, saveFilePath, &errorMessage);
    UiHelpers::SetPageLoading(this, false);

    if (!downloadSucceeded) {
        UiHelpers::ShowOverlayMessage(this,
                                     QMessageBox::Critical,
                                     QStringLiteral("下载失败：%1").arg(errorMessage.isEmpty() ? QStringLiteral("未知错误") : errorMessage));
        emit statusMessageRequested(QStringLiteral("%1推广下载失败").arg(groupName), 3000);
        return;
    }

    UiHelpers::ShowOverlayMessage(this, QMessageBox::Information, QStringLiteral("下载成功"));
    emit statusMessageRequested(QStringLiteral("%1推广下载已保存到 %2").arg(groupName, saveFilePath), 5000);
}
