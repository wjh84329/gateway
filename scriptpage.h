#ifndef SCRIPTPAGE_H
#define SCRIPTPAGE_H

#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui {
class ScriptPage;
}
QT_END_NAMESPACE

class ScriptPage : public QWidget
{
    Q_OBJECT

public:
    explicit ScriptPage(QWidget *parent = nullptr);
    ~ScriptPage() override;

private:
    Ui::ScriptPage *ui;
};

#endif // SCRIPTPAGE_H
