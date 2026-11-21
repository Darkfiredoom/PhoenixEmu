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

protected:
    /// With cartridge cards: a game card case behind them, a recessed slot for each.
    void paintEvent(QPaintEvent* event) override;

private:
    GameCard* m_gameCard = nullptr;
};
