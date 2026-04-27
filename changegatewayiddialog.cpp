#include "changegatewayiddialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QGraphicsDropShadowEffect>

ChangeGatewayIdDialog::ChangeGatewayIdDialog(const QString &currentId, QWidget *parent)
    : QDialog(parent)
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::Dialog);
    setModal(true);
    setFixedSize(340, 220);
    setAttribute(Qt::WA_DeleteOnClose);
    setAttribute(Qt::WA_TranslucentBackground);

    // Add shadow effect
    auto shadowEffect = new QGraphicsDropShadowEffect(this);
    shadowEffect->setBlurRadius(20);
    shadowEffect->setColor(QColor(139, 74, 83, 40));
    shadowEffect->setOffset(0, 8);
    setGraphicsEffect(shadowEffect);

    setupUI();
    m_idEdit->setText(currentId);
    m_idEdit->selectAll();
    m_idEdit->setFocus();

    connect(m_confirmButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_idEdit, &QLineEdit::returnPressed, this, &QDialog::accept);
}

void ChangeGatewayIdDialog::setupUI()
{
    auto mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(36, 36, 36, 36);
    mainLayout->setSpacing(14);

    auto titleLabel = new QLabel(QStringLiteral("修改网关标识"), this);
    titleLabel->setAlignment(Qt::AlignCenter);
    titleLabel->setStyleSheet("color: #7a3f49; font-size: 15px; font-weight: 600; background: transparent;");
    mainLayout->addWidget(titleLabel);

    auto promptLabel = new QLabel(QStringLiteral("请输入网关标识:"), this);
    promptLabel->setStyleSheet("color: #9a7b81; font-size: 11px; background: transparent;");
    mainLayout->addWidget(promptLabel);

    m_idEdit = new QLineEdit(this);
    m_idEdit->setFixedHeight(32);
    m_idEdit->setStyleSheet(
        "QLineEdit { "
        "  border: 1px solid #e0e0e0; "
        "  padding: 6px 10px; "
        "  background-color: #fafafa; "
        "  color: #333333; "
        "  font-size: 12px; "
        "  border-radius: 4px; "
        "  selection-background-color: #8b4a53; "
        "} "
        "QLineEdit:focus { "
        "  border: 1px solid #8b4a53; "
        "  background-color: #ffffff; "
        "}"
    );
    mainLayout->addWidget(m_idEdit);

    auto buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(10);
    buttonLayout->addStretch();

    m_confirmButton = new QPushButton(QStringLiteral("确定"), this);
    m_confirmButton->setFixedSize(80, 32);
    m_confirmButton->setStyleSheet(
        "QPushButton { "
        "  color: #ffffff; "
        "  background-color: #8b4a53; "
        "  border: none; "
        "  font-size: 12px; "
        "  font-weight: 500; "
        "  border-radius: 4px; "
        "} "
        "QPushButton:hover { "
        "  background-color: #9a5660; "
        "} "
        "QPushButton:pressed { "
        "  background-color: #7a3d46; "
        "}"
    );
    buttonLayout->addWidget(m_confirmButton);

    m_cancelButton = new QPushButton(QStringLiteral("取消"), this);
    m_cancelButton->setFixedSize(80, 32);
    m_cancelButton->setStyleSheet(
        "QPushButton { "
        "  color: #666666; "
        "  background-color: #f0f0f0; "
        "  border: 1px solid #e0e0e0; "
        "  font-size: 12px; "
        "  font-weight: 500; "
        "  border-radius: 4px; "
        "} "
        "QPushButton:hover { "
        "  background-color: #e8e8e8; "
        "  border: 1px solid #d0d0d0; "
        "} "
        "QPushButton:pressed { "
        "  background-color: #e0e0e0; "
        "}"
    );
    buttonLayout->addWidget(m_cancelButton);

    buttonLayout->addStretch();
    mainLayout->addLayout(buttonLayout);
}

void ChangeGatewayIdDialog::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Draw rounded background with margin for shadow
    QRect drawRect = rect().adjusted(10, 10, -10, -10);
    QPainterPath path;
    path.addRoundedRect(drawRect, 16, 16);
    painter.fillPath(path, QColor(255, 255, 255, 255));

    // Draw border - thicker and more visible
    QPen pen(QColor(139, 74, 83, 80));
    pen.setWidth(2);
    painter.setPen(pen);
    painter.drawPath(path);

    QDialog::paintEvent(event);
}

void ChangeGatewayIdDialog::mousePressEvent(QMouseEvent *event)
{
    if (event->y() < 40) {
        m_dragPosition = event->globalPosition().toPoint() - frameGeometry().topLeft();
        event->accept();
    } else {
        QDialog::mousePressEvent(event);
    }
}

void ChangeGatewayIdDialog::mouseMoveEvent(QMouseEvent *event)
{
    if (event->buttons() == Qt::LeftButton && event->y() < 40) {
        move(event->globalPosition().toPoint() - m_dragPosition);
        event->accept();
    } else {
        QDialog::mouseMoveEvent(event);
    }
}

QString ChangeGatewayIdDialog::getGatewayId() const
{
    return m_idEdit->text().trimmed();
}
