// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

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
    const u32 icon_size = UISettings::values.game_icon_size.GetValue();

    int heightMargin = 0;
    int widthMargin = 80;

    if (UISettings::values.show_game_name) {
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

void GameGrid::paintEvent(QPaintEvent* event) {
    if (GameCard::CurrentStyle() != GameCard::Style::Cartridge || model() == nullptr) {
        QListView::paintEvent(event);
        return;
    }
    {
        QPainter painter{viewport()};
        painter.setRenderHint(QPainter::Antialiasing);
        const QRect area = viewport()->rect();

        // The case: moulded dark plastic, a little lighter at the top, with fine ribs.
        QLinearGradient plastic{area.topLeft(), area.bottomLeft()};
        plastic.setColorAt(0, QColor(0x34, 0x36, 0x3B));
        plastic.setColorAt(1, QColor(0x1E, 0x1F, 0x23));
        painter.fillRect(area, plastic);
        painter.setPen(QPen(QColor(255, 255, 255, 7), 1));
        for (int y = area.top() + 3; y < area.bottom(); y += 4) {
            painter.drawLine(area.left(), y, area.right(), y);
        }
        // The rim, in the theme's accent, as the case's coloured edge.
        QColor rim = PhoenixTheme::Recolor(QColor(0xFF, 0xC8, 0x3D));
        rim.setAlpha(150);
        painter.setPen(QPen(rim, 3));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(QRectF(area).adjusted(4, 4, -4, -4), 18, 18);

        // A recessed slot under each cartridge in view.
        const int icon_size = static_cast<int>(UISettings::values.game_icon_size.GetValue());
        const int cart_w = qRound(icon_size * GameCard::CartridgeAspect);
        if (icon_size > 0) {
            QStyleOptionViewItem option;
            option.initFrom(this);
            for (int row = 0; row < model()->rowCount(); ++row) {
                if (isRowHidden(row)) {
                    continue;
                }
                const QModelIndex index = model()->index(row, 0);
                option.rect = visualRect(index);
                if (!option.rect.intersects(area)) {
                    continue;
                }
                const QRect card = m_gameCard->getCardRect(option, index);
                const QRectF slot{card.left() + (card.width() - cart_w) / 2.0 - 7.0,
                                  card.top() + 7.0 - 7.0, cart_w + 14.0, icon_size + 14.0};
                QLinearGradient well{slot.topLeft(), slot.bottomLeft()};
                well.setColorAt(0, QColor(0x0C, 0x0D, 0x0F));
                well.setColorAt(0.15, QColor(0x15, 0x16, 0x19));
                well.setColorAt(1, QColor(0x1B, 0x1C, 0x20));
                painter.setPen(Qt::NoPen);
                painter.setBrush(well);
                painter.drawRoundedRect(slot, 14, 14);
                // Light catches the slot's lower lip.
                painter.setPen(QPen(QColor(255, 255, 255, 28), 1));
                painter.setBrush(Qt::NoBrush);
                painter.drawRoundedRect(slot.adjusted(0.5, 1.5, -0.5, 0.5), 14, 14);
                // The little notch a finger uses to lift the cartridge out.
                const QRectF notch{slot.center().x() - 14, slot.bottom() - 6, 28, 10};
                painter.setPen(Qt::NoPen);
                painter.setBrush(QColor(0x0A, 0x0B, 0x0D));
                painter.drawRoundedRect(notch, 5, 5);
            }
        }
    }
    QListView::paintEvent(event);
}
