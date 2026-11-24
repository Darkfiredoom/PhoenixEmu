// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <vector>

#include <QPainter>
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
        // Room for the rack's holder around each cartridge, and the name under it.
        widthMargin = 70;
        heightMargin = UISettings::values.show_game_name ? 78 : 40;
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

/// A moulded plastic part: a fill a little lighter at the top, a highlight along its top edge
/// and a shade along its bottom one.
void Moulded(QPainter& p, const QRectF& rect, qreal radius, const QColor& colour) {
    QLinearGradient fill{rect.topLeft(), rect.bottomLeft()};
    fill.setColorAt(0, colour.lighter(118));
    fill.setColorAt(0.5, colour);
    fill.setColorAt(1, colour.darker(125));
    p.setPen(Qt::NoPen);
    p.setBrush(fill);
    p.drawRoundedRect(rect, radius, radius);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(255, 255, 255, 60), 1));
    p.drawLine(QPointF(rect.left() + radius, rect.top() + 0.5),
               QPointF(rect.right() - radius, rect.top() + 0.5));
    p.setPen(QPen(QColor(0, 0, 0, 70), 1));
    p.drawLine(QPointF(rect.left() + radius, rect.bottom() - 0.5),
               QPointF(rect.right() - radius, rect.bottom() - 0.5));
}

} // Anonymous namespace

void GameGrid::paintEvent(QPaintEvent* event) {
    if (GameCard::CurrentStyle() != GameCard::Style::Cartridge || model() == nullptr) {
        QListView::paintEvent(event);
        return;
    }
    // The cartridges hang on a wall rack, as a game card display does: a moulded board in the
    // theme's colour, and for each game a holder with side arms, a cradle under it and a clip
    // over its top. The holder's back is painted behind the cartridges, its front over them.
    const QRect area = viewport()->rect();
    const QColor accent = PhoenixTheme::Recolor(PhoenixTheme::BaseAccent);
    // Deep enough that a red Switch 2 cartridge still stands out against it.
    const QColor board = accent.darker(230);
    const QColor holder = accent.darker(165);
    const int icon_size = static_cast<int>(GameCard::GridIconSize());
    std::vector<QRectF> carts;
    if (icon_size > 0) {
        QStyleOptionViewItem option;
        option.initFrom(this);
        for (int row = 0; row < model()->rowCount(); ++row) {
            if (isRowHidden(row)) {
                continue;
            }
            const QModelIndex index = model()->index(row, 0);
            option.rect = visualRect(index);
            if (!option.rect.adjusted(-20, -20, 20, 20).intersects(area)) {
                continue;
            }
            carts.push_back(CartridgeIn(m_gameCard->getCardRect(option, index), icon_size));
        }
    }
    {
        QPainter painter{viewport()};
        painter.setRenderHint(QPainter::Antialiasing);

        // The board: lit from above, with the faint grain of printed plastic.
        QLinearGradient plastic{area.topLeft(), area.bottomLeft()};
        plastic.setColorAt(0, board.lighter(112));
        plastic.setColorAt(1, board.darker(135));
        painter.fillRect(area, plastic);
        painter.setPen(QPen(QColor(0, 0, 0, 14), 1));
        for (int y = area.top() + 2; y < area.bottom(); y += 3) {
            painter.drawLine(area.left(), y, area.right(), y);
        }
        // A shadow under the screen's top edge, as if the board were hung below a shelf.
        QLinearGradient top_shade{QPointF(0, area.top()), QPointF(0, area.top() + 24)};
        top_shade.setColorAt(0, QColor(0, 0, 0, 90));
        top_shade.setColorAt(1, QColor(0, 0, 0, 0));
        painter.fillRect(QRect(area.left(), area.top(), area.width(), 24), top_shade);

        const bool named = UISettings::values.show_game_name.GetValue();
        for (const QRectF& cart : carts) {
            const qreal arm = std::max(5.0, cart.width() * 0.07);
            // The holder's back plate, and the recess the cartridge sits in.
            const QRectF plate = cart.adjusted(-arm - 6, -10, arm + 6, named ? 44 : 14);
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(0, 0, 0, 70));
            painter.drawRoundedRect(plate.translated(0, 4), 12, 12); // its drop shadow
            Moulded(painter, plate, 12, holder);
            QLinearGradient recess{cart.topLeft(), cart.bottomLeft()};
            recess.setColorAt(0, QColor(0x08, 0x05, 0x05));
            recess.setColorAt(1, QColor(0x1A, 0x10, 0x0E));
            painter.setPen(Qt::NoPen);
            painter.setBrush(recess);
            painter.drawRoundedRect(cart.adjusted(-4, -4, 4, 4), 10, 10);
            // The cartridge's shadow on the recess's back.
            painter.setBrush(QColor(0, 0, 0, 150));
            painter.drawRoundedRect(cart.adjusted(1, 4, 3, 6), 9, 9);
            // The side arms that keep it from sliding sideways.
            for (const qreal x : {cart.left() - arm - 1, cart.right() + 1}) {
                Moulded(painter, QRectF(x, cart.top() + cart.height() * 0.18, arm,
                                        cart.height() * 0.82),
                        arm / 2, holder.lighter(108));
            }
            // A dark band behind the name, so it reads on any theme colour.
            if (named) {
                const QRectF name_plate{plate.left() + 6, cart.bottom() + 12, plate.width() - 12,
                                        26};
                painter.setPen(Qt::NoPen);
                painter.setBrush(QColor(0, 0, 0, 120));
                painter.drawRoundedRect(name_plate, 7, 7);
            }
        }
        // Mounting screws at the board's corners.
        for (const QPointF at : {QPointF(18, 18), QPointF(area.width() - 18.0, 18),
                                 QPointF(18, area.height() - 18.0),
                                 QPointF(area.width() - 18.0, area.height() - 18.0)}) {
            QRadialGradient head{at, 6};
            head.setColorAt(0, QColor(0xD8, 0xD8, 0xDC));
            head.setColorAt(1, QColor(0x6A, 0x6A, 0x70));
            painter.setPen(QPen(QColor(0, 0, 0, 90), 1));
            painter.setBrush(head);
            painter.drawEllipse(at, 5.5, 5.5);
            painter.setPen(QPen(QColor(0x40, 0x40, 0x46), 1.4));
            painter.drawLine(at + QPointF(-3, 0), at + QPointF(3, 0));
        }
    }
    QListView::paintEvent(event);
    if (carts.empty()) {
        return;
    }
    // The holder's front: the cradle lip over the cartridge's foot and the clip over its top.
    QPainter painter{viewport()};
    painter.setRenderHint(QPainter::Antialiasing);
    for (const QRectF& cart : carts) {
        const qreal arm = std::max(5.0, cart.width() * 0.07);
        const QRectF lip{cart.left() - arm - 2, cart.bottom() - cart.height() * 0.07,
                         cart.width() + 2 * arm + 4, cart.height() * 0.07 + 8};
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0, 60));
        painter.drawRoundedRect(lip.translated(0, 3), 6, 6);
        Moulded(painter, lip, 6, holder.lighter(112));
        // The finger notch in the lip's middle, to lift the cartridge out.
        const QRectF notch{lip.center().x() - cart.width() * 0.16, lip.top() + 2,
                           cart.width() * 0.32, lip.height() * 0.42};
        painter.setPen(Qt::NoPen);
        painter.setBrush(holder.darker(150));
        painter.drawRoundedRect(notch, notch.height() / 2, notch.height() / 2);
        // Two small tabs that hold the cartridge's top corners.
        const qreal tab_w = cart.width() * 0.18;
        for (const qreal x : {cart.left() - 2, cart.right() + 2 - tab_w}) {
            Moulded(painter, QRectF(x, cart.top() - 6, tab_w, 11), 4, holder.lighter(112));
        }
    }
}
