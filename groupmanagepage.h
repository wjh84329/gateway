#ifndef GROUPMANAGEPAGE_H
#define GROUPMANAGEPAGE_H

#include <QString>
#include <QWidget>

#include <QPoint>
#include <QTableWidget>

QT_BEGIN_NAMESPACE
class QEvent;
class QModelIndex;
QT_END_NAMESPACE

QT_BEGIN_NAMESPACE
namespace Ui {
class GroupManagePage;
}
QT_END_NAMESPACE

/// 禁止单元格内联编辑（Qt 6 需重写三参数 edit，公有槽 edit(QModelIndex) 不可 override）。
class GroupManageTableWidget : public QTableWidget
{
public:
    explicit GroupManageTableWidget(QWidget *parent = nullptr);

protected:
    bool edit(const QModelIndex &index, EditTrigger trigger, QEvent *event) override;
};

class GroupManagePage : public QWidget
{
    Q_OBJECT

public:
    explicit GroupManagePage(QWidget *parent = nullptr);
    ~GroupManagePage() override;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

signals:
    void statusMessageRequested(const QString &message, int timeout);

private slots:
    void OnAddGroup();
    void OnEditGroup();
    void OnDeleteGroup();
    void OnGroupContextMenu(const QPoint &pos);

private:
    void LoadGroups();
    void DownloadGroupRechargeFile(const QString &groupName, const QString &groupUuid);
    void UpdateGroupActionButtons();
    /// @param row 有效行号，或 -1 表示空白区域
    void ShowGroupTableContextMenu(int row, const QPoint &globalPos);
    /// @return 用户确认且名称非空时 true，并写入 nameOut
    bool RunGroupNameDialog(const QString &title, QString *nameOut, const QString &initialName = QString());

    Ui::GroupManagePage *ui;
};

#endif // GROUPMANAGEPAGE_H
