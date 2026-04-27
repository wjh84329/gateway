#include "scriptpage.h"
#include "ui_scriptpage.h"

#include <QFrame>
#include <QStringList>

ScriptPage::ScriptPage(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::ScriptPage)
{
    ui->setupUi(this);

    ui->scriptMenuList->setFixedWidth(100);
    ui->scriptMenuList->addItems({
        QStringLiteral("NPC"),
        QStringLiteral("充值"),
        QStringLiteral("附加赠送"),
        QStringLiteral("积分"),
        QStringLiteral("装备")
    });
    for (int i = 0; i < ui->scriptMenuList->count(); ++i) {
        ui->scriptMenuList->item(i)->setTextAlignment(Qt::AlignCenter);
    }
    ui->scriptMenuList->setCurrentRow(1);
    ui->scriptMenuList->setFocusPolicy(Qt::NoFocus);

    ui->engineComboBox->addItems({QStringLiteral("默认引擎")});
    ui->engineComboBox->setFixedWidth(140);
    ui->scriptEditor->setLineWrapMode(QPlainTextEdit::NoWrap);
    ui->scriptEditor->setFrameShape(QFrame::NoFrame);
    ui->saveScriptButton->setProperty("scriptSave", true);
    ui->saveScriptButton->setFixedSize(150, 32);

    const QStringList scriptContents = {
        QStringLiteral("[@main]\n"
                       "#IF\n"
                       "#SAY\n"
                       ";NPC 默认脚本内容\n"
                       "欢迎来到<$SERVERNAME>\\\n"
                       "<进入/@进入>    <离开/@exit>\n\n"
                       "[@进入]\n"
                       "#SAY\n"
                       "祝您游戏愉快。"),
        QStringLiteral("[@main]\n"
                       "#IF\n"
                       "#SAY\n"
                       ";GM请注意, 所列各种脚本内有网关识别项, 请勿修改, 其他内容可按自己NPC样式修改\n"
                       "<$USERNAME>你好, 欢迎来到<$SERVERNAME>, 很高兴为您服务 \\\n"
                       "          \\注意充值事项\\\n"
                       "①本系统支持网上银行、微信扫码、支付宝。\\\n"
                       "②本服充值比例为: 1元=(游戏币)\\\n"
                       "③声明: 请认准本服【官网】或本NPC充值渠道!!! \\ \\\n"
                       "<领取/@领取>          <充值/@充值>          <关闭/@exit>\n\n"
                       "[@领取]\n"
                       "<callget>\n"
                       "[@充值]\n"
                       "#IF\n"
                       "#SAY\n"
                       ";GM请注意, 变量 引擎脚本, 3 后面的3为控制显示前面的空格数, 需要调整NPC整体左右位置, 更改后面的数字即可\n"
                       "<引擎脚本,3>"),
        QStringLiteral("[@main]\n"
                       "#IF\n"
                       "#SAY\n"
                       ";附加赠送脚本\n"
                       "满足条件后可领取附加奖励。\\\n"
                       "<查看奖励/@奖励>\n\n"
                       "[@奖励]\n"
                       "#ACT\n"
                       "give 金币 1000"),
        QStringLiteral("[@main]\n"
                       "#IF\n"
                       "#SAY\n"
                       ";积分脚本\n"
                       "当前可使用积分兑换奖励。\\\n"
                       "<积分兑换/@积分兑换>\n\n"
                       "[@积分兑换]\n"
                       "#ACT\n"
                       "take 积分 100"),
        QStringLiteral("[@main]\n"
                       "#IF\n"
                       "#SAY\n"
                       ";装备脚本\n"
                       "请选择需要领取的装备类型。\\\n"
                       "<武器/@武器>    <防具/@防具>\n\n"
                       "[@武器]\n"
                       "#ACT\n"
                       "give 屠龙 1")
    };

    connect(ui->scriptMenuList, &QListWidget::currentRowChanged, ui->scriptEditor, [this, scriptContents](int row) {
        if (row >= 0 && row < scriptContents.size()) {
            ui->scriptEditor->setPlainText(scriptContents.at(row));
        }
    });
    ui->scriptEditor->setPlainText(scriptContents.at(ui->scriptMenuList->currentRow()));
}

ScriptPage::~ScriptPage()
{
    delete ui;
}
