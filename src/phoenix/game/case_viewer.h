// SPDX-FileCopyrightText: 2026 PhoenixEmu contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <memory>

#include <QPersistentModelIndex>
#include <QPixmap>
#include <QVariantAnimation>
#include <QWidget>

#include "common/common_types.h"
#include "qt_common/game_list/case_art.h"

/// A game's case, opened (after PyroCade): over the game list the case turns, its cover swings
/// open around the spine and shows the inside of the cover through the plastic (red on Switch 2,
/// clear on Switch 1) and the cartridge in its holder, where the real cases hold it. Clicking the
/// cartridge starts the game; Esc or a click beside the case closes it.
class CaseViewer : public QWidget {
    Q_OBJECT

public:
    explicit CaseViewer(QWidget* parent);

    /// Shows the game's case and opens it.
    void Open(const QModelIndex& game);
    /// Closes the case (animated), then hides.
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
    void keyPressEvent(QKeyEvent* event) override;

private:
    struct Layout {
        QRectF tray;    ///< The case's back half (where the cartridge is), at the hinge
        QRectF spine;   ///< The spine between the halves, when open
        qreal hinge_x;  ///< Where the cover turns
        qreal angle;    ///< The cover's turn, 0 (closed) to 180 (open flat)
        QRectF cartridge;
    };
    [[nodiscard]] Layout ComputeLayout() const;
    /// The case coming forward (0 to 1), then the cover opening (0 to 1), from progress.
    [[nodiscard]] qreal Intro() const;
    [[nodiscard]] qreal Opening() const;
    /// One inner half of the open case: plastic, the inside art's half seen through it.
    void PaintInside(QPainter& p, const QRectF& panel, bool left_half) const;
    void PaintCoverFront(QPainter& p, const QRectF& panel) const;
    void PaintTrayParts(QPainter& p, const Layout& layout) const;
    void Play();

    /// Inspect: the case in front of you, turned round by dragging; Turning: it turns back to
    /// face you; Opening: its cover swings open (and the cartridge can be clicked).
    enum class Stage { Inspect, Turning, Opening };
    Stage stage = Stage::Inspect;
    QVariantAnimation intro_animation; ///< The case coming forward (intro 0 to 1)
    QVariantAnimation turn_animation;  ///< Back to facing the eye before it opens
    qreal intro = 0.0;
    qreal inspect_angle = 0.0;
    bool dragging = false;
    bool dragged = false;
    QPointF press_pos;
    qreal press_angle = 0.0;
    [[nodiscard]] QRectF ClosedCaseRect() const;
    void StartOpening();

    QVariantAnimation animation; ///< The cover opening (progress 0 to 1)
    qreal progress = 0.0; ///< 0 closed, 1 open
    bool closing = false;
    bool cartridge_hover = false;

    QPersistentModelIndex game;
    std::shared_ptr<const CaseArt::Art> art;
    QPixmap picture; ///< The game's picture: the cartridge label, and the cover without case art
    QString title;
    bool is_switch2 = true;
};
