#include "partitionpage.h"
#include "ui_partitionpage.h"

#include "groupmanagepage.h"
#include "partitionmanagepage.h"
#include "partitiontemplatepage.h"

#include <QButtonGroup>
#include <QPushButton>

PartitionPage::PartitionPage(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::PartitionPage)
{
    ui->setupUi(this);

    const auto configureSideMenuButton = [](QPushButton *button, bool checked = false) {
        button->setProperty("sideMenu", true);
        button->setCheckable(true);
        button->setChecked(checked);
        button->setFixedHeight(48);
    };

    configureSideMenuButton(ui->partitionManageMenuButton, true);
    configureSideMenuButton(ui->partitionTemplateMenuButton);
    configureSideMenuButton(ui->groupManageMenuButton);

    auto *sideButtonGroup = new QButtonGroup(this);
    sideButtonGroup->setExclusive(true);
    sideButtonGroup->addButton(ui->partitionManageMenuButton, 0);
    sideButtonGroup->addButton(ui->partitionTemplateMenuButton, 1);
    sideButtonGroup->addButton(ui->groupManageMenuButton, 2);

    auto *partitionManagePage = new PartitionManagePage(ui->contentStack);
    auto *partitionTemplatePage = new PartitionTemplatePage(ui->contentStack);
    auto *groupManagePage = new GroupManagePage(ui->contentStack);

    connect(partitionManagePage, &PartitionManagePage::statusMessageRequested, this, &PartitionPage::statusMessageRequested);
    connect(groupManagePage, &GroupManagePage::statusMessageRequested, this, &PartitionPage::statusMessageRequested);

    ui->contentStack->addWidget(partitionManagePage);
    ui->contentStack->addWidget(partitionTemplatePage);
    ui->contentStack->addWidget(groupManagePage);

    connect(sideButtonGroup, &QButtonGroup::idClicked, ui->contentStack, &QStackedWidget::setCurrentIndex);
}

PartitionPage::~PartitionPage()
{
    delete ui;
}
