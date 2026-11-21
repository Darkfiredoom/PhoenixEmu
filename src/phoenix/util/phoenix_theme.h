// SPDX-FileCopyrightText: 2026 PhoenixEmu contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>

#include <QColor>
#include <QRegularExpression>
#include <QString>

#include "common/common_types.h"

/// The Customize menu's choices for the PhoenixEmu theme: an accent colour, a background colour
/// and a button shape. Colours are not repainted one by one: the theme's warm accent colours
/// (ember gold to amber, text included) take the accent's hue, saturation and brightness
/// relative to the base gold, and its warm dark charcoals the background's relative
/// to the base charcoal. Gold on warm charcoal changes nothing. The stylesheet goes through Transform
/// when it loads; the parts PhoenixEmu draws itself (glass, capsules, the home screen's light)
/// through Recolor and Radius, and redraw when Generation changes.
namespace PhoenixTheme {

/// The colours the stylesheet and the painted parts are written in (gold on warm charcoal):
/// Recolor maps from these, and they map to themselves.
inline const QColor BaseAccent{0xFF, 0xC8, 0x3D};
inline const QColor BaseBackground{0x24, 0x1B, 0x12};

/// PhoenixEmu's default look: flame red on a dark ember red. Used when nothing is chosen.
inline const QColor DefaultAccent{0xFF, 0x52, 0x30};
inline const QColor DefaultBackground{0x2E, 0x0C, 0x08};

enum class Shape : u32 { Pill = 0, Soft = 1, Square = 2 };

namespace Detail {
inline QColor accent = DefaultAccent;
inline QColor background = DefaultBackground;
inline Shape shape = Shape::Pill;
inline std::atomic<u32> generation{0};
} // namespace Detail

/// Takes the choices (Customize menu, settings at startup).
inline void Configure(const QColor& accent, const QColor& background, Shape shape) {
    Detail::accent = accent.isValid() ? accent : DefaultAccent;
    Detail::background = background.isValid() ? background : DefaultBackground;
    Detail::shape = shape;
    Detail::generation.fetch_add(1, std::memory_order_relaxed);
}

/// Changes whenever the choices do: cached drawings compare it.
[[nodiscard]] inline u32 Generation() {
    return Detail::generation.load(std::memory_order_relaxed);
}

/// True when the choices are the colours everything is written in: nothing to recolour.
[[nodiscard]] inline bool IsDefault() {
    return Detail::accent == BaseAccent && Detail::background == BaseBackground;
}

/// A colour of the theme as the choices have it.
[[nodiscard]] inline QColor Recolor(const QColor& color) {
    if (IsDefault()) {
        return color;
    }
    float hue = 0.0f;
    float saturation = 0.0f;
    float value = 0.0f;
    float alpha = 0.0f;
    color.getHsvF(&hue, &saturation, &value, &alpha);
    const float degrees = hue < 0.0f ? -1.0f : hue * 360.0f;
    const bool warm = degrees >= 12.0f && degrees <= 68.0f;
    const auto shifted = [&](const QColor& to, const QColor& from) {
        float to_h = 0, to_s = 0, to_v = 0, from_h = 0, from_s = 0, from_v = 0;
        to.getHsvF(&to_h, &to_s, &to_v);
        from.getHsvF(&from_h, &from_s, &from_v);
        if (to_h < 0.0f) {
            to_h = from_h; // a grey: keep the hue, it loses its saturation below
        }
        float new_h = std::fmod(to_h + (hue - from_h) + 1.0f, 1.0f);
        const float new_s = std::clamp(saturation * (to_s / std::max(from_s, 0.01f)), 0.0f, 1.0f);
        const float new_v = std::clamp(value * (to_v / std::max(from_v, 0.01f)), 0.0f, 1.0f);
        return QColor::fromHsvF(new_h, new_s, new_v, alpha);
    };
    if (warm && saturation >= 0.18f && value >= 0.35f) {
        return shifted(Detail::accent, BaseAccent);
    }
    if (warm && saturation >= 0.08f && value < 0.35f) {
        return shifted(Detail::background, BaseBackground);
    }
    return color;
}

/// WCAG relative luminance, 0 (black) to 1 (white).
[[nodiscard]] inline double Luminance(const QColor& color) {
    const auto channel = [](double v) {
        return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * channel(color.redF()) + 0.7152 * channel(color.greenF()) +
           0.0722 * channel(color.blueF());
}

/// WCAG contrast ratio, 1 to 21.
[[nodiscard]] inline double Contrast(const QColor& a, const QColor& b) {
    const double la = Luminance(a);
    const double lb = Luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

/// `text` as drawn over `background`, moved towards white (or black on a light background)
/// until it stands out by `ratio`. An accent-coloured clock on a background of the same hue
/// otherwise fades into it. Transparent text is made more opaque as it moves.
[[nodiscard]] inline QColor Readable(const QColor& text, const QColor& background,
                                     double ratio = 4.5) {
    const auto over = [&](const QColor& c) {
        const qreal a = c.alphaF();
        return QColor::fromRgbF(c.redF() * a + background.redF() * (1 - a),
                                c.greenF() * a + background.greenF() * (1 - a),
                                c.blueF() * a + background.blueF() * (1 - a));
    };
    if (Contrast(over(text), background) >= ratio) {
        return text;
    }
    const bool light_text = Luminance(background) < 0.2;
    const QColor target = light_text ? QColor(Qt::white) : QColor(Qt::black);
    QColor mixed = target;
    for (int step = 1; step <= 10; ++step) {
        const qreal t = step / 10.0;
        mixed = QColor::fromRgbF(text.redF() + (target.redF() - text.redF()) * t,
                                 text.greenF() + (target.greenF() - text.greenF()) * t,
                                 text.blueF() + (target.blueF() - text.blueF()) * t,
                                 text.alphaF() + (1.0 - text.alphaF()) * t);
        if (Contrast(over(mixed), background) >= ratio) {
            break;
        }
    }
    return mixed;
}

/// A corner radius as the button shape has it.
[[nodiscard]] inline qreal Radius(qreal radius) {
    switch (Detail::shape) {
    case Shape::Soft:
        return std::min(radius, qreal{6});
    case Shape::Square:
        return std::min(radius, qreal{2});
    case Shape::Pill:
        break;
    }
    return radius;
}

/// The PhoenixEmu stylesheet with the choices applied: every #rrggbb, rgb() and rgba() colour
/// through Recolor, every border radius through Radius.
[[nodiscard]] inline QString Transform(const QString& stylesheet) {
    if (IsDefault() && Detail::shape == Shape::Pill) {
        return stylesheet;
    }
    QString result;
    result.reserve(stylesheet.size());
    static const QRegularExpression pattern{
        QStringLiteral(R"(#([0-9A-Fa-f]{6})\b|rgba?\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*(?:,\s*([0-9.]+)\s*)?\)|((?:border(?:-top-left|-top-right|-bottom-left|-bottom-right)?-radius)\s*:\s*)(\d+)px)")};
    qsizetype last = 0;
    auto it = pattern.globalMatch(stylesheet);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        result.append(QStringView{stylesheet}.mid(last, match.capturedStart() - last));
        last = match.capturedEnd();
        if (!match.captured(1).isEmpty()) {
            const QColor color = Recolor(QColor(QLatin1Char('#') + match.captured(1)));
            result.append(color.name(QColor::HexRgb));
        } else if (!match.captured(2).isEmpty()) {
            const bool has_alpha = !match.captured(5).isEmpty();
            const QColor color = Recolor(QColor(match.captured(2).toInt(),
                                                match.captured(3).toInt(),
                                                match.captured(4).toInt()));
            if (has_alpha) {
                result.append(QStringLiteral("rgba(%1, %2, %3, %4)")
                                  .arg(color.red())
                                  .arg(color.green())
                                  .arg(color.blue())
                                  .arg(match.captured(5)));
            } else {
                result.append(QStringLiteral("rgb(%1, %2, %3)")
                                  .arg(color.red())
                                  .arg(color.green())
                                  .arg(color.blue()));
            }
        } else {
            const qreal radius = Radius(match.captured(7).toDouble());
            result.append(match.captured(6));
            result.append(QStringLiteral("%1px").arg(qRound(radius)));
        }
    }
    result.append(QStringView{stylesheet}.mid(last));
    return result;
}

} // namespace PhoenixTheme
