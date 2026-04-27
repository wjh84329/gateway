#ifndef PARTITIONTEMPLATEPAGE_H
#define PARTITIONTEMPLATEPAGE_H

#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui {
class PartitionTemplatePage;
}
QT_END_NAMESPACE

class PartitionTemplatePage : public QWidget
{
    Q_OBJECT

public:
    explicit PartitionTemplatePage(QWidget *parent = nullptr);
    ~PartitionTemplatePage() override;

private:
    void LoadTemplates();

    Ui::PartitionTemplatePage *ui;
};

#endif // PARTITIONTEMPLATEPAGE_H
