#ifndef CHANGEGATEWAYIDDIALOG_H
#define CHANGEGATEWAYIDDIALOG_H

#include <QDialog>

QT_BEGIN_NAMESPACE
class QLineEdit;
class QPushButton;
QT_END_NAMESPACE

class ChangeGatewayIdDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ChangeGatewayIdDialog(const QString &currentId, QWidget *parent = nullptr);
    QString getGatewayId() const;

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

private:
    void setupUI();

    QLineEdit *m_idEdit = nullptr;
    QPushButton *m_confirmButton = nullptr;
    QPushButton *m_cancelButton = nullptr;
    QPoint m_dragPosition;
};

#endif // CHANGEGATEWAYIDDIALOG_H
