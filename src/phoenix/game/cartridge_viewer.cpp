// SPDX-FileCopyrightText: 2026 PhoenixEmu contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cmath>

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>

#include "phoenix/game/cartridge_viewer.h"
#include "phoenix/game/case_box.h"
#include "phoenix/game/game_card.h"
#include "qt_common/game_list/case_art.h"
#include "qt_common/game_list/game_list_p.h"

namespace {

constexpr qreal Pi = 3.14159265358979;

// The kiln palette (dist/qt_themes/phoenix/style.qss).
const QColor TextColor{0xF6, 0xEB, 0xDD};
const QColor MutedColor{0xA8, 0x99, 0x8A};
const QColor EmberColor{0xFF, 0x5A, 0x1F};
const QColor GoldColor{0xFF, 0xC2, 0x3D};
const QColor PanelColor{0x1A, 0x15, 0x12};
const QColor LineColor{0x3A, 0x2E, 0x25};

qreal Smooth(qreal t) {
    t = std::clamp(t, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

/// The angle folded into -180..180.
qreal Fold(qreal degrees) {
    qreal a = std::fmod(degrees, 360.0);
    if (a > 180.0) {
        a -= 360.0;
    } else if (a < -180.0) {
        a += 360.0;
    }
    return a;
}

} // Anonymous namespace

CartridgeViewer::CartridgeViewer(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_NoSystemBackground);
    hide();
    intro_animation.setStartValue(0.0);
    intro_animation.setEndValue(1.0);
    intro_animation.setDuration(360);
    intro_animation.setEasingCurve(QEasingCurve::OutCubic);
    connect(&intro_animation, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        intro = v.toReal();
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
    turn_animation.setEasingCurve(QEasingCurve::InOutCubic);
    connect(&turn_animation, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        angle = v.toReal();
        update();
    });
}

void CartridgeViewer::Open(const QModelIndex& index) {
    const QModelIndex first = index.sibling(index.row(), 0);
    if (!first.isValid()) {
        return;
    }
    game = first;
    title = first.data(GameListItemPath::TitleRole).toString();
    is_switch2 = first.data(GameListItemPath::Switch2Role).toBool();
    picture = first.data(GameListItemPath::LargePictureRole).value<QPixmap>();
    if (picture.isNull()) {
        picture = first.data(Qt::DecorationRole).value<QPixmap>();
    }
    if (picture.isNull()) {
        // No picture of its own: the label from the case's front.
        const auto art = CaseArt::Load(title, first.data(GameListItemPath::ProgramIdRole).toULongLong(),
                                       is_switch2);
        if (art != nullptr && !art->front.isNull()) {
            picture = QPixmap::fromImage(art->front);
        }
    }
    if (parentWidget() != nullptr) {
        setGeometry(parentWidget()->rect());
    }
    closing = false;
    intro = 0.0;
    // Comes forward turning to its rest, an edge showing, so it reads as something solid.
    angle = CartridgeRestAngle - 40.0;
    zoom = 1.0;
    dragging = false;
    hover = Button::None;
    show();
    raise();
    setFocus();
    turn_animation.stop();
    turn_animation.setStartValue(angle);
    turn_animation.setEndValue(CartridgeRestAngle);
    turn_animation.setDuration(700);
    turn_animation.start();
    intro_animation.stop();
    intro_animation.setDirection(QAbstractAnimation::Forward);
    intro_animation.start();
}

void CartridgeViewer::Close() {
    if (!isVisible() || closing) {
        return;
    }
    closing = true;
    intro_animation.stop();
    intro_animation.setDirection(QAbstractAnimation::Backward);
    intro_animation.setCurrentTime(static_cast<int>(intro * intro_animation.duration()));
    intro_animation.start();
}

void CartridgeViewer::Play() {
    const QModelIndex index = game;
    closing = false;
    intro_animation.stop();
    turn_animation.stop();
    hide();
    if (index.isValid()) {
        emit PlayRequested(index);
    }
}

void CartridgeViewer::TurnOver() {
    // To the other face's rest, half a turn on.
    const qreal from = Fold(angle);
    const bool front = std::abs(from) < 90.0;
    const qreal to = front ? CartridgeRestAngle + 180.0 : CartridgeRestAngle;
    const qreal start = (!front && from > 0) ? from - 360.0 : from;
    turn_animation.stop();
    turn_animation.setStartValue(start);
    turn_animation.setEndValue(to);
    turn_animation.setDuration(560);
    turn_animation.start();
}

qreal CartridgeViewer::Intro() const {
    return Smooth(intro);
}

QRectF CartridgeViewer::CardRect() const {
    const qreal room_h = height() * 0.62;
    const qreal room_w = width() * 0.5;
    qreal h = std::min(room_h, room_w / GameCard::CartridgeAspect);
    h *= zoom * (0.8 + 0.2 * Intro());
    const qreal w = h * GameCard::CartridgeAspect;
    const qreal cy = height() * 0.46;
    return {width() / 2.0 - w / 2, cy - h / 2, w, h};
}

CartridgeViewer::Buttons CartridgeViewer::ButtonRects() const {
    // Laid out from the unzoomed card, so they stay put while it is brought nearer.
    const qreal room_h = height() * 0.62;
    const qreal base_h = std::min(room_h, width() * 0.5 / GameCard::CartridgeAspect);
    const qreal y = height() * 0.46 + base_h / 2 + 34;
    const qreal h = 40;
    const qreal play_w = 120, turn_w = 136, close_w = 100, gap = 12;
    const qreal total = play_w + turn_w + close_w + gap * 2;
    qreal x = width() / 2.0 - total / 2;
    Buttons b;
    b.play = {x, y, play_w, h};
    x += play_w + gap;
    b.turn = {x, y, turn_w, h};
    x += turn_w + gap;
    b.close = {x, y, close_w, h};
    return b;
}

CartridgeViewer::Button CartridgeViewer::ButtonAt(const QPointF& pos) const {
    const Buttons b = ButtonRects();
    if (b.play.contains(pos)) {
        return Button::Play;
    }
    if (b.turn.contains(pos)) {
        return Button::Turn;
    }
    if (b.close.contains(pos)) {
        return Button::Close;
    }
    return Button::None;
}



void CartridgeViewer::PaintButton(QPainter& p, const QRectF& rect, const QString& text,
                                  bool primary, bool hovered) const {
    const qreal r = rect.height() / 2;
    p.setPen(primary ? Qt::NoPen : QPen(hovered ? EmberColor : LineColor, 1.5));
    if (primary) {
        QLinearGradient fill{rect.topLeft(), rect.bottomRight()};
        fill.setColorAt(0.0, hovered ? EmberColor.lighter(115) : EmberColor);
        fill.setColorAt(1.0, hovered ? GoldColor.lighter(108) : GoldColor);
        p.setBrush(fill);
    } else {
        p.setBrush(hovered ? QColor(0x2A, 0x21, 0x1B) : PanelColor);
    }
    p.drawRoundedRect(rect, r, r);
    QFont font = p.font();
    font.setPixelSize(15);
    font.setWeight(QFont::Bold);
    p.setFont(font);
    p.setPen(primary ? QColor(0x1A, 0x0C, 0x05) : TextColor);
    p.drawText(rect, Qt::AlignCenter, text);
}

void CartridgeViewer::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.fillRect(rect(), QColor(8, 6, 4, static_cast<int>(228 * Intro())));
    p.setOpacity(std::clamp(Intro() * 1.5, 0.0, 1.0));

    const QRectF card = CardRect();
    const qreal a = Fold(angle);
    const qreal rad = a * Pi / 180.0;
    const bool back_showing = std::cos(rad) < 0.0;

    // A warm glow behind it.
    {
        QRadialGradient glow{card.center(), card.height() * 0.75};
        QColor tint = is_switch2 ? QColor(0xD8, 0x26, 0x1A) : EmberColor;
        tint.setAlpha(static_cast<int>(70 * Intro()));
        glow.setColorAt(0.0, tint);
        glow.setColorAt(1.0, QColor(0, 0, 0, 0));
        p.fillRect(rect(), glow);
    }

    // The card as a solid, like the cases (case_box.cpp).
    DrawCartridgeBox(p, is_switch2, card, devicePixelRatioF(), a, picture);

    // The game's name and which card it is above, the buttons and keys below.
    const qreal base_top = height() * 0.46 -
                           std::min(height() * 0.62, width() * 0.5 / GameCard::CartridgeAspect) / 2;
    QFont font = p.font();
    font.setPixelSize(26);
    font.setWeight(QFont::Black);
    p.setFont(font);
    p.setPen(TextColor);
    p.drawText(QRectF(0, base_top - 84, width(), 36), Qt::AlignCenter, title);
    font.setPixelSize(13);
    font.setWeight(QFont::Bold);
    font.setLetterSpacing(QFont::PercentageSpacing, 112);
    p.setFont(font);
    p.setPen(is_switch2 ? QColor(0xFF, 0x6A, 0x5C) : MutedColor);
    p.drawText(QRectF(0, base_top - 46, width(), 22), Qt::AlignCenter,
               (is_switch2 ? tr("SWITCH 2 GAME CARD") : tr("SWITCH 1 GAME CARD")) +
                   (back_showing ? tr("  ·  BACK") : tr("  ·  FRONT")));
    font.setLetterSpacing(QFont::PercentageSpacing, 100);

    const Buttons b = ButtonRects();
    PaintButton(p, b.play, tr("Play"), true, hover == Button::Play);
    PaintButton(p, b.turn, tr("Turn over"), false, hover == Button::Turn);
    PaintButton(p, b.close, tr("Close"), false, hover == Button::Close);

    font.setPixelSize(13);
    font.setWeight(QFont::DemiBold);
    p.setFont(font);
    p.setPen(MutedColor);
    p.drawText(QRectF(0, b.play.bottom() + 16, width(), 22), Qt::AlignCenter,
               tr("Drag to turn  ·  Space turns it over  ·  Scroll to bring it nearer  ·  "
                  "Enter to play  ·  Esc to close"));
}

void CartridgeViewer::mouseMoveEvent(QMouseEvent* event) {
    if (dragging) {
        const qreal dx = event->position().x() - press_pos.x();
        if (!dragged && std::abs(dx) > 4) {
            dragged = true;
            setCursor(Qt::ClosedHandCursor);
        }
        if (dragged) {
            angle = press_angle + dx * 0.6;
            update();
        }
        return;
    }
    const Button over = ButtonAt(event->position());
    if (over != hover) {
        hover = over;
        update();
    }
    if (over != Button::None) {
        setCursor(Qt::PointingHandCursor);
    } else {
        setCursor(CardRect().contains(event->position()) ? Qt::OpenHandCursor : Qt::ArrowCursor);
    }
}

void CartridgeViewer::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        Close();
        return;
    }
    switch (ButtonAt(event->position())) {
    case Button::Play:
        Play();
        return;
    case Button::Turn:
        TurnOver();
        return;
    case Button::Close:
        Close();
        return;
    case Button::None:
        break;
    }
    const QRectF card = CardRect();
    if (card.adjusted(-card.width() * 0.2, 0, card.width() * 0.2, 0).contains(event->position())) {
        turn_animation.stop();
        dragging = true;
        dragged = false;
        press_pos = event->position();
        press_angle = angle;
        return;
    }
    Close();
}

void CartridgeViewer::mouseReleaseEvent(QMouseEvent* event) {
    if (!dragging || event->button() != Qt::LeftButton) {
        return;
    }
    dragging = false;
    setCursor(Qt::OpenHandCursor);
    if (!dragged) {
        TurnOver(); // a click on the card turns it over
        return;
    }
    // Let go: it settles at the rest of whichever face is nearer.
    const qreal from = Fold(angle);
    qreal to = std::abs(from) < 90.0 ? CartridgeRestAngle : CartridgeRestAngle + 180.0;
    if (to - from > 180.0) {
        to -= 360.0;
    }
    turn_animation.stop();
    turn_animation.setStartValue(from);
    turn_animation.setEndValue(to);
    turn_animation.setDuration(std::max(140, static_cast<int>(std::abs(to - from) / 90.0 * 360)));
    turn_animation.start();
}

void CartridgeViewer::wheelEvent(QWheelEvent* event) {
    const int delta = event->angleDelta().y();
    if (delta == 0) {
        return;
    }
    zoom = std::clamp(zoom * (delta > 0 ? 1.08 : 1.0 / 1.08), 0.7, 1.45);
    update();
    event->accept();
}

void CartridgeViewer::keyPressEvent(QKeyEvent* event) {
    switch (event->key()) {
    case Qt::Key_Escape:
    case Qt::Key_Backspace:
        Close();
        return;
    case Qt::Key_Space:
    case Qt::Key_F:
        TurnOver();
        return;
    case Qt::Key_Left:
    case Qt::Key_Right:
        turn_animation.stop();
        angle += event->key() == Qt::Key_Left ? -15.0 : 15.0;
        update();
        return;
    case Qt::Key_Plus:
    case Qt::Key_Equal:
    case Qt::Key_Minus:
        zoom = std::clamp(zoom * (event->key() == Qt::Key_Minus ? 1.0 / 1.08 : 1.08), 0.7, 1.45);
        update();
        return;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        Play();
        return;
    default:
        QWidget::keyPressEvent(event);
    }
}
