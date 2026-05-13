#pragma once

#include <QString>

/// 全应用 QRadioButton 指示器：未选为描边圆环，选中为描边 + 实心内点（与 Checkbox SVG 主题一致）。
namespace GatewayRadioStyle {

inline QString indicatorRules(int px)
{
    return QStringLiteral(
        "QRadioButton::indicator { width: %1px; height: %1px; min-width: %1px; min-height: %1px; "
        "max-width: %1px; max-height: %1px; border: none; border-radius: 0; background: transparent; margin: 0; padding: 0; }"
        "QRadioButton::indicator:unchecked { image: url(:/radio/radio_unchecked.svg); }"
        "QRadioButton::indicator:checked { image: url(:/radio/radio_checked.svg); }"
        "QRadioButton::indicator:disabled { opacity: 0.45; }")
        .arg(px);
}

} // namespace GatewayRadioStyle
