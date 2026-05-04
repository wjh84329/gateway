#ifndef PARTITIONMANAGEPAGE_H
#define PARTITIONMANAGEPAGE_H

#include <QJsonArray>
#include <QJsonObject>
#include <QMenu>
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

signals:
    void statusMessageRequested(const QString &message, int timeout);

public slots:
    void LoadPartitions();

private slots:
    void OnAddPartition();
    void OnEditPartition();
    void OnDeletePartition();
    void OnLoadPartition();
    void OnContextMenu(const QPoint &pos);

private:
    void LoadTemplatesAndGroups();
    int GetSelectedPartitionId() const;
    QJsonObject GetSelectedPartitionObject() const;

    Ui::PartitionManagePage *ui;

    // 缓存模板和分组数据供对话框使用
    QJsonArray m_templates;
    QJsonArray m_groups;
    // 缓存完整分区列表
    QJsonArray m_partitions;
};

#endif // PARTITIONMANAGEPAGE_H
