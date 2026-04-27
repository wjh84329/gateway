#ifndef GROUPMANAGEPAGE_H
#define GROUPMANAGEPAGE_H

#include <QString>
#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui {
class GroupManagePage;
}
QT_END_NAMESPACE

class GroupManagePage : public QWidget
{
    Q_OBJECT

public:
    explicit GroupManagePage(QWidget *parent = nullptr);
    ~GroupManagePage() override;

signals:
    void statusMessageRequested(const QString &message, int timeout);

private:
    void LoadGroups();
    void DownloadGroupRechargeFile(const QString &groupName, const QString &groupUuid);

    Ui::GroupManagePage *ui;
};

#endif // GROUPMANAGEPAGE_H
