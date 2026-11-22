// SPDX-FileCopyrightText: 2026 PhoenixEmu contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cmath>

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include "phoenix/game/case_box.h"
#include "phoenix/game/case_viewer.h"
#include "phoenix/game/game_card.h"
#include "qt_common/game_list/game_list_p.h"

namespace {

/// Where the real cases hold their parts, as fractions of the back half (measured on the
/// player's Switch 1 and Switch 2 "inside layout" photos): the cartridge's holder low in the
/// middle, the two booklet clips by the spine.
constexpr qreal CartridgeLeft = 0.36;
constexpr qreal CartridgeWidth = 0.22;
constexpr qreal CartridgeCenterY = 0.79;
constexpr QRectF ClipUpper{0.05, 0.14, 0.16, 0.08};
constexpr QRectF ClipLower{0.05, 0.48, 0.16, 0.09};
constexpr qreal SpineFraction = 0.09; ///< The spine's width, of a half's

QRectF Part(const QRectF& panel, const QRectF& fraction) {
    return {panel.left() + fraction.x() * panel.width(), panel.top() + fraction.y() * panel.height(),
            fraction.width() * panel.width(), fraction.height() * panel.height()};
}

/// Draws `image` filling `target`, cropped to keep its shape.
void DrawCover(QPainter& p, const QImage& image, const QRectF& target, const QRectF& source_part) {
    const QRectF source{source_part.x() * image.width(), source_part.y() * image.height(),
                        source_part.width() * image.width(), source_part.height() * image.height()};
    const qreal target_aspect = target.width() / target.height();
    QRectF crop = source;
    if (crop.width() / crop.height() > target_aspect) {
        const qreal w = crop.height() * target_aspect;
        crop.adjust((crop.width() - w) / 2, 0, -(crop.width() - w) / 2, 0);
    } else {
        const qreal h = crop.width() / target_aspect;
        crop.adjust(0, (crop.height() - h) / 2, 0, -(crop.height() - h) / 2);
    }
    p.drawImage(target, image, crop);
}

} // Anonymous namespace

CaseViewer::CaseViewer(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_NoSystemBackground);
    hide();
    animation.setStartValue(0.0);
    animation.setEndValue(1.0);
    animation.setDuration(1000); // eased in Opening()
    animation.setEasingCurve(QEasingCurve::Linear);
    intro_animation.setStartValue(0.0);
    intro_animation.setEndValue(1.0);
    intro_animation.setDuration(380);
    intro_animation.setEasingCurve(QEasingCurve::OutCubic);
    connect(&intro_animation, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        intro = value.toReal();
        update();
    });
    connect(&intro_animation, &QVariantAnimation::finished, this, [this] {
        if (closing) {
            closing = false;
            hide();
            if (parentWidget() != nullptr) {
                parentWidget()->setFocus();
            }
        }
    });
    turn_animation.setDuration(380);
    turn_animation.setEasingCurve(QEasingCurve::InOutCubic);
    connect(&turn_animation, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        inspect_angle = value.toReal();
        update();
    });
    connect(&turn_animation, &QVariantAnimation::finished, this, [this] {
        if (stage == Stage::Turning) {
            stage = Stage::Opening;
            progress = 0.0;
            animation.setDirection(QAbstractAnimation::Forward);
            animation.start();
        }
    });
    connect(&animation, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        progress = value.toReal();
        update();
    });
    connect(&animation, &QVariantAnimation::finished, this, [this] {
        if (closing) {
            // Closed again: then the case goes back.
            stage = Stage::Inspect;
            intro_animation.setDirection(QAbstractAnimation::Backward);
            intro_animation.start();
        }
    });
}

void CaseViewer::Open(const QModelIndex& index) {
    const QModelIndex first = index.sibling(index.row(), 0);
    if (!first.isValid()) {
        return;
    }
    game = first;
    title = first.data(GameListItemPath::TitleRole).toString();
    is_switch2 = first.data(GameListItemPath::Switch2Role).toBool();
    art = CaseArt::Load(title, first.data(GameListItemPath::ProgramIdRole).toULongLong());
    picture = first.data(GameListItemPath::LargePictureRole).value<QPixmap>();
    if (picture.isNull()) {
        picture = first.data(Qt::DecorationRole).value<QPixmap>();
    }
    closing = false;
    cartridge_hover = false;
    if (parentWidget() != nullptr) {
        setGeometry(parentWidget()->rect());
    }
    progress = 0.0;
    intro = 0.0;
    stage = Stage::Inspect;
    inspect_angle = CaseRestAngle;
    dragging = false;
    show();
    raise();
    setFocus();
    animation.stop();
    turn_animation.stop();
    intro_animation.stop();
    intro_animation.setDirection(QAbstractAnimation::Forward);
    intro_animation.start();
}

void CaseViewer::StartOpening() {
    // Turn to face the eye by the short way round, then open.
    qreal from = std::fmod(inspect_angle, 360.0);
    if (from > 180.0) {
        from -= 360.0;
    } else if (from < -180.0) {
        from += 360.0;
    }
    stage = Stage::Turning;
    turn_animation.stop();
    turn_animation.setStartValue(from);
    turn_animation.setEndValue(0.0);
    turn_animation.setDuration(std::max(120, static_cast<int>(std::abs(from) / 180.0 * 600)));
    turn_animation.start();
}

void CaseViewer::Close() {
    if (!isVisible() || closing) {
        return;
    }
    closing = true;
    turn_animation.stop();
    if (stage == Stage::Opening && progress > 0.0) {
        animation.stop();
        animation.setDirection(QAbstractAnimation::Backward);
        animation.setCurrentTime(static_cast<int>(progress * animation.duration()));
        animation.start();
        return;
    }
    stage = Stage::Inspect;
    intro_animation.stop();
    intro_animation.setDirection(QAbstractAnimation::Backward);
    intro_animation.setCurrentTime(static_cast<int>(intro * intro_animation.duration()));
    intro_animation.start();
}

void CaseViewer::Play() {
    const QModelIndex index = game;
    closing = false;
    animation.stop();
    intro_animation.stop();
    turn_animation.stop();
    hide();
    if (index.isValid()) {
        emit PlayRequested(index);
    }
}

namespace {
qreal Smooth(qreal t) {
    t = std::clamp(t, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}
} // Anonymous namespace

qreal CaseViewer::Intro() const {
    return Smooth(intro);
}

qreal CaseViewer::Opening() const {
    // A slow start and a soft landing, like a hinge with a little stiffness.
    const qreal t = std::clamp(progress, 0.0, 1.0);
    return t < 0.5 ? 4 * t * t * t : 1 - std::pow(-2 * t + 2, 3) / 2;
}

CaseViewer::Layout CaseViewer::ComputeLayout() const {
    const qreal aspect = GameCard::CaseAspect;
    const qreal full_h = std::min(height() * 0.72,
                                  width() * 0.9 / (2.0 * aspect + SpineFraction * aspect));
    const qreal h = full_h * (0.82 + 0.18 * Intro());
    const qreal w = h * aspect;
    const qreal top = (height() - h) / 2 + 12;
    const qreal open_hinge = width() / 2.0 + w * SpineFraction / 2;
    const qreal closed_hinge = width() / 2.0 - w / 2;
    Layout layout;
    const qreal open = Opening();
    const qreal hinge = closed_hinge + (open_hinge - closed_hinge) * open;
    const qreal spine_w = w * SpineFraction * open;
    layout.tray = {hinge, top, w, h};
    layout.spine = {hinge - spine_w, top, spine_w, h};
    layout.hinge_x = hinge - spine_w;
    layout.angle = 180.0 * open;
    const qreal cart_w = w * CartridgeWidth;
    const qreal cart_h = cart_w / GameCard::CartridgeAspect;
    layout.cartridge = {hinge + w * CartridgeLeft, top + h * CartridgeCenterY - cart_h / 2, cart_w,
                        cart_h};
    return layout;
}

QRectF CaseViewer::ClosedCaseRect() const {
    // Where the closed case sits (the cover over the back half), at the intro's scale.
    const Layout layout = ComputeLayout();
    return {layout.hinge_x, layout.tray.top(), layout.tray.width(), layout.tray.height()};
}

void CaseViewer::PaintInside(QPainter& p, const QRectF& panel, bool left_half) const {
    const qreal radius = panel.width() * 0.035;
    QPainterPath shape;
    shape.addRoundedRect(panel, radius, radius);
    p.save();
    p.setClipPath(shape);
    // The plastic, then the inside of the cover seen through it.
    const QColor plastic = is_switch2 ? QColor(0xD2, 0x08, 0x14) : QColor(0xE6, 0xE9, 0xED);
    QLinearGradient body{panel.topLeft(), panel.bottomRight()};
    body.setColorAt(0.0, plastic.lighter(112));
    body.setColorAt(0.55, plastic);
    body.setColorAt(1.0, plastic.darker(118));
    p.fillRect(panel, body);
    const QRectF sheet = panel.adjusted(panel.width() * 0.045, panel.height() * 0.03,
                                        -panel.width() * 0.045, -panel.height() * 0.03);
    if (art != nullptr && !art->inside.isNull()) {
        DrawCover(p, art->inside, sheet,
                  left_half ? QRectF(0, 0, 0.5, 1) : QRectF(0.5, 0, 0.5, 1));
    } else {
        p.fillRect(sheet, QColor(0xF4, 0xF2, 0xEE));
    }
    // The sheet sits in a shallow recess: a shadow along its top and spine edges.
    {
        QLinearGradient top_shadow{sheet.topLeft(), QPointF(sheet.left(), sheet.top() + sheet.height() * 0.05)};
        top_shadow.setColorAt(0.0, QColor(0, 0, 0, 70));
        top_shadow.setColorAt(1.0, QColor(0, 0, 0, 0));
        p.fillRect(sheet, top_shadow);
        const qreal edge = left_half ? sheet.right() : sheet.left();
        QLinearGradient spine_shadow{QPointF(edge, 0),
                                     QPointF(edge + (left_half ? -1 : 1) * sheet.width() * 0.08, 0)};
        spine_shadow.setColorAt(0.0, QColor(0, 0, 0, 80));
        spine_shadow.setColorAt(1.0, QColor(0, 0, 0, 0));
        p.fillRect(sheet, spine_shadow);
    }
    if (is_switch2) {
        // Seen through red plastic: the colours go to shades of red.
        p.setCompositionMode(QPainter::CompositionMode_Multiply);
        p.fillRect(sheet, QColor(0xFF, 0x22, 0x2A));
        p.setCompositionMode(QPainter::CompositionMode_SourceOver);
        p.fillRect(sheet, QColor(0xE0, 0x10, 0x1C, 70));
    } else {
        p.fillRect(sheet, QColor(255, 255, 255, 60)); // clear plastic, a little frosted
    }
    // The plastic's gloss.
    QLinearGradient gloss{panel.topLeft(), QPointF(panel.left() + panel.width() * 0.7,
                                                   panel.top() + panel.height() * 0.45)};
    gloss.setColorAt(0.0, QColor(255, 255, 255, 46));
    gloss.setColorAt(0.6, QColor(255, 255, 255, 0));
    p.fillRect(panel, gloss);
    // The rim, catching the light.
    p.setClipping(false);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(is_switch2 ? QColor(0xFF, 0x5A, 0x60, 200) : QColor(255, 255, 255, 220),
                  std::max<qreal>(2.0, panel.width() * 0.012)));
    p.drawRoundedRect(panel.adjusted(1, 1, -1, -1), radius, radius);
    p.restore();
}

void CaseViewer::PaintCoverFront(QPainter& p, const QRectF& panel) const {
    const qreal dpr = devicePixelRatioF();
    const int height_px = qRound(panel.height());
    if (art != nullptr && !art->front.isNull()) {
        p.drawPixmap(panel, GameCard::CasePixmap(height_px, dpr, is_switch2, false),
                     QRectF(GameCard::CasePixmap(height_px, dpr, is_switch2, false).rect()));
        const QRectF window = GameCard::CaseArtWindowRect(panel);
        p.save();
        QPainterPath clip;
        clip.addRoundedRect(window, window.width() * 0.015, window.width() * 0.015);
        p.setClipPath(clip);
        DrawCover(p, art->front, window, QRectF(0, 0, 1, 1));
        p.restore();
        return;
    }
    const QPixmap frame = GameCard::CasePixmap(height_px, dpr, is_switch2, true);
    p.drawPixmap(panel, frame, QRectF(frame.rect()));
    if (!picture.isNull()) {
        GameCard::DrawCoverCropped(p, picture, GameCard::CaseCoverRect(panel));
    }
}

void CaseViewer::PaintTrayParts(QPainter& p, const Layout& layout) const {
    const QRectF& tray = layout.tray;
    const QColor raised = is_switch2 ? QColor(0xF0, 0x2A, 0x34, 230) : QColor(255, 255, 255, 235);
    const QColor shade = is_switch2 ? QColor(0x80, 0x00, 0x08, 160) : QColor(0x9A, 0xA0, 0xA8, 150);
    // The booklet clips.
    for (const QRectF& fraction : {ClipUpper, ClipLower}) {
        const QRectF clip = Part(tray, fraction);
        p.setPen(QPen(shade, 1.5));
        p.setBrush(raised);
        p.drawRoundedRect(clip, clip.height() * 0.3, clip.height() * 0.3);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 255, 255, 70));
        p.drawRoundedRect(clip.adjusted(clip.width() * 0.15, clip.height() * 0.2,
                                        -clip.width() * 0.15, -clip.height() * 0.55),
                          4, 4);
    }
    // The cartridge's holder: a raised frame a little bigger than the cartridge, clips at its sides.
    const QRectF cart = layout.cartridge;
    const QRectF holder = cart.adjusted(-cart.width() * 0.1, -cart.height() * 0.07,
                                        cart.width() * 0.1, cart.height() * 0.07);
    p.setPen(QPen(shade, 1.5));
    p.setBrush(raised);
    p.drawRoundedRect(holder, holder.width() * 0.1, holder.width() * 0.1);
    p.setBrush(shade);
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(cart.adjusted(-2, -2, 2, 2), cart.width() * 0.08, cart.width() * 0.08);
    for (const qreal side : {holder.left() + holder.width() * 0.02, holder.right() - holder.width() * 0.1}) {
        p.setBrush(raised.lighter(110));
        p.drawRoundedRect(QRectF(side, holder.top() + holder.height() * 0.35,
                                 holder.width() * 0.08, holder.height() * 0.3),
                          2, 2);
    }

    // The cartridge, with the game's art on its label; lifted a little when the pointer is on it.
    QRectF shown = cart;
    if (cartridge_hover) {
        shown.translate(0, -cart.height() * 0.05);
        for (int i = 6; i >= 1; --i) {
            p.setPen(QPen(QColor(0xFF, 0xC8, 0x3D, 18 + (6 - i) * 14), 2));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(shown.adjusted(-i * 2.0, -i * 2.0, i * 2.0, i * 2.0),
                              cart.width() * 0.1 + i * 2.0, cart.width() * 0.1 + i * 2.0);
        }
    }
    const QPixmap frame = GameCard::CartridgePixmap(qRound(shown.height()), devicePixelRatioF(),
                                                    is_switch2);
    p.drawPixmap(shown, frame, QRectF(frame.rect()));
    const QRectF label = GameCard::CartridgeLabelRect(shown);
    p.save();
    QPainterPath label_clip;
    label_clip.addRoundedRect(label, label.width() * 0.04, label.width() * 0.04);
    p.setClipPath(label_clip);
    if (!picture.isNull()) {
        GameCard::DrawCoverCropped(p, picture, label);
    } else if (art != nullptr && !art->front.isNull()) {
        DrawCover(p, art->front, label, QRectF(0, 0.12, 1, 0.75));
    }
    p.restore();
}

void CaseViewer::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.fillRect(rect(), QColor(8, 6, 4, static_cast<int>(200 * Intro())));
    const Layout layout = ComputeLayout();
    p.setOpacity(std::clamp(Intro() * 1.6, 0.0, 1.0));

    if (stage != Stage::Opening) {
        // The case in front of you, to be turned round, then opened.
        // (The box is drawn at 92% of the rectangle it gets: given a larger one, it matches the
        // opening cover's size exactly.)
        const QRectF closed = ClosedCaseRect();
        const QRectF box_rect{closed.center().x() - closed.width() / 0.92 / 2,
                              closed.center().y() - closed.height() / 0.92 / 2,
                              closed.width() / 0.92, closed.height() / 0.92};
        DrawCaseBox(p, game, box_rect, devicePixelRatioF(), inspect_angle, picture);
        p.setOpacity(Intro());
        QFont font = p.font();
        font.setPixelSize(std::max(16, static_cast<int>(layout.tray.height() * 0.045)));
        font.setBold(true);
        p.setFont(font);
        p.setPen(QColor(0xF2, 0xEE, 0xE6));
        p.drawText(QRectF(0, layout.tray.top() - font.pixelSize() * 2.4, width(),
                          font.pixelSize() * 2),
                   Qt::AlignCenter, title);
        font.setBold(false);
        font.setPixelSize(std::max(12, font.pixelSize() * 2 / 3));
        p.setFont(font);
        p.setPen(QColor(0xC8, 0xBC, 0xA8));
        p.drawText(QRectF(0, layout.tray.bottom() + 10, width(), font.pixelSize() * 2),
                   Qt::AlignCenter,
                   tr("Drag to turn it round  ·  Click the case to open it  ·  Esc to close"));
        return;
    }

    // The back half, and its parts once the cover is away from them.
    PaintInside(p, layout.tray, false);
    const qreal parts = std::clamp((Opening() - 0.25) / 0.35, 0.0, 1.0);
    if (parts > 0.0) {
        const qreal before = p.opacity();
        p.setOpacity(before * parts);
        PaintTrayParts(p, layout);
        p.setOpacity(before);
    }
    // The spine, seen from inside.
    if (layout.spine.width() > 0.5) {
        const QColor plastic = is_switch2 ? QColor(0xB0, 0x03, 0x10) : QColor(0xCF, 0xD3, 0xD8);
        p.fillRect(layout.spine, plastic);
        p.fillRect(layout.spine.adjusted(layout.spine.width() * 0.3, 0, -layout.spine.width() * 0.3, 0),
                   plastic.darker(115));
    }
    // The cover's shadow on the tray while it is still over it.
    if (layout.angle > 0.5 && layout.angle < 100.0) {
        const qreal lift = std::sin(layout.angle * 3.14159265358979 / 180.0);
        QLinearGradient shadow{QPointF(layout.hinge_x, 0),
                               QPointF(layout.hinge_x + layout.tray.width() * (1.0 - layout.angle / 110.0), 0)};
        shadow.setColorAt(0.0, QColor(0, 0, 0, static_cast<int>(130 * lift)));
        shadow.setColorAt(1.0, QColor(0, 0, 0, 0));
        p.fillRect(layout.tray, shadow);
    }
    // The cover, turning on the spine: its front while it faces us, then its inside.
    const QRectF cover{layout.hinge_x, layout.tray.top(), layout.tray.width(),
                       layout.tray.height()};
    QTransform turn;
    turn.translate(layout.hinge_x, cover.center().y());
    // A positive turn brings the cover's free edge towards the eye as it swings over, as a real
    // cover does (the other way it sank into the screen and looked like a slide); a short eye
    // distance makes the perspective visible.
    turn.rotate(layout.angle, Qt::YAxis, cover.width() * 3.4);
    turn.translate(-layout.hinge_x, -cover.center().y());
    p.save();
    p.setTransform(turn, true);
    if (layout.angle < 90.0) {
        PaintCoverFront(p, cover);
        // Turning away from the light.
        p.fillRect(cover, QColor(0, 0, 0, static_cast<int>(layout.angle / 90.0 * 90)));
    } else {
        // Seen from behind the cover is mirrored: drawn mirrored back so its art reads right.
        QTransform mirror;
        mirror.translate(cover.center().x(), 0);
        mirror.scale(-1, 1);
        mirror.translate(-cover.center().x(), 0);
        p.setTransform(mirror, true);
        PaintInside(p, cover, true);
        p.fillRect(cover, QColor(0, 0, 0, static_cast<int>((180.0 - layout.angle) / 90.0 * 90)));
    }
    p.restore();

    // The game's name above, what to do below.
    p.setOpacity(Opening());
    QFont font = p.font();
    font.setPixelSize(std::max(16, static_cast<int>(layout.tray.height() * 0.045)));
    font.setBold(true);
    p.setFont(font);
    p.setPen(QColor(0xF2, 0xEE, 0xE6));
    p.drawText(QRectF(0, layout.tray.top() - font.pixelSize() * 2.4, width(), font.pixelSize() * 2),
               Qt::AlignCenter, title);
    font.setBold(false);
    font.setPixelSize(std::max(12, font.pixelSize() * 2 / 3));
    p.setFont(font);
    p.setPen(QColor(0xC8, 0xBC, 0xA8));
    p.drawText(QRectF(0, layout.tray.bottom() + 10, width(), font.pixelSize() * 2),
               Qt::AlignCenter, tr("Click the cartridge to play  ·  Esc to close"));
}

void CaseViewer::mouseMoveEvent(QMouseEvent* event) {
    if (stage != Stage::Opening) {
        if (dragging) {
            const qreal dx = event->position().x() - press_pos.x();
            if (!dragged && std::abs(dx) > 5) {
                dragged = true;
                setCursor(Qt::ClosedHandCursor);
            }
            if (dragged) {
                // Half a degree a pixel, either way, all the way round.
                inspect_angle = std::fmod(press_angle + dx * 0.5 + 720.0, 360.0);
                update();
            }
        } else {
            setCursor(ClosedCaseRect().contains(event->position()) ? Qt::OpenHandCursor
                                                                   : Qt::ArrowCursor);
        }
        return;
    }
    const bool on_cartridge =
        Opening() > 0.95 && ComputeLayout().cartridge.contains(event->position());
    if (on_cartridge != cartridge_hover) {
        cartridge_hover = on_cartridge;
        setCursor(on_cartridge ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
}

void CaseViewer::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        Close();
        return;
    }
    if (stage == Stage::Turning) {
        return;
    }
    if (stage == Stage::Inspect) {
        // The case turned round may reach past its front's rectangle: a little room around it.
        const QRectF box = ClosedCaseRect();
        if (box.adjusted(-box.width() * 0.25, 0, box.width() * 0.25, 0).contains(event->position())) {
            dragging = true;
            dragged = false;
            press_pos = event->position();
            press_angle = inspect_angle;
        } else {
            Close();
        }
        return;
    }
    const Layout layout = ComputeLayout();
    if (Opening() > 0.95 && layout.cartridge.adjusted(-6, -6, 6, 6).contains(event->position())) {
        Play();
        return;
    }
    const QRectF open_case{layout.hinge_x - layout.tray.width(), layout.tray.top(),
                           layout.tray.right() - layout.hinge_x + layout.tray.width(),
                           layout.tray.height()};
    if (!open_case.contains(event->position())) {
        Close();
    }
}

void CaseViewer::mouseReleaseEvent(QMouseEvent* event) {
    if (stage == Stage::Inspect && dragging && event->button() == Qt::LeftButton) {
        dragging = false;
        setCursor(Qt::OpenHandCursor);
        if (!dragged) {
            StartOpening(); // a click, not a drag: open it
        }
    }
}

void CaseViewer::keyPressEvent(QKeyEvent* event) {
    switch (event->key()) {
    case Qt::Key_Escape:
    case Qt::Key_Backspace:
        Close();
        return;
    case Qt::Key_Left:
    case Qt::Key_Right:
        if (stage == Stage::Inspect) {
            inspect_angle = std::fmod(inspect_angle + (event->key() == Qt::Key_Left ? -15.0 : 15.0) + 360.0,
                                      360.0);
            update();
        }
        return;
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Space:
        if (stage == Stage::Inspect) {
            StartOpening();
        } else if (Opening() > 0.95) {
            Play();
        }
        return;
    default:
        QWidget::keyPressEvent(event);
    }
}
