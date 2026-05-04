#ifndef PARTITIONDIALOG_H
#define PARTITIONDIALOG_H

#include <QCheckBox>
#include <QComboBox>
#include <QDateTimeEdit>
#include <QDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QLineEdit>
#include <QPoint>
#include <QPushButton>
#include <QRadioButton>
#include <QTableWidget>
#include <QButtonGroup>
#include <QWidget>

class PartitionDialog : public QDialog
{
    Q_OBJECT

public:
    /// @param partitionObject 编辑时传入分区 JSON 对象，添加时传默认 QJsonObject()
    explicit PartitionDialog(const QJsonObject &partitionObject,
                             const QJsonArray &templates,
                             const QJsonArray &groups,
                             QWidget *parent = nullptr);
    ~PartitionDialog() override;

    /// 返回最终构造的请求 JSON body（用于 InstallClientPartition / UpdateClientPartition）
    QJsonObject GetRequestBody() const;

    /// 是否是编辑模式
    bool IsEditMode() const { return !m_partitionId.isEmpty(); }

    QString GetPartitionId() const { return m_partitionId; }

private slots:
    void OnBrowsePath();
    void OnConfirm();

private:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void SetupUi();
    void PopulateFields();

    // 原始数据
    QJsonObject m_partitionObject;
    QJsonArray  m_templates;
    QJsonArray  m_groups;
    QString     m_partitionId;
    QWidget     *m_titleWidget        = nullptr;
    bool         m_dragging           = false;
    QPoint       m_dragOffset;

    // UI 控件
    QLineEdit      *m_nameEdit          = nullptr;
    QLineEdit      *m_pathEdit          = nullptr;
    QPushButton    *m_browseButton      = nullptr;
    QComboBox      *m_templateCombo     = nullptr;
    QDateTimeEdit  *m_dateTimeEdit      = nullptr;
    QCheckBox      *m_ybEggCheck        = nullptr;
    QTableWidget   *m_groupTable        = nullptr;
    QRadioButton   *m_radioNoUpdate     = nullptr;
    QRadioButton   *m_radioOnlyRecharge = nullptr;
    QRadioButton   *m_radioAllUpdate    = nullptr;
    QButtonGroup   *m_cmdTypeGroup      = nullptr;
    QPushButton    *m_confirmButton     = nullptr;
    QPushButton    *m_cancelButton      = nullptr;
    QLineEdit      *m_serverIpEdit      = nullptr;
    QLineEdit      *m_serverPortEdit    = nullptr;
};

#endif // PARTITIONDIALOG_H
