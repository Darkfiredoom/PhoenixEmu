// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QListView>
#include <QString>

class GameCard;
class GameListModel;

class GameGrid : public QListView {
    Q_OBJECT

public:
    explicit GameGrid(QWidget* parent = nullptr);

    void SetModel(GameListModel* model);
    void ApplyFilter(const QString& edit_filter_text, GameListModel* model);
    void UpdateIconSize();

    QModelIndex indexAt(const QPoint& point) const override;

signals:
    /// The Cartridge look: the picked cartridge was clicked (turned over, CartridgeViewer).
    void CartridgeOpenRequested(const QModelIndex& game);

protected:
    /// With cartridge cards: a rack behind them (as the home Library's), a slot for each with
    /// the name on a plate, and a Play button under the picked one.
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    /// The Play button under the picked cartridge, in the viewport.
    [[nodiscard]] QRectF PlayRect() const;
    GameCard* m_gameCard = nullptr;
};
