// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QGuiApplication>
#include <QImageReader>
#include <QPainter>
#include <QPainterPath>
#include <QPixmapCache>
#include "game_card.h"
#include "phoenix/util/phoenix_theme.h"
#include "qt_common/config/uisettings.h"
#include "qt_common/game_list/game_list_p.h"

namespace {
// PhoenixEmu's colours (dist/qt_themes/phoenix/style.qss).
const QColor EmberBase{0xFF, 0xC8, 0x3D};
const QColor EmberDeepBase{0xE8, 0x9E, 0x10};
const QColor CreamBase{0xFF, 0xF4, 0xDA};
/// How far the home tiles sit in from their cells' centre (room for the first one's glow).
constexpr int HomeInset = 28;
} // Anonymous namespace

GameCard::GameCard(QObject* parent) : QStyledItemDelegate{parent} {
    setObjectName("GameCard");
}

GameCard::Style GameCard::CurrentStyle() {
    return static_cast<Style>(std::min<u32>(UISettings::values.game_card_style.GetValue(), 3));
}

QPixmap GameCard::PlatformBorderPixmap(bool is_switch2, int size, qreal dpr) {
    // Scaling the 1024 px frame on every paint is far too slow for a moving view: once per size.
    const QString key = QStringLiteral("phoenix-border-%1-%2-%3")
                            .arg(is_switch2 ? 2 : 1)
                            .arg(size)
                            .arg(dpr);
    QPixmap border;
    if (QPixmapCache::find(key, &border)) {
        return border;
    }
    border.load(is_switch2 ? QStringLiteral(":/game_borders/switch2.png")
                           : QStringLiteral(":/game_borders/switch1.png"));
    if (border.isNull()) {
        return border;
    }
    const int pixels = std::max(1, qRound(size * dpr));
    border = border.scaled(pixels, pixels, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    border.setDevicePixelRatio(dpr);
    QPixmapCache::insert(key, border);
    return border;
}

QPixmap GameCard::CartridgePixmap(int height_px, qreal dpr, bool is_switch2) {
    // In 32 px steps, so the picked tile's spring animation reuses a few renders of the SVG.
    const int bucket = std::max(32, (std::max(1, height_px) + 31) / 32 * 32);
    const QString key = QStringLiteral("phoenix-cartridge-%1-%2-%3")
                            .arg(bucket)
                            .arg(dpr)
                            .arg(is_switch2 ? 2 : 1);
    QPixmap cartridge;
    if (QPixmapCache::find(key, &cartridge)) {
        return cartridge;
    }
    QImageReader reader(QStringLiteral(":/game_borders/game_cartridge.svg"));
    reader.setScaledSize(QSize(qRound(bucket * CartridgeAspect * dpr), qRound(bucket * dpr)));
    QImage image = reader.read();
    if (image.isNull()) {
        return {};
    }
    if (!is_switch2) {
        // Switch 1 game cards are dark grey plastic: every red pixel (the body, and the red band
        // and corners of the label that show around the art's rounded corners) turned grey, its
        // shading kept.
        image = image.convertToFormat(QImage::Format_ARGB32);
        for (int y = 0; y < image.height(); ++y) {
            auto* const row = reinterpret_cast<QRgb*>(image.scanLine(y));
            for (int x = 0; x < image.width(); ++x) {
                const QRgb c = row[x];
                const int r = qRed(c), g = qGreen(c), b = qBlue(c);
                if (qAlpha(c) == 0 || r < g + 40 || r < b + 40) {
                    continue; // not red: the label's black, highlights, the clear margin
                }
                const int shade = std::clamp(28 + r * 70 / 255, 0, 255);
                row[x] = qRgba(shade, shade, shade + 4, qAlpha(c));
            }
        }
    }
    cartridge = QPixmap::fromImage(image);
    QPixmapCache::insert(key, cartridge);
    return cartridge;
}

QPixmap GameCard::CasePixmap(int height_px, qreal dpr, bool is_switch2, bool band_on) {
    const int bucket = std::max(32, (std::max(1, height_px) + 31) / 32 * 32);
    const QString key = QStringLiteral("phoenix-case-%1-%2-%3-%4")
                            .arg(bucket)
                            .arg(dpr)
                            .arg(is_switch2 ? 2 : 1)
                            .arg(band_on ? 1 : 0);
    QPixmap cached;
    if (QPixmapCache::find(key, &cached)) {
        return cached;
    }
    const QSize size{qRound(bucket * CaseAspect * dpr), qRound(bucket * dpr)};
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.scale(size.width() / CaseBase.width(), size.height() / CaseBase.height());

    // The plastic case, a little lighter towards the opening edge: Switch 2's red, Switch 1's
    // clear (as in the player's case layout photo).
    const QRectF body{1.5, 1.5, 253.0, 397.0};
    QLinearGradient plastic{body.topLeft(), body.topRight()};
    if (is_switch2) {
        plastic.setColorAt(0.0, QColor(0xB8, 0x00, 0x0C));
        plastic.setColorAt(0.12, QColor(0xD4, 0x05, 0x14));
        plastic.setColorAt(1.0, QColor(0xE8, 0x1C, 0x26));
    } else {
        plastic.setColorAt(0.0, QColor(0xC9, 0xCD, 0xD3, 240));
        plastic.setColorAt(0.12, QColor(0xE4, 0xE7, 0xEB, 240));
        plastic.setColorAt(1.0, QColor(0xF4, 0xF6, 0xF8, 240));
    }
    p.setPen(QPen(QColor(255, 255, 255, 70), 1.2));
    p.setBrush(plastic);
    p.drawRoundedRect(body, 12, 12);

    if (!band_on) {
        // The player's case art: a thin rim round the whole cover, no spine strip on the front.
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0xF4, 0xF1, 0xEC));
        p.drawRoundedRect(CaseArtWindow, 6, 6);
        p.end();
        const QPixmap plain = QPixmap::fromImage(image);
        QPixmapCache::insert(key, plain);
        return plain;
    }
    // The spine: a darker strip with two moulded ridges.
    QLinearGradient spine{QPointF(2, 0), QPointF(19, 0)};
    if (is_switch2) {
        spine.setColorAt(0.0, QColor(0x8E, 0x00, 0x09));
        spine.setColorAt(0.6, QColor(0xB0, 0x02, 0x0E));
        spine.setColorAt(1.0, QColor(0x9A, 0x00, 0x0A));
    } else {
        spine.setColorAt(0.0, QColor(0xA9, 0xAE, 0xB5));
        spine.setColorAt(0.6, QColor(0xC8, 0xCC, 0xD2));
        spine.setColorAt(1.0, QColor(0xB4, 0xB9, 0xC0));
    }
    p.setPen(Qt::NoPen);
    p.setBrush(spine);
    QPainterPath spine_path;
    spine_path.addRoundedRect(QRectF(2, 2, 17, 396), 11, 11);
    p.drawPath(spine_path.intersected([] {
        QPainterPath clip;
        clip.addRect(QRectF(0, 0, 19, 400));
        return clip;
    }()));
    p.setPen(QPen(QColor(255, 255, 255, 40), 1.0));
    p.drawLine(QPointF(8, 16), QPointF(8, 384));
    p.drawLine(QPointF(13, 16), QPointF(13, 384));

    // The cover sheet behind the clear sleeve, and its header band.
    const QRectF window = CaseWindow;
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0xF4, 0xF1, 0xEC));
    p.drawRoundedRect(window, 4, 4);
    const QRectF band{window.left(), window.top(), window.width(), CaseCover.top() - window.top()};
    QPainterPath band_path;
    band_path.addRoundedRect(band.adjusted(0, 0, 0, 4), 4, 4);
    p.setClipRect(band);
    if (is_switch2) {
        // Switch 2: a dark band with a red line under it.
        p.fillPath(band_path, QColor(0x1E, 0x1E, 0x22));
        p.fillRect(QRectF(band.left(), band.bottom() - 3, band.width(), 3), QColor(0xE6, 0x00, 0x12));
    } else {
        p.fillPath(band_path, QColor(0xE6, 0x00, 0x12));
    }
    p.setClipping(false);
    QFont font = QGuiApplication::font();
    font.setPixelSize(13);
    font.setBold(true);
    font.setLetterSpacing(QFont::AbsoluteSpacing, 1.2);
    p.setFont(font);
    p.setPen(Qt::white);
    p.drawText(band.adjusted(10, 0, -8, is_switch2 ? -3 : 0), Qt::AlignVCenter | Qt::AlignLeft,
               is_switch2 ? QStringLiteral("NINTENDO SWITCH 2")
                          : QStringLiteral("NINTENDO SWITCH"));

    // The sleeve's sheen across the top of the cover.
    QLinearGradient sheen{window.topLeft(), QPointF(window.left() + 90, window.top() + 140)};
    sheen.setColorAt(0.0, QColor(255, 255, 255, 46));
    sheen.setColorAt(1.0, QColor(255, 255, 255, 0));
    p.fillRect(band, sheen);
    p.end();

    const QPixmap result = QPixmap::fromImage(image);
    QPixmapCache::insert(key, result);
    return result;
}

QRectF GameCard::CaseWindowRect(const QRectF& case_rect) {
    const qreal sx = case_rect.width() / CaseBase.width();
    const qreal sy = case_rect.height() / CaseBase.height();
    return {case_rect.left() + CaseWindow.x() * sx, case_rect.top() + CaseWindow.y() * sy,
            CaseWindow.width() * sx, CaseWindow.height() * sy};
}

QRectF GameCard::CaseArtWindowRect(const QRectF& case_rect) {
    const qreal sx = case_rect.width() / CaseBase.width();
    const qreal sy = case_rect.height() / CaseBase.height();
    return {case_rect.left() + CaseArtWindow.x() * sx, case_rect.top() + CaseArtWindow.y() * sy,
            CaseArtWindow.width() * sx, CaseArtWindow.height() * sy};
}

void GameCard::CaseParts(const QModelIndex& index, const QRectF& case_rect, int height_px,
                         qreal dpr, QPixmap& frame, QRectF& art_rect, QPixmap& art) {
    const bool is_switch2 = index.data(GameListItemPath::Switch2Role).toBool();
    const QPixmap front = index.data(GameListItemPath::CaseFrontRole).value<QPixmap>();
    if (!front.isNull()) {
        frame = CasePixmap(height_px, dpr, is_switch2, false);
        art_rect = CaseArtWindowRect(case_rect);
        art = front;
        return;
    }
    frame = CasePixmap(height_px, dpr, is_switch2, true);
    art_rect = CaseCoverRect(case_rect);
}

QRectF GameCard::CaseCoverRect(const QRectF& case_rect) {
    const qreal sx = case_rect.width() / CaseBase.width();
    const qreal sy = case_rect.height() / CaseBase.height();
    return {case_rect.left() + CaseCover.x() * sx, case_rect.top() + CaseCover.y() * sy,
            CaseCover.width() * sx, CaseCover.height() * sy};
}

QRectF GameCard::CartridgeLabelRect(const QRectF& cartridge_rect) {
    const qreal sx = cartridge_rect.width() / CartridgeBase.width();
    const qreal sy = cartridge_rect.height() / CartridgeBase.height();
    return {cartridge_rect.left() + CartridgeLabel.x() * sx,
            cartridge_rect.top() + CartridgeLabel.y() * sy, CartridgeLabel.width() * sx,
            CartridgeLabel.height() * sy};
}

void GameCard::DrawCoverCropped(QPainter& painter, const QPixmap& pixmap, const QRectF& target) {
    if (pixmap.isNull() || target.isEmpty()) {
        return;
    }
    const qreal scale =
        std::max(target.width() / pixmap.width(), target.height() / pixmap.height());
    const qreal src_w = target.width() / scale;
    const qreal src_h = target.height() / scale;
    painter.drawPixmap(target, pixmap,
                       QRectF((pixmap.width() - src_w) / 2, (pixmap.height() - src_h) / 2, src_w,
                              src_h));
}

void GameCard::paint(QPainter* painter, const QStyleOptionViewItem& option,
                     const QModelIndex& index) const {
    if (!index.isValid())
        return;
    if (m_columns < 1) {
        PaintHomeTile(painter, option, index);
        return;
    }

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    constexpr int cardCornerRadius = 14;

    const QRect cardRect = getCardRect(option, index);

    // Cartridges sit in the grid's case (GameGrid paints its slots): no card panel behind them.
    const bool in_case = IsFramed(CurrentStyle());
    QPalette palette = option.palette;
    QColor backgroundColor = palette.window().color();
    QColor borderColor = palette.dark().color();
    QColor textColor = in_case ? QColor(0xF2, 0xEE, 0xE6) : palette.text().color();

    // highlight on select or hover: an ember outline, as on the home view
    QPen borderPen{borderColor, 1};
    if (option.state & QStyle::State_Selected) {
        backgroundColor = backgroundColor.lighter(125);
        borderPen = QPen(PhoenixTheme::Recolor(EmberBase), 3);
        textColor = PhoenixTheme::Recolor(CreamBase);
    } else if (option.state & QStyle::State_MouseOver) {
        backgroundColor = backgroundColor.lighter(120);
        borderPen = QPen(PhoenixTheme::Recolor(EmberBase).darker(130), 1);
    }

    if (!in_case) {
        painter->setBrush(backgroundColor);
        painter->setPen(borderPen);
        painter->drawRoundedRect(cardRect, cardCornerRadius, cardCornerRadius);
    }

    if (!in_case &&
        ((option.state & QStyle::State_Selected) || (option.state & QStyle::State_MouseOver))) {
        QLinearGradient cardSheen{cardRect.topLeft(),
                                  QPointF(cardRect.left(), cardRect.top() + cardRect.height() * 0.45)};
        cardSheen.setColorAt(0.0, QColor(255, 255, 255, (option.state & QStyle::State_Selected) ? 45 : 25));
        cardSheen.setColorAt(1.0, QColor(255, 255, 255, 0));
        painter->save();
        QPainterPath cardClip;
        cardClip.addRoundedRect(cardRect, cardCornerRadius, cardCornerRadius);
        painter->setClipPath(cardClip);
        painter->fillRect(cardRect, cardSheen);
        painter->restore();
    }

    const u32 icon_size = UISettings::values.game_icon_size.GetValue();
    QPixmap icon_pixmap = index.data(Qt::DecorationRole).value<QPixmap>();

    QRect iconRect;
    const Style style = CurrentStyle();
    const qreal dpr = painter->device() ? painter->device()->devicePixelRatioF() : 1.0;
    if (!icon_pixmap.isNull() && IsFramed(style)) {
        // The portrait cartridge or case replaces the square icon; the art sits in its label.
        const int cart_w = qRound(icon_size * FrameAspect(style));
        iconRect = {cardRect.left() + (cardRect.width() - cart_w) / 2,
                    cardRect.top() + cardMargin - 1, cart_w, static_cast<int>(icon_size)};
        painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
        QPixmap frame;
        QRectF label;
        QPixmap art = icon_pixmap;
        if (style == Style::Case) {
            CaseParts(index, QRectF(iconRect), static_cast<int>(icon_size), dpr, frame, label, art);
        } else {
            frame = FramePixmap(style, static_cast<int>(icon_size), dpr,
                                index.data(GameListItemPath::Switch2Role).toBool());
            label = FrameArtRect(style, QRectF(iconRect));
        }
        painter->drawPixmap(iconRect, frame);
        painter->save();
        QPainterPath label_clip;
        label_clip.addRoundedRect(label, label.width() * 0.04, label.width() * 0.04);
        painter->setClipPath(label_clip);
        DrawCoverCropped(*painter, art, label);
        painter->restore();
        if (option.state & (QStyle::State_Selected | QStyle::State_MouseOver)) {
            // Picked out of its slot: an outline in the accent colour.
            const bool selected = (option.state & QStyle::State_Selected) != 0;
            painter->setBrush(Qt::NoBrush);
            QColor ring = PhoenixTheme::Recolor(EmberBase);
            ring.setAlpha(selected ? 255 : 140);
            painter->setPen(QPen(ring, selected ? 3 : 2));
            painter->drawRoundedRect(QRectF(iconRect).adjusted(-3, -3, 3, 3), 12, 12);
        }
    } else if (!icon_pixmap.isNull()) {
        QSize scaled = icon_pixmap.size();
        scaled.scale(icon_size, icon_size, Qt::KeepAspectRatio);

        iconRect = {cardRect.left() + (cardRect.width() - scaled.width()) / 2,
                    cardRect.top() + cardMargin - 1, scaled.width(), scaled.height()};

        painter->setRenderHint(QPainter::SmoothPixmapTransform, true);

        painter->save();
        QPainterPath clip_path;
        clip_path.addRoundedRect(iconRect, cardCornerRadius, cardCornerRadius);
        painter->setClipPath(clip_path);
        painter->drawPixmap(iconRect, icon_pixmap);
        painter->restore();
        if (style == Style::Badge) {
            const QPixmap border = PlatformBorderPixmap(
                index.data(GameListItemPath::Switch2Role).toBool(), iconRect.width(), dpr);
            painter->drawPixmap(iconRect, border);
        }
    } else {
        iconRect = {cardRect.left() + cardMargin, cardRect.top() + cardMargin,
                    static_cast<int>(icon_size), static_cast<int>(icon_size)};
    }

    if (UISettings::values.show_game_name.GetValue()) {
        QRect textRect = cardRect;
        textRect.setTop(iconRect.bottom() + cardMargin);
        textRect.adjust(cardMargin, 0, -cardMargin, -cardMargin);

        QString title = index.data(Qt::DisplayRole).toString().split(QLatin1Char('\n')).first();

        painter->setPen(textColor);
        QFont font = option.font;
        font.setBold(true);
        font.setPixelSize(std::max(11.0, std::sqrt(static_cast<double>(icon_size))));
        painter->setFont(font);

        painter->drawText(textRect, Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, title);
    }

    painter->restore();
}

void GameCard::PaintHomeTile(QPainter* painter, const QStyleOptionViewItem& option,
                             const QModelIndex& index) const {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setRenderHint(QPainter::SmoothPixmapTransform);

    const bool selected = (option.state & QStyle::State_Selected) != 0;
    const bool hovered = (option.state & QStyle::State_MouseOver) != 0;
    const QRect cell = option.rect;
    const int tile = m_contentSize.width();
    // The tile sits a little above the middle; the name goes under it.
    // Shifted in from the view's left edge (the first tile's glow and name need the room).
    QRect tile_rect{cell.center().x() - tile / 2 + HomeInset, cell.top() + (cell.height() - tile) / 2 - 16,
                    tile, tile};
    if (selected) {
        // Lifted towards the player, like a picked card (springs up when picked).
        const int grow = static_cast<int>(tile / 20.0 * m_lift);
        tile_rect.adjust(-grow, -grow - grow / 2, grow, grow - grow / 2);
    } else if (hovered) {
        tile_rect.translate(0, -3);
    }
    const Style style = CurrentStyle();
    const bool cartridge = IsFramed(style);
    if (cartridge) {
        const int width = qRound(tile_rect.height() * FrameAspect(style));
        tile_rect = QRect(tile_rect.center().x() - width / 2, tile_rect.top(), width,
                          tile_rect.height());
    }
    const qreal radius = tile_rect.width() * (cartridge ? 0.08 : 0.11);
    const qreal dpr = painter->device() ? painter->device()->devicePixelRatioF() : 1.0;

    // A soft shadow under the tile.
    painter->setPen(Qt::NoPen);
    for (int i = 3; i >= 1; --i) {
        painter->setBrush(QColor(0, 0, 0, 30));
        painter->drawRoundedRect(QRectF(tile_rect).translated(0, 3 + 3 * i).adjusted(-i, -i, i, i),
                                 radius + i, radius + i);
    }

    // The selected tile glows ember.
    if (selected) {
        for (int i = 8; i >= 1; --i) {
            QColor glow = PhoenixTheme::Recolor(EmberBase);
            glow.setAlpha(static_cast<int>((6 + (8 - i) * 4) * m_lift));
            painter->setPen(QPen(glow, 2));
            painter->setBrush(Qt::NoBrush);
            const qreal spread = 3.0 + i * 2.2;
            painter->drawRoundedRect(QRectF(tile_rect).adjusted(-spread, -spread, spread, spread),
                                     radius + spread, radius + spread);
        }
    }

    // A cartridge frame goes under the art; the art then fills its label.
    QRect art_rect = tile_rect;
    qreal art_radius = radius;
    QPixmap picture = index.data(GameListItemPath::LargePictureRole).value<QPixmap>();
    if (picture.isNull()) {
        picture = index.data(Qt::DecorationRole).value<QPixmap>();
    }
    if (cartridge) {
        QPixmap frame;
        QRectF art_area;
        if (style == Style::Case) {
            CaseParts(index, QRectF(tile_rect), tile_rect.height(), dpr, frame, art_area, picture);
        } else {
            frame = FramePixmap(style, tile_rect.height(), dpr,
                                index.data(GameListItemPath::Switch2Role).toBool());
            art_area = FrameArtRect(style, QRectF(tile_rect));
        }
        if (!frame.isNull()) {
            painter->drawPixmap(tile_rect, frame);
        }
        art_rect = art_area.toRect();
        art_radius = art_rect.width() * 0.04;
    }

    // The picture fills the tile (cropped to a square), or an ember tile with the initials.
    QPainterPath clip;
    clip.addRoundedRect(QRectF(art_rect), art_radius, art_radius);
    painter->setClipPath(clip);
    const QString title =
        index.data(Qt::DisplayRole).toString().split(QLatin1Char('\n')).first();
    if (!picture.isNull()) {
        const QSize target = art_rect.size() * dpr;
        // Scaled once per picture and size (this ran every frame of the moving home view).
        const QString key = QStringLiteral("%1-%2x%3")
                                .arg(picture.cacheKey())
                                .arg(target.width())
                                .arg(target.height());
        auto cached = m_scaled.find(key);
        if (cached == m_scaled.end()) {
            if (m_scaled.size() > 96) {
                m_scaled.clear();
            }
            cached = m_scaled.insert(key, picture.scaled(target, Qt::KeepAspectRatioByExpanding,
                                                         Qt::SmoothTransformation));
        }
        const QPixmap& scaled = cached.value();
        const QRect source{(scaled.width() - target.width()) / 2,
                           (scaled.height() - target.height()) / 2, target.width(),
                           target.height()};
        painter->drawPixmap(art_rect, scaled, source);
    } else {
        QLinearGradient fill{art_rect.topLeft(), art_rect.bottomRight()};
        fill.setColorAt(0, PhoenixTheme::Recolor(EmberBase));
        fill.setColorAt(1, PhoenixTheme::Recolor(EmberDeepBase));
        painter->fillRect(art_rect, fill);
        QFont font = option.font;
        font.setBold(true);
        font.setPixelSize(art_rect.height() / 4);
        painter->setFont(font);
        painter->setPen(PhoenixTheme::Recolor(QColor(0x3A, 0x26, 0x00)));
        painter->drawText(art_rect, Qt::AlignCenter, title.left(2).toUpper());
    }
    // Glassmorphic specular sheen across the upper area
    QLinearGradient sheen{art_rect.topLeft(),
                          QPointF(art_rect.left(), art_rect.top() + art_rect.height() * 0.42)};
    sheen.setColorAt(0.0, QColor(255, 255, 255, selected ? 65 : (hovered ? 35 : 18)));
    sheen.setColorAt(1.0, QColor(255, 255, 255, 0));
    painter->fillRect(art_rect, sheen);
    painter->setClipping(false);

    // The badge frame (its middle is clear) sits on the art and is the tile's outline.
    if (style == Style::Badge) {
        painter->drawPixmap(tile_rect,
                            PlatformBorderPixmap(index.data(GameListItemPath::Switch2Role).toBool(),
                                                 tile_rect.width(), dpr));
    }
    if (!selected) {
        // The others step back a little.
        QPainterPath dim;
        dim.addRoundedRect(QRectF(cartridge ? tile_rect : art_rect), radius, radius);
        painter->fillPath(dim, QColor(10, 8, 4, hovered ? 25 : 80));
    }

    // Outline: thick ember when selected, a soft cream line on hover. A badge or cartridge frame
    // is its own outline.
    painter->setBrush(Qt::NoBrush);
    if (style == Style::Plain && selected) {
        painter->setPen(QPen(PhoenixTheme::Recolor(EmberBase), std::max(3, tile / 70)));
        painter->drawRoundedRect(QRectF(tile_rect), radius, radius);
        // Inner luminous glass rim
        painter->setPen(QPen(QColor(255, 255, 255, 110), 1));
        painter->drawRoundedRect(QRectF(tile_rect).adjusted(1.5, 1.5, -1.5, -1.5), radius - 1.0, radius - 1.0);
    } else if (style == Style::Plain && hovered) {
        painter->setPen(QPen(PhoenixTheme::Recolor(QColor(0xFF, 0xF4, 0xDA, 170)), 2));
        painter->drawRoundedRect(QRectF(tile_rect), radius, radius);
    } else if (style == Style::Plain) {
        painter->setPen(QPen(PhoenixTheme::Recolor(QColor(0xFF, 0xFF, 0xFF, 28)), 1));
        painter->drawRoundedRect(QRectF(tile_rect), radius, radius);
    }

    // The selected game's name, in a little pill under it.
    if (selected && UISettings::values.show_game_name.GetValue()) {
        QFont font = option.font;
        font.setBold(true);
        font.setPixelSize(std::clamp(tile / 13, 13, 20));
        painter->setFont(font);
        const QFontMetrics metrics{font};
        const int max_width = cell.width() * 2;
        const QString shown = metrics.elidedText(title, Qt::ElideRight, max_width);
        const int text_width = metrics.horizontalAdvance(shown);
        const QRect pill{tile_rect.center().x() - text_width / 2 - 16, tile_rect.bottom() + 18,
                         text_width + 32, metrics.height() + 12};
        painter->setPen(QPen(PhoenixTheme::Recolor(EmberBase).darker(150), 1));
        painter->setBrush(PhoenixTheme::Recolor(QColor(0x20, 0x16, 0x10, 235)));
        painter->drawRoundedRect(QRectF(pill), pill.height() / 2.0, pill.height() / 2.0);

        // Glass highlight on upper half of the pill
        painter->save();
        QPainterPath pill_clip;
        pill_clip.addRoundedRect(QRectF(pill), pill.height() / 2.0, pill.height() / 2.0);
        painter->setClipPath(pill_clip);
        QLinearGradient pill_sheen{pill.topLeft(), QPointF(pill.left(), pill.center().y())};
        pill_sheen.setColorAt(0, QColor(255, 255, 255, 35));
        pill_sheen.setColorAt(1, QColor(255, 255, 255, 0));
        painter->fillRect(pill, pill_sheen);
        painter->restore();

        painter->setPen(PhoenixTheme::Recolor(CreamBase));
        painter->drawText(pill, Qt::AlignCenter, shown);
    }
    painter->restore();
}

QRect GameCard::getCardRect(const QStyleOptionViewItem& option, const QModelIndex& index) const {
    const int cell_width = option.rect.width();

    const int card_width = cell_width - m_padding;

    int card_left, card_top, card_height;

    if (m_columns >= 1) {
        // grid mode

        // center everything in-line, such that the leftmost and rightmost cards
        // have ~ equal padding to the edge of the viewport
        // spacing between each card is larger, but equal to each other
        const int column = index.row() % m_columns;

        const int row_width = m_columns * cell_width;
        const int total_gap = row_width - cardMargin * 2 - m_columns * card_width;
        const int gap = (m_columns > 1) ? (total_gap / (m_columns - 1)) : 0;
        card_left =
            option.rect.left() - column * cell_width + cardMargin + column * (card_width + gap) + 4;

        // fill cell vertically
        card_top = option.rect.top() + cardMargin;
        card_height = option.rect.height() - cardMargin - 1;
    } else {
        // carousel mode: the tile (PaintHomeTile)
        const int tile = m_contentSize.width();
        const int width =
            IsFramed(CurrentStyle()) ? qRound(tile * FrameAspect(CurrentStyle())) : tile;
        return QRect(option.rect.center().x() - width / 2 + HomeInset,
                     option.rect.top() + (option.rect.height() - tile) / 2 - 16, width, tile);
        card_left = option.rect.left() + cardMargin + 4;

        // the delegate itself takes up the full height, but the card itself
        // gets centered
        const int content_height = m_contentSize.height() - cardMargin;
        const int cell_height = option.rect.height();

        card_height = std::min(content_height, cell_height - cardMargin * 2) - 1;
        card_top = option.rect.top() + (cell_height - card_height) / 2;
    }

    return QRect(card_left, card_top, card_width - cardMargin, card_height);
}

bool GameCard::hitTest(const QPoint& point, const QModelIndex& index, const QWidget* widget,
                       const QRect& cellRect) const {
    QStyleOptionViewItem option;
    option.initFrom(widget);
    option.rect = cellRect;
    return getCardRect(option, index).contains(point);
}

QSize GameCard::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const {
    return m_size;
}

void GameCard::setSize(const QSize& newSize, const QSize& contentSize, const int padding,
                       const int columns) {
    m_size = newSize;
    m_contentSize = contentSize;
    m_padding = padding;
    m_columns = columns;
}
