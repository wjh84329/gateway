#ifndef SCRIPTPAGE_H
#define SCRIPTPAGE_H

#include <QHash>
#include <QString>
#include <QVector>
#include <QWidget>

class QShowEvent;

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

protected:
    void showEvent(QShowEvent *event) override;

private:
    struct PartitionScriptContext
    {
        int templateId = 0;
        int partitionId = 0;
        QString gameEngine;
        bool isTongQu = false;
    };

    void RefreshPartitionCombo();
    void LoadMergedScriptsForCurrentPartition();
    void ApplyEditorFromCurrentSelection();
    QString CurrentMenuFileName() const;

    Ui::ScriptPage *ui;
    QVector<PartitionScriptContext> m_partitionContexts;
    QHash<QString, QString> m_mergedFiles;
    bool m_loadingScripts = false;
};

#endif // SCRIPTPAGE_H
