#include "groupmanagepage.h"
#include "ui_groupmanagepage.h"

#include "applogger.h"
#include "gatewayapiclient.h"
#include "pageutils.h"

#include <QFileDialog>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonObject>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidgetItem>

namespace {
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

GroupManagePage::GroupManagePage(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::GroupManagePage)
{
    ui->setupUi(this);

    ui->addGroupButton->setProperty("partitionAction", true);
    ui->addGroupButton->setFixedSize(100, 30);
    ui->refreshGroupButton->setProperty("partitionAction", true);
    ui->refreshGroupButton->setFixedSize(100, 30);
    connect(ui->refreshGroupButton, &QPushButton::clicked, this, &GroupManagePage::LoadGroups);

    ui->groupTable->setColumnCount(3);
    ui->groupTable->setHorizontalHeaderLabels({
        QStringLiteral("分组编号"),
        QStringLiteral("分组名称"),
        QStringLiteral("推广下载")
    });
    const auto createDownloadButton = [this](const QString &groupName, const QString &groupUuid) {
        auto *button = new QPushButton(QStringLiteral("推广下载"), this);
        button->setProperty("groupDownload", true);
        connect(button, &QPushButton::clicked, this, [this, groupName, groupUuid] {
            DownloadGroupRechargeFile(groupName, groupUuid);
        });
        return button;
    };
    ui->groupTable->setRowCount(0);

    UiHelpers::ConfigureReadonlyTable(ui->groupTable);
    ui->groupTable->horizontalHeader()->setStretchLastSection(true);
    ui->groupTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    ui->groupTable->setColumnWidth(0, 95);
    ui->groupTable->setColumnWidth(1, 160);
    UiHelpers::CenterTableItems(ui->groupTable);

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

    const auto createDownloadButton = [this](const QString &groupName, const QString &groupUuid) {
        auto *button = new QPushButton(QStringLiteral("推广下载"), this);
        button->setProperty("groupDownload", true);
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

        ui->groupTable->setItem(row, 0, new QTableWidgetItem(groupId));
        ui->groupTable->setItem(row, 1, new QTableWidgetItem(groupName));
        ui->groupTable->setCellWidget(row, 2, createDownloadButton(groupName.isEmpty() ? groupId : groupName, groupUuid));
    }

    UiHelpers::CenterTableItems(ui->groupTable);
    emit statusMessageRequested(QStringLiteral("分组列表已刷新"), 3000);
    ui->refreshGroupButton->setEnabled(true);
    UiHelpers::SetPageLoading(this, false);
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
