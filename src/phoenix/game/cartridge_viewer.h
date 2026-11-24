// SPDX-FileCopyrightText: 2026 PhoenixEmu contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QPersistentModelIndex>
#include <QPixmap>
#include <QVariantAnimation>
#include <QWidget>

/// A game's cartridge held up close over the game list: drag it (or press Space) to turn it over
/// and see its back, red on Switch 2 game cards and dark grey on Switch 1 ones, scroll to bring
/// it nearer. Play (or Enter) starts the game; Esc or a click beside it puts it back.
class CartridgeViewer : public QWidget {
    Q_OBJECT

public:
    explicit CartridgeViewer(QWidget* parent);

    void Open(const QModelIndex& game);
    void Close();
    [[nodiscard]] bool IsOpen() const {
        return isVisible() && !closing;
    }

signals:
    void PlayRequested(const QModelIndex& game);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    enum class Button { None, Play, Turn, Close };
    struct Buttons {
        QRectF play;
        QRectF turn;
        QRectF close;
    };

    [[nodiscard]] qreal Intro() const;
    [[nodiscard]] QRectF CardRect() const;
    [[nodiscard]] Buttons ButtonRects() const;
    [[nodiscard]] Button ButtonAt(const QPointF& pos) const;
    void PaintButton(QPainter& p, const QRectF& rect, const QString& text, bool primary,
                     bool hover) const;
    void TurnOver();
    void Play();

    QVariantAnimation intro_animation; ///< The cartridge coming forward (0 to 1)
    QVariantAnimation turn_animation;  ///< A half turn (Space, Turn over) or settling after a drag
    qreal intro = 0.0;
    qreal angle = 0.0; ///< Its turn about the upright axis, degrees; 180 shows the back
    qreal zoom = 1.0;
    bool closing = false;

    bool dragging = false;
    bool dragged = false;
    QPointF press_pos;
    qreal press_angle = 0.0;
    Button hover = Button::None;

    QPersistentModelIndex game;
    QPixmap picture; ///< The game's picture, on the label
    QString title;
    bool is_switch2 = true;
};
