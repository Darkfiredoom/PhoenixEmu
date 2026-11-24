// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QHash>
#include <QPixmap>
#include <QRectF>
#include <QSizeF>
#include <QStyledItemDelegate>

/**
 * A stylized "card"-like delegate for the game grid view.
 * Adapted from QML
 */
class GameCard : public QStyledItemDelegate {
    Q_OBJECT
public:
    explicit GameCard(QObject* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;

    QRect getCardRect(const QStyleOptionViewItem &option, const QModelIndex &index) const;

    bool hitTest(const QPoint& point, const QModelIndex& index,
                 const QWidget* widget, const QRect& cellRect) const;

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    void setSize(const QSize& newSize, const QSize& contentSize, const int padding, const int columns);

    /// UISettings::game_card_style.
    enum class Style { Plain = 0, Badge = 1, Cartridge = 2, Case = 3 };
    [[nodiscard]] static Style CurrentStyle();

    /// The cartridge frame's proportions and where the game's art goes on it, in the SVG's units
    /// (from Eglantine's home screen redesign).
    static constexpr qreal CartridgeAspect = 232.0 / 324.0;
    /// The grid's icon height: the chosen size, but at least 192 px for cartridges on their
    /// rack (the default 64 px left them too small to read).
    [[nodiscard]] static unsigned GridIconSize();
    static constexpr QSizeF CartridgeBase{232.0, 324.0};
    static constexpr QRectF CartridgeLabel{32.6, 39.5, 166.0, 234.0};

    /// The Switch or Switch 2 badge frame at `size` logical pixels (its middle is clear).
    static QPixmap PlatformBorderPixmap(bool is_switch2, int size, qreal dpr);
    /// The cartridge frame about `height_px` tall, in device pixels.
    /// The cartridge frame: Switch 2's red card, or (is_switch2 false) Switch 1's dark grey one.
    static QPixmap CartridgePixmap(int height_px, qreal dpr, bool is_switch2 = true);
    static QRectF CartridgeLabelRect(const QRectF& cartridge_rect);

    /// The Case look (after PyroCade's game cases): a Switch keep case, its spine on the left, a
    /// "NINTENDO SWITCH" (Switch 1) or "NINTENDO SWITCH 2" header band over the cover art.
    static constexpr qreal CaseAspect = 256.0 / 400.0;
    static constexpr QSizeF CaseBase{256.0, 400.0};
    static constexpr QRectF CaseCover{20.0, 42.0, 228.0, 350.0};
    /// The case about `height_px` tall, in device pixels (the cover area is left for the art).
    /// Switch 2 cases are red plastic, Switch 1 cases clear. band: the generated header band (off
    /// when the player's own case art, which has its header, fills the whole cover).
    static QPixmap CasePixmap(int height_px, qreal dpr, bool is_switch2, bool band = true);
    static QRectF CaseCoverRect(const QRectF& case_rect);
    /// The whole cover behind the case's sleeve (where the player's case art goes).
    static constexpr QRectF CaseWindow{20.0, 8.0, 228.0, 384.0};
    static QRectF CaseWindowRect(const QRectF& case_rect);
    /// With the player's case art: the cover fills the front behind a thin rim, as on a real
    /// case (the spine is its own face, drawn beside it in 3D).
    static constexpr QRectF CaseArtWindow{6.0, 6.0, 244.0, 388.0};
    static QRectF CaseArtWindowRect(const QRectF& case_rect);
    /// The Case look's parts for a game: the player's case art when the folder has it (its
    /// front fills the whole cover), else the generated case with `art` in its cover.
    static void CaseParts(const QModelIndex& index, const QRectF& case_rect, int height_px,
                          qreal dpr, QPixmap& frame, QRectF& art_rect, QPixmap& art);

    /// Cartridge and Case draw a frame with the art inside it; these pick the style's parts.
    [[nodiscard]] static bool IsFramed(Style style) {
        return style == Style::Cartridge || style == Style::Case;
    }
    [[nodiscard]] static qreal FrameAspect(Style style) {
        return style == Style::Case ? CaseAspect : CartridgeAspect;
    }
    static QPixmap FramePixmap(Style style, int height_px, qreal dpr, bool is_switch2) {
        return style == Style::Case ? CasePixmap(height_px, dpr, is_switch2)
                                    : CartridgePixmap(height_px, dpr, is_switch2);
    }
    static QRectF FrameArtRect(Style style, const QRectF& frame) {
        return style == Style::Case ? CaseCoverRect(frame) : CartridgeLabelRect(frame);
    }
    /// The solid part of the frame (the cartridge's SVG has a clear margin around it).
    static QRectF FrameBody(Style style, const QRectF& frame) {
        if (style == Style::Case) {
            return frame;
        }
        const qreal sx = frame.width() / CartridgeBase.width();
        const qreal sy = frame.height() / CartridgeBase.height();
        return {frame.left() + 13.0 * sx, frame.top() + 8.0 * sy, 205.0 * sx, 297.0 * sy};
    }
    /// The frame's corner radius at a width.
    [[nodiscard]] static qreal FrameRadius(Style style, qreal width) {
        return style == Style::Case ? 12.0 * width / CaseBase.width()
                                    : 18.0 * width / CartridgeBase.width();
    }
    /// Draws `pixmap` filling `target`, cropped at the sides or top and bottom to keep its shape.
    static void DrawCoverCropped(QPainter& painter, const QPixmap& pixmap, const QRectF& target);

    /// How far the selected home tile is lifted, 0 to 1 (an easing with overshoot animates it).
    void setLift(qreal lift) {
        m_lift = lift;
    }

private:
    /// The home view (carousel): a big rounded tile, the selected one lifted with a glowing
    /// ember outline and its name under it.
    void PaintHomeTile(QPainter* painter, const QStyleOptionViewItem& option,
                       const QModelIndex& index) const;

    static constexpr int cardMargin = 8;

    qreal m_lift = 1.0;
    /// Home tile pictures scaled to their tile (PaintHomeTile), by picture and size.
    mutable QHash<QString, QPixmap> m_scaled;
    QSize m_size;
    QSize m_contentSize;
    int m_padding;
    int m_columns;
};
