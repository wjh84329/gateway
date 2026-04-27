#ifndef PARTITIONMANAGEPAGE_H
#define PARTITIONMANAGEPAGE_H

#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui {
class PartitionManagePage;
}
QT_END_NAMESPACE

class PartitionManagePage : public QWidget
{
    Q_OBJECT

public:
    explicit PartitionManagePage(QWidget *parent = nullptr);
    ~PartitionManagePage() override;

private:
    void LoadPartitions();

    Ui::PartitionManagePage *ui;
};

#endif // PARTITIONMANAGEPAGE_H
