// SPDX-FileCopyrightText: 2026 PhoenixEmu contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>

#include <QPainter>
#include <QPainterPath>
#include <QPixmapCache>
#include <QPolygonF>
#include <QTransform>

#include "phoenix/game/case_box.h"
#include "phoenix/game/game_card.h"
#include "qt_common/game_list/game_list_p.h"

/// A face of the case: the plastic with its rim, and what shows through it (`paint_inner` fills
/// the part inside the rim). Sizes in logical pixels.
namespace {
QPixmap CaseFace(QSizeF size, qreal dpr, const QColor& plastic, qreal rim, qreal radius,
                 const std::function<void(QPainter&, const QRectF&)>& paint_inner) {
    QPixmap face(QSize(std::max(1, qRound(size.width() * dpr)), std::max(1, qRound(size.height() * dpr))));
    face.setDevicePixelRatio(dpr);
    face.fill(Qt::transparent);
    QPainter fp(&face);
    fp.setRenderHint(QPainter::Antialiasing);
    fp.setRenderHint(QPainter::SmoothPixmapTransform);
    const QRectF whole{QPointF(0, 0), size};
    // The plastic, lighter where the light falls.
    QLinearGradient body{whole.topLeft(), whole.bottomRight()};
    body.setColorAt(0.0, plastic.lighter(115));
    body.setColorAt(0.6, plastic);
    body.setColorAt(1.0, plastic.darker(115));
    fp.setPen(Qt::NoPen);
    fp.setBrush(body);
    fp.drawRoundedRect(whole, radius, radius);
    const QRectF inner = whole.adjusted(rim, rim, -rim, -rim);
    if (paint_inner && inner.width() > 1 && inner.height() > 1) {
        fp.save();
        QPainterPath clip;
        clip.addRoundedRect(inner, radius * 0.35, radius * 0.35);
        fp.setClipPath(clip);
        paint_inner(fp, inner);
        fp.restore();
    }
    // The rim's edge catching the light, and the light across the clear sleeve (the Switch
    // Cabinet viewer's sweep: clear, a highlight at a quarter, a little shade at the middle).
    fp.setBrush(Qt::NoBrush);
    fp.setPen(QPen(QColor(255, 255, 255, 70), std::max<qreal>(1.0, rim * 0.25)));
    fp.drawRoundedRect(whole.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
    QLinearGradient sweep{whole.topLeft(), whole.topRight()};
    sweep.setColorAt(0.0, QColor(255, 255, 255, 0));
    sweep.setColorAt(0.25, QColor(255, 255, 255, 56));
    sweep.setColorAt(0.5, QColor(0, 0, 0, 34));
    sweep.setColorAt(1.0, QColor(255, 255, 255, 0));
    QPainterPath shape;
    shape.addRoundedRect(whole, radius, radius);
    fp.fillPath(shape, sweep);
    return face;
}

} // Anonymous namespace

/// The picked case as a box in 3D, as the Switch Cabinet viewer (switch-images-julio) builds its
/// cases: a tenth as thick as it is wide, the eye 4.4 widths away, every corner through the same
/// projection. `angle_deg` turns it about its upright middle (39.6 shows the spine; dragging
/// spins it round to the back). Each face is drawn only while it faces the eye.
void DrawCaseBox(QPainter& p, const QModelIndex& index, const QRectF& local, qreal dpr,
                 qreal angle_deg, const QPixmap& picture) {
    const qreal w = local.width();
    const qreal h = local.height();
    const qreal box_w = w * 0.92;
    const qreal box_h = h * 0.92;
    const qreal thick = box_w * 0.098;
    const qreal eye = box_w * 4.43;
    const qreal angle = angle_deg * 3.14159265358979 / 180.0;
    const qreal cs = std::cos(angle);
    const qreal sn = std::sin(angle);
    const QPointF centre = local.center();
    struct Vec {
        qreal x, y, z;
    };
    const auto turn = [&](const Vec& v) {
        return Vec{cs * v.x + sn * v.z, v.y, -sn * v.x + cs * v.z};
    };
    const auto project = [&](const Vec& v) {
        const Vec r = turn(v);
        const qreal f = eye / (eye - r.z);
        return QPointF(centre.x() + r.x * f, centre.y() + r.y * f);
    };
    const qreal hw = box_w / 2;
    const qreal hh = box_h / 2;
    const qreal hd = thick / 2;
    const bool is_switch2 = index.data(GameListItemPath::Switch2Role).toBool();
    const QColor plastic = is_switch2 ? QColor(0xD0, 0x0A, 0x16) : QColor(0xE2, 0xE5, 0xE9);
    const qreal rim = box_w * (6.0 / 256.0);
    const qreal radius = box_w * (12.0 / 256.0);

    // The faces' pictures, each drawn flat when its face shows (a case standing on a shelf shows
    // only its spine), then put on its face.
    const QPixmap cover = index.data(GameListItemPath::CaseFrontRole).value<QPixmap>();
    const QPixmap back = index.data(GameListItemPath::CaseBackRole).value<QPixmap>();
    const QPixmap spine = index.data(GameListItemPath::CaseSpineRole).value<QPixmap>();
    const QSizeF face_size{box_w, box_h};
    const QSizeF side_size{thick, box_h};
    const auto make_front = [&]() -> QPixmap {
        if (!cover.isNull()) {
            return CaseFace(face_size, dpr, plastic, rim, radius,
                            [&](QPainter& fp, const QRectF& inner) {
                                GameCard::DrawCoverCropped(fp, cover, inner);
                            });
        }
        // No case art: the generated case, its own band and the game's picture.
        QPixmap front_face(QSize(qRound(box_w * dpr), qRound(box_h * dpr)));
        front_face.setDevicePixelRatio(dpr);
        front_face.fill(Qt::transparent);
        QPainter fp(&front_face);
        fp.setRenderHint(QPainter::Antialiasing);
        fp.setRenderHint(QPainter::SmoothPixmapTransform);
        const QRectF whole{QPointF(0, 0), face_size};
        QPixmap frame;
        QRectF art_area;
        QPixmap art_picture = picture;
        GameCard::CaseParts(index, whole, qRound(box_h), dpr, frame, art_area, art_picture);
        fp.drawPixmap(whole, frame, QRectF(frame.rect()));
        QPainterPath clip;
        clip.addRoundedRect(art_area, art_area.width() * 0.02, art_area.width() * 0.02);
        fp.setClipPath(clip);
        if (!art_picture.isNull()) {
            GameCard::DrawCoverCropped(fp, art_picture, art_area);
        }
        fp.end();
        return front_face;
    };
    const auto make_back = [&]() {
        return CaseFace(face_size, dpr, plastic, rim, radius, [&](QPainter& fp, const QRectF& inner) {
            if (!back.isNull()) {
                GameCard::DrawCoverCropped(fp, back, inner);
            } else {
                fp.fillRect(inner, QColor(0xF2, 0xF0, 0xEC));
            }
        });
    };
    const auto make_spine = [&]() {
        return CaseFace(side_size, dpr, plastic, rim * 0.45, rim * 0.6,
                        [&](QPainter& fp, const QRectF& inner) {
                            if (!spine.isNull()) {
                                fp.drawPixmap(inner, spine, QRectF(spine.rect()));
                            }
                        });
    };
    // The opening edge: plastic all the way, with the groove where the halves meet.
    const auto make_edge = [&]() {
        return CaseFace(side_size, dpr, plastic, 0, rim * 0.6, [&](QPainter& fp, const QRectF& inner) {
            fp.setPen(QPen(plastic.darker(140), std::max<qreal>(1.0, thick * 0.08)));
            fp.drawLine(QPointF(inner.center().x(), inner.top() + rim),
                        QPointF(inner.center().x(), inner.bottom() - rim));
        });
    };

    struct Face {
        std::array<Vec, 4> corners; ///< Top left, top right, bottom right, bottom left of its picture
        Vec normal;
        int kind; ///< 0 front, 1 back, 2 spine, 3 opening edge
    };
    const std::array<Face, 4> faces{{
        {{{{-hw, -hh, hd}, {hw, -hh, hd}, {hw, hh, hd}, {-hw, hh, hd}}}, {0, 0, 1}, 0},
        {{{{hw, -hh, -hd}, {-hw, -hh, -hd}, {-hw, hh, -hd}, {hw, hh, -hd}}}, {0, 0, -1}, 1},
        {{{{-hw, -hh, -hd}, {-hw, -hh, hd}, {-hw, hh, hd}, {-hw, hh, -hd}}}, {-1, 0, 0}, 2},
        {{{{hw, -hh, hd}, {hw, -hh, -hd}, {hw, hh, -hd}, {hw, hh, hd}}}, {1, 0, 0}, 3},
    }};

    // A soft shadow under the box: its outline, a little below.
    QPolygonF outline;
    for (const Face& face : faces) {
        QPolygonF quad;
        for (const Vec& corner : face.corners) {
            quad << project(corner);
        }
        outline = outline.united(quad);
    }
    p.setPen(Qt::NoPen);
    for (int spread = 1; spread <= 5; ++spread) {
        p.setBrush(QColor(0, 0, 0, 14));
        p.drawPolygon(outline.translated(0, 3.0 + spread));
    }

    // The light comes from the front, a little left and above.
    const qreal lx = -0.35, ly = -0.25, lz = 0.9;
    for (const Face& face : faces) {
        const Vec n = turn(face.normal);
        const Vec c = turn(Vec{(face.corners[0].x + face.corners[2].x) / 2, 0,
                               (face.corners[0].z + face.corners[2].z) / 2});
        // Facing the eye (at z = eye in front of the middle)?
        if (n.x * -c.x + n.y * -c.y + n.z * (eye - c.z) <= 0) {
            continue;
        }
        const QPolygonF quad{project(face.corners[0]), project(face.corners[1]),
                             project(face.corners[2]), project(face.corners[3])};
        const QPixmap picture_of_face = face.kind == 0   ? make_front()
                                        : face.kind == 1 ? make_back()
                                        : face.kind == 2 ? make_spine()
                                                         : make_edge();
        const QSizeF source_size =
            QSizeF(picture_of_face.size()) / picture_of_face.devicePixelRatio();
        const QRectF source{QPointF(0, 0), source_size};
        const QPolygonF from{source.topLeft(), source.topRight(), source.bottomRight(),
                             source.bottomLeft()};
        QTransform onto;
        if (!QTransform::quadToQuad(from, quad, onto)) {
            continue;
        }
        const qreal lit = std::clamp((n.x * lx + n.y * ly + n.z * lz) / 1.0, 0.0, 1.0);
        p.save();
        p.setTransform(onto, true);
        p.drawPixmap(source, picture_of_face, QRectF(picture_of_face.rect()));
        p.fillRect(source, QColor(0, 0, 0, static_cast<int>((1.0 - lit) * 110)));
        p.restore();
    }
}

namespace {
/// The back of a game card: the player's photos of a Switch 2 card (red) and a Switch 1 card
/// (dark grey), cut out to the card's shape.
QPixmap CartridgeBack(bool is_switch2) {
    const QString path = is_switch2 ? QStringLiteral(":/game_borders/cartridge_back_switch2.png")
                                    : QStringLiteral(":/game_borders/cartridge_back_switch1.png");
    QPixmap back;
    if (!QPixmapCache::find(path, &back)) {
        back.load(path);
        QPixmapCache::insert(path, back);
    }
    return back;
}
} // Anonymous namespace

void DrawCartridgeBox(QPainter& p, bool is_switch2, const QRectF& local, qreal dpr,
                      qreal angle_deg, const QPixmap& picture) {
    using Style = GameCard::Style;
    // The card itself, without the frame's clear margin.
    const QRectF body = GameCard::FrameBody(Style::Cartridge, local);
    const qreal box_w = body.width();
    const qreal box_h = body.height();
    if (box_w < 2 || box_h < 2) {
        return;
    }
    // A real game card is 21 x 31 x 3.3 mm: about a sixth as thick as it is wide.
    const qreal thick = box_w * 0.157;
    const qreal eye = box_w * 4.43;
    const qreal radius = GameCard::FrameRadius(Style::Cartridge, local.width());
    const qreal angle = angle_deg * 3.14159265358979 / 180.0;
    const qreal cs = std::cos(angle);
    const qreal sn = std::sin(angle);
    const QPointF centre = body.center();
    struct Vec {
        qreal x, y, z;
    };
    const auto turn = [&](const Vec& v) {
        return Vec{cs * v.x + sn * v.z, v.y, -sn * v.x + cs * v.z};
    };
    const auto project = [&](const Vec& v) {
        const Vec r = turn(v);
        const qreal f = eye / (eye - r.z);
        return QPointF(centre.x() + r.x * f, centre.y() + r.y * f);
    };
    const qreal hw = box_w / 2;
    const qreal hh = box_h / 2;
    const qreal hd = thick / 2;
    const QSizeF face_size{box_w, box_h};
    const QRectF whole{QPointF(0, 0), face_size};
    const QSize face_px{std::max(1, qRound(box_w * dpr)), std::max(1, qRound(box_h * dpr))};

    // A layer of the card at depth z (its picture's top left at the card's top left).
    const auto draw_layer = [&](const QPixmap& layer, qreal z, bool mirrored, int shade) {
        QPolygonF quad;
        if (mirrored) {
            quad << project({hw, -hh, z}) << project({-hw, -hh, z}) << project({-hw, hh, z})
                 << project({hw, hh, z});
        } else {
            quad << project({-hw, -hh, z}) << project({hw, -hh, z}) << project({hw, hh, z})
                 << project({-hw, hh, z});
        }
        const QPolygonF from{whole.topLeft(), whole.topRight(), whole.bottomRight(),
                             whole.bottomLeft()};
        QTransform onto;
        if (!QTransform::quadToQuad(from, quad, onto)) {
            return;
        }
        p.save();
        p.setTransform(onto, true);
        p.drawPixmap(whole, layer, QRectF(layer.rect()));
        if (shade > 0) {
            QPainterPath shape;
            shape.addRoundedRect(whole, radius, radius);
            p.fillPath(shape, QColor(0, 0, 0, shade));
        }
        p.restore();
    };

    // The faces' pictures.
    const auto make_front = [&]() {
        QPixmap face(face_px);
        face.setDevicePixelRatio(dpr);
        face.fill(Qt::transparent);
        QPainter fp(&face);
        fp.setRenderHint(QPainter::Antialiasing);
        fp.setRenderHint(QPainter::SmoothPixmapTransform);
        QPainterPath shape;
        shape.addRoundedRect(whole, radius, radius);
        fp.setClipPath(shape);
        const QRectF frame_rect = local.translated(-body.topLeft());
        const QPixmap frame = GameCard::CartridgePixmap(qRound(frame_rect.height()), dpr, is_switch2);
        fp.drawPixmap(frame_rect, frame, QRectF(frame.rect()));
        const QRectF label = GameCard::CartridgeLabelRect(frame_rect);
        QPainterPath label_clip;
        label_clip.addRoundedRect(label, label.width() * 0.04, label.width() * 0.04);
        fp.setClipPath(label_clip, Qt::IntersectClip);
        if (!picture.isNull()) {
            GameCard::DrawCoverCropped(fp, picture, label);
        } else {
            fp.fillRect(label, QColor(0x22, 0x1C, 0x18));
        }
        return face;
    };
    const auto make_back = [&]() {
        QPixmap face(face_px);
        face.setDevicePixelRatio(dpr);
        face.fill(Qt::transparent);
        QPainter fp(&face);
        fp.setRenderHint(QPainter::Antialiasing);
        fp.setRenderHint(QPainter::SmoothPixmapTransform);
        const QPixmap back = CartridgeBack(is_switch2);
        if (!back.isNull()) {
            fp.drawPixmap(whole, back, QRectF(back.rect()));
        } else {
            fp.setPen(Qt::NoPen);
            fp.setBrush(is_switch2 ? QColor(0xD8, 0x26, 0x1A) : QColor(0x35, 0x3B, 0x3C));
            fp.drawRoundedRect(whole, radius, radius);
        }
        return face;
    };
    // One slice of the edge: the card's outline in its plastic. Drawn small (only its rim shows
    // past the slices in front of it), and scaled up onto the layer.
    const QColor edge = is_switch2 ? QColor(0xB4, 0x1A, 0x10) : QColor(0x28, 0x2D, 0x2E);
    QPixmap slice(QSize(std::max(1, qRound(box_w * dpr * 0.5)), std::max(1, qRound(box_h * dpr * 0.5))));
    slice.fill(Qt::transparent);
    {
        QPainter sp(&slice);
        sp.setRenderHint(QPainter::Antialiasing);
        const QRectF r{QPointF(0, 0), QSizeF(slice.size())};
        QLinearGradient fill{r.topLeft(), r.bottomLeft()};
        fill.setColorAt(0.0, edge.lighter(122));
        fill.setColorAt(0.5, edge);
        fill.setColorAt(1.0, edge.darker(125));
        sp.setPen(Qt::NoPen);
        sp.setBrush(fill);
        const qreal rr = radius * dpr * 0.5;
        sp.drawRoundedRect(r, rr, rr);
    }

    // A soft shadow under it: its outline, a little below.
    QPolygonF outline;
    for (const qreal z : {-hd, hd}) {
        outline = outline.united(QPolygonF{project({-hw, -hh, z}), project({hw, -hh, z}),
                                           project({hw, hh, z}), project({-hw, hh, z})});
    }
    p.setPen(Qt::NoPen);
    for (int spread = 1; spread <= 5; ++spread) {
        p.setBrush(QColor(0, 0, 0, 14));
        p.drawPolygon(outline.translated(0, 3.0 + spread));
    }

    // Which face is towards the eye (at z = eye in front of the middle)?
    const Vec front_normal = turn({0, 0, 1});
    const Vec front_centre = turn({0, 0, hd});
    const bool front_shows = front_normal.x * -front_centre.x + front_normal.z * (eye - front_centre.z) > 0;
    // The light from the front, a little left: a face turned away from it darker; the edge
    // lighter on the side the light reaches.
    const qreal lit = std::clamp(std::abs(cs) * 0.85 + 0.15 - sn * 0.12 * (front_shows ? 1 : -1),
                                 0.0, 1.0);
    const int edge_shade = static_cast<int>(std::clamp(0.35 - sn * 0.25, 0.05, 0.6) * 255 * 0.6);
    const int steps = std::clamp(static_cast<int>(thick * std::abs(sn) / 1.2), 2, 28);
    for (int i = 0; i <= steps; ++i) {
        // From the far face to the near one.
        const qreal t = static_cast<qreal>(i) / steps;
        const qreal z = front_shows ? -hd + thick * t : hd - thick * t;
        draw_layer(slice, z * 0.98, false, edge_shade);
    }
    if (front_shows) {
        draw_layer(make_front(), hd, false, static_cast<int>((1.0 - lit) * 120));
    } else {
        draw_layer(make_back(), -hd, true, static_cast<int>((1.0 - lit) * 120));
    }
}
