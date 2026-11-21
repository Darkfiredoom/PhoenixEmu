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
    enum class Style { Plain = 0, Badge = 1, Cartridge = 2 };
    [[nodiscard]] static Style CurrentStyle();

    /// The cartridge frame's proportions and where the game's art goes on it, in the SVG's units
    /// (from Eglantine's home screen redesign).
    static constexpr qreal CartridgeAspect = 232.0 / 324.0;
    static constexpr QSizeF CartridgeBase{232.0, 324.0};
    static constexpr QRectF CartridgeLabel{32.6, 39.5, 166.0, 234.0};

    /// The Switch or Switch 2 badge frame at `size` logical pixels (its middle is clear).
    static QPixmap PlatformBorderPixmap(bool is_switch2, int size, qreal dpr);
    /// The cartridge frame about `height_px` tall, in device pixels.
    static QPixmap CartridgePixmap(int height_px, qreal dpr);
    static QRectF CartridgeLabelRect(const QRectF& cartridge_rect);
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
