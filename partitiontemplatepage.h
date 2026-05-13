#ifndef PARTITIONTEMPLATEPAGE_H
#define PARTITIONTEMPLATEPAGE_H

#include <QJsonArray>
#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui {
class PartitionTemplatePage;
}
QT_END_NAMESPACE

class PartitionTemplatePage : public QWidget
{
    Q_OBJECT

public:
    explicit PartitionTemplatePage(QWidget *parent = nullptr);
    ~PartitionTemplatePage() override;

signals:
    void statusMessageRequested(const QString &message, int timeout);

private slots:
    void OnAddTemplate();
    void OnEditTemplate();
    void OnDeleteTemplate();
    void OnTemplateContextMenu(const QPoint &pos);

private:
    void LoadTemplates();
    void UpdateTemplateActionButtons();
    int SelectedTemplateId() const;

    Ui::PartitionTemplatePage *ui;
    QJsonArray m_templatesCache;
};

#endif // PARTITIONTEMPLATEPAGE_H
