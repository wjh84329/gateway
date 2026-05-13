#ifndef ORDERPAGE_H
#define ORDERPAGE_H

#include "gatewayapiclient.h"

#include <QHideEvent>
#include <QJsonArray>
#include <QShowEvent>
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

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private slots:
    void onSearchClicked();

private:
    void loadPartitions();
    void requestAutoQueryOnShow();
    void populateTable(const QJsonArray &orders);

    Ui::OrderPage *ui;
    GatewayApiClient m_api;
    bool m_deferredQueryAfterPartitions = false;
};

#endif // ORDERPAGE_H
