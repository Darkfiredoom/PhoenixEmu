// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <vector>

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScroller>
#include <QScrollerProperties>
#include <QStyleOptionViewItem>

#include "qt_common/config/uisettings.h"
#include "qt_common/game_list/model.h"
#include "phoenix/game/common.h"
#include "phoenix/game/game_card.h"
#include "phoenix/game/game_grid.h"
#include "phoenix/util/phoenix_theme.h"

GameGrid::GameGrid(QWidget* parent) : QListView{parent} {
    m_gameCard = new GameCard(this);
    setItemDelegate(m_gameCard);

    setViewMode(QListView::ListMode);
    setResizeMode(QListView::Fixed);
    setUniformItemSizes(true);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);

    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setContextMenuPolicy(Qt::CustomContextMenu);

    setSpacing(10);
    setWordWrap(true);
    setTextElideMode(Qt::ElideRight);
    setFlow(QListView::LeftToRight);
    setWrapping(true);
}

void GameGrid::SetModel(GameListModel* model) {
    QListView::setModel(model);
    UpdateIconSize();
    // The rack's lit slot and its Play button reach past the cell: repaint them all.
    connect(selectionModel(), &QItemSelectionModel::currentChanged, viewport(),
            qOverload<>(&QWidget::update));
}

void GameGrid::ApplyFilter(const QString& edit_filter_text, GameListModel* model) {
    int row_count = model->rowCount();

    for (int i = 0; i < row_count; ++i) {
        QStandardItem* item = model->item(i, 0);
        if (!item)
            continue;

        if (Phoenix::FilterMatches(edit_filter_text, item)) {
            setRowHidden(i, false);
        } else {
            setRowHidden(i, true);
        }
    }
}

void GameGrid::UpdateIconSize() {
    const u32 icon_size = GameCard::GridIconSize();

    int heightMargin = 0;
    int widthMargin = 80;

    if (GameCard::CurrentStyle() == GameCard::Style::Cartridge) {
        // Room for the slot around each cartridge, the name plate and the picked one's Play.
        widthMargin = 70;
        heightMargin = (UISettings::values.show_game_name ? 108 : 70) + GameCard::FramedGridInset;
    } else if (UISettings::values.show_game_name) {
        switch (icon_size) {
        case 128:
            heightMargin = 65;
            break;
        case 0:
            widthMargin = 120;
            heightMargin = 120;
            break;
        case 64:
            heightMargin = 77;
            break;
        case 32:
        case 256:
            heightMargin = 81;
            break;
        }
    } else {
        widthMargin = 24;
        heightMargin = 24;
    }

    const int view_width = viewport()->width();

    const double spacing = 0.01;
    const int min_item_width = icon_size + widthMargin;

    int columns = std::max(1, (view_width - 16) / min_item_width);
    int stretched_width = ((view_width) - (spacing * (columns - 1))) / columns;

    QSize grid_size(stretched_width, icon_size + heightMargin);
    if (gridSize() != grid_size) {
        setUpdatesEnabled(false);

        setGridSize(grid_size);
        m_gameCard->setSize(grid_size, grid_size, stretched_width - min_item_width, columns);

        setUpdatesEnabled(true);
    }
}

QModelIndex GameGrid::indexAt(const QPoint& point) const {
    QModelIndex index = QListView::indexAt(point);
    if (!index.isValid())
        return {};
    if (m_gameCard && !m_gameCard->hitTest(point, index, this, visualRect(index)))
        return {};
    return index;
}

namespace {

/// The cartridge of `index` in its cell, as GameCard::paint places it.
QRectF CartridgeIn(const QRect& card, int icon_size) {
    const int cart_w = qRound(icon_size * GameCard::CartridgeAspect);
    return QRectF(card.left() + (card.width() - cart_w) / 2, card.top() + 8 - 1, cart_w,
                  icon_size);
}

} // Anonymous namespace

void GameGrid::paintEvent(QPaintEvent* event) {
    if (GameCard::CurrentStyle() != GameCard::Style::Cartridge || model() == nullptr) {
        QListView::paintEvent(event);
        return;
    }
    // The cartridges stand in a rack, as on the home Library: a dark panel with two screws,
    // a slot for each game with its name on a plate, the picked one lit with Play under it.
    const QRect area = viewport()->rect();
    const int icon_size = static_cast<int>(GameCard::GridIconSize());
    const bool named = UISettings::values.show_game_name.GetValue();
    const QModelIndex current = currentIndex();
    struct Slot {
        QRectF cart;
        QRectF slot;
        bool picked;
    };
    std::vector<Slot> cart_slots;
    QRectF rack;
    if (icon_size > 0) {
        QStyleOptionViewItem option;
        option.initFrom(this);
        for (int row = 0; row < model()->rowCount(); ++row) {
            if (isRowHidden(row)) {
                continue;
            }
            const QModelIndex index = model()->index(row, 0);
            option.rect = visualRect(index);
            const QRectF cart = CartridgeIn(m_gameCard->getCardRect(option, index), icon_size);
            const qreal side = std::max(5.0, cart.width() * 0.07) + 10;
            const QRectF slot = cart.adjusted(-side, -14, side, named ? 76 : 44);
            rack = rack.isNull() ? slot : rack.united(slot);
            if (slot.adjusted(-20, -20, 20, 20).intersects(area)) {
                cart_slots.push_back({cart, slot, current.isValid() && current.row() == row});
            }
        }
    }
    {
        QPainter painter{viewport()};
        painter.setRenderHint(QPainter::Antialiasing);
        // The ground, warmed by a faint ember glow at the top left.
        painter.fillRect(area, PhoenixTheme::Recolor(QColor(0x0E, 0x0B, 0x09)));
        QRadialGradient glow{QPointF(area.left() + area.width() * 0.1, area.top()), area.width() * 0.5};
        glow.setColorAt(0, QColor(0xFF, 0x5A, 0x1F, 34));
        glow.setColorAt(1, QColor(0xFF, 0x5A, 0x1F, 0));
        painter.fillRect(area, glow);
        if (!rack.isNull()) {
            // The rack round every slot (it scrolls with them), screws at its top corners.
            const QRectF panel = rack.adjusted(-22, -24, 22, 20);
            QLinearGradient fill{panel.topLeft(), panel.bottomLeft()};
            fill.setColorAt(0, QColor(0x2A, 0x20, 0x19));
            fill.setColorAt(1, QColor(0x17, 0x11, 0x0D));
            painter.setPen(QPen(QColor(0x3A, 0x2E, 0x25), 1.2));
            painter.setBrush(fill);
            painter.drawRoundedRect(panel, 22, 22);
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(0xFF, 0xC2, 0x3D, 220));
            painter.drawEllipse(QPointF(panel.left() + 18, panel.top() + 16), 4, 4);
            painter.drawEllipse(QPointF(panel.right() - 18, panel.top() + 16), 4, 4);
        }
        const QColor ember = PhoenixTheme::Recolor(QColor(0xFF, 0x5A, 0x1F));
        for (const Slot& s : cart_slots) {
            if (s.picked) {
                for (int i = 1; i <= 4; ++i) {
                    painter.setPen(QPen(QColor(0xFF, 0x6A, 0x2A, 60 - i * 12), 2));
                    painter.setBrush(Qt::NoBrush);
                    painter.drawRoundedRect(s.slot.adjusted(-i * 1.5, -i * 1.5, i * 1.5, i * 1.5),
                                            16 + i, 16 + i);
                }
            }
            painter.setPen(s.picked ? QPen(ember, 2.5) : QPen(QColor(0x2A, 0x21, 0x1B), 1));
            painter.setBrush(QColor(0x12, 0x0E, 0x0B));
            painter.drawRoundedRect(s.slot, 16, 16);
            // The cartridge's shadow in the slot.
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(0, 0, 0, 120));
            painter.drawRoundedRect(s.cart.adjusted(4, 8, 2, 6), 10, 10);
            if (named) {
                const QRectF plate{s.slot.left() + 10, s.cart.bottom() + 10, s.slot.width() - 20, 26};
                painter.setBrush(QColor(0x2A, 0x21, 0x1B));
                painter.drawRoundedRect(plate, 8, 8);
            }
        }
    }
    QListView::paintEvent(event);
    // Play under the picked cartridge.
    const QRectF play = PlayRect();
    if (!play.isNull() && play.intersects(QRectF(area))) {
        QPainter painter{viewport()};
        painter.setRenderHint(QPainter::Antialiasing);
        QLinearGradient fill{play.topLeft(), play.topRight()};
        fill.setColorAt(0, PhoenixTheme::Recolor(QColor(0xFF, 0x5A, 0x1F)));
        fill.setColorAt(1, PhoenixTheme::Recolor(QColor(0xFF, 0x8A, 0x3D)));
        painter.setPen(Qt::NoPen);
        painter.setBrush(fill);
        painter.drawRoundedRect(play, play.height() / 2, play.height() / 2);
        QPainterPath triangle;
        const QPointF at{play.left() + 14, play.center().y()};
        triangle.moveTo(at + QPointF(-3.5, -5));
        triangle.lineTo(at + QPointF(5, 0));
        triangle.lineTo(at + QPointF(-3.5, 5));
        triangle.closeSubpath();
        painter.setBrush(Qt::white);
        painter.drawPath(triangle);
        QFont font = painter.font();
        font.setPixelSize(12);
        font.setWeight(QFont::Black);
        painter.setFont(font);
        painter.setPen(Qt::white);
        painter.drawText(play.adjusted(14, 0, 0, 0), Qt::AlignCenter, tr("Play"));
    }
}

QRectF GameGrid::PlayRect() const {
    const QModelIndex current = currentIndex();
    const int icon_size = static_cast<int>(GameCard::GridIconSize());
    if (GameCard::CurrentStyle() != GameCard::Style::Cartridge || !current.isValid() ||
        icon_size <= 0 || isRowHidden(current.row())) {
        return {};
    }
    QStyleOptionViewItem option;
    option.initFrom(this);
    option.rect = visualRect(current);
    const QRectF cart = CartridgeIn(m_gameCard->getCardRect(option, current), icon_size);
    const qreal top = cart.bottom() + (UISettings::values.show_game_name.GetValue() ? 42 : 10);
    return {cart.center().x() - 38, top, 76, 26};
}

void GameGrid::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && GameCard::CurrentStyle() == GameCard::Style::Cartridge) {
        const QModelIndex current = currentIndex();
        // Play under the picked cartridge starts it; a click on the picked cartridge turns it
        // over (the home Library does the same).
        if (current.isValid() && PlayRect().contains(event->position())) {
            emit activated(current);
            event->accept();
            return;
        }
        const QModelIndex hit = indexAt(event->position().toPoint());
        if (hit.isValid() && current.isValid() && hit.row() == current.row()) {
            emit CartridgeOpenRequested(current);
            event->accept();
            return;
        }
    }
    QListView::mousePressEvent(event);
}
