#ifndef PARTITIONPAGE_H
#define PARTITIONPAGE_H

#include <QString>
#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui {
class PartitionPage;
}
QT_END_NAMESPACE

class PartitionPage : public QWidget
{
    Q_OBJECT

public:
    explicit PartitionPage(QWidget *parent = nullptr);
    ~PartitionPage() override;

signals:
    void statusMessageRequested(const QString &message, int timeout);

private:
    Ui::PartitionPage *ui;
};

#endif // PARTITIONPAGE_H
