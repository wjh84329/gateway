#ifndef ORDERPAGE_H
#define ORDERPAGE_H

#include "gatewayapiclient.h"

#include <QJsonArray>
#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui {
class OrderPage;
}
QT_END_NAMESPACE

class OrderPage : public QWidget
{
    Q_OBJECT

public:
    explicit OrderPage(QWidget *parent = nullptr);
    ~OrderPage() override;

private slots:
    void onSearchClicked();

private:
    void loadPartitions();
    void populateTable(const QJsonArray &orders);

    Ui::OrderPage *ui;
    GatewayApiClient m_api;
};

#endif // ORDERPAGE_H
