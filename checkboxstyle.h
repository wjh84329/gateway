#pragma once

#include <QString>

/// 全应用 QCheckBox 指示器：未选为描边方框，选中为描边 + SVG 勾（非整块填色）。
namespace GatewayCheckboxStyle {

inline QString indicatorRules(int px)
{
    return QStringLiteral(
        "QCheckBox::indicator { width: %1px; height: %1px; min-width: %1px; min-height: %1px; "
        "max-width: %1px; max-height: %1px; border: none; border-radius: 0; background: transparent; margin: 0; padding: 0; }"
        "QCheckBox::indicator:unchecked { image: url(:/checkbox/checkbox_unchecked.svg); }"
        "QCheckBox::indicator:checked { image: url(:/checkbox/checkbox_checked.svg); }"
        "QCheckBox::indicator:disabled { opacity: 0.45; }")
        .arg(px);
}

} // namespace GatewayCheckboxStyle
