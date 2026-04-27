#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QPoint>

QT_BEGIN_NAMESPACE
class QLabel;
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void UpdateGatewayIdDisplay();

    Ui::MainWindow *ui;
    QLabel *m_gatewayIdStatusLabel = nullptr;
    bool m_dragging = false;
    QPoint m_dragPosition;
};
#endif // MAINWINDOW_H
