// SPDX-FileCopyrightText: 2026 PhoenixEmu contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <tuple>
#include <vector>

#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QRadialGradient>
#include <QRandomGenerator>

#include "common/common_types.h"
#include "phoenix/util/phoenix_theme.h"
#include "qt_common/config/uisettings.h"

/// PhoenixEmu's moving backdrop for the home view and the loading screen: a warm dark base, the
/// picked game's picture blurred into an ambient glow, and cozy sunbeams with dust motes.
namespace PhoenixBackdrop {

/// A blurred copy of `picture` covering `size` (cropped to its shape, never stretched): made
/// small, blurred there, then scaled up once. Rebuild it when the size changes. `soft` keeps
/// more of the picture (a banner background) instead of a glow of its colours.
/// soft: the "picked game's picture as the background" look, close to the picture. banner: the
/// picture is the game's real banner, so it is shown nearly as it is (lightly softened), not
/// turned into a glow of its colours as a square picture blown up is.
inline QPixmap MakeAmbient(const QPixmap& picture, QSize size, bool soft = false,
                           bool banner = false) {
    if (picture.isNull() || size.isEmpty()) {
        return {};
    }
    // Cover: crop the picture to the target's aspect ratio.
    const qreal target_aspect = qreal(size.width()) / size.height();
    QRect crop = picture.rect();
    if (qreal(crop.width()) / crop.height() > target_aspect) {
        const int w = int(crop.height() * target_aspect);
        crop = {(crop.width() - w) / 2, 0, w, crop.height()};
    } else {
        const int h = int(crop.width() / target_aspect);
        crop = {0, (crop.height() - h) / 2, crop.width(), h};
    }
    const int divisor = banner && soft ? 2 : banner ? 4 : soft ? 6 : 10;
    const QSize small{std::max(8, size.width() / divisor), std::max(8, size.height() / divisor)};
    QImage image = picture.copy(crop)
                       .scaled(small, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                       .toImage()
                       .convertToFormat(QImage::Format_ARGB32);
    // Three box blur passes (close to a Gaussian), each across then down.
    const int w = image.width();
    const int h = image.height();
    const int radius = banner && soft ? 1 : soft ? 2 : 3;
    std::vector<QRgb> line(std::max(w, h));
    for (int pass = 0; pass < 3; ++pass) {
        for (int y = 0; y < h; ++y) {
            auto* const row = reinterpret_cast<QRgb*>(image.scanLine(y));
            for (int x = 0; x < w; ++x) {
                int r = 0, g = 0, b = 0, n = 0;
                for (int k = std::max(0, x - radius); k <= std::min(w - 1, x + radius); ++k) {
                    r += qRed(row[k]);
                    g += qGreen(row[k]);
                    b += qBlue(row[k]);
                    ++n;
                }
                line[x] = qRgb(r / n, g / n, b / n);
            }
            std::copy(line.begin(), line.begin() + w, row);
        }
        for (int x = 0; x < w; ++x) {
            for (int y = 0; y < h; ++y) {
                int r = 0, g = 0, b = 0, n = 0;
                for (int k = std::max(0, y - radius); k <= std::min(h - 1, y + radius); ++k) {
                    const QRgb c = reinterpret_cast<const QRgb*>(image.constScanLine(k))[x];
                    r += qRed(c);
                    g += qGreen(c);
                    b += qBlue(c);
                    ++n;
                }
                line[y] = qRgb(r / n, g / n, b / n);
            }
            for (int y = 0; y < h; ++y) {
                reinterpret_cast<QRgb*>(image.scanLine(y))[x] = line[y];
            }
        }
    }
    return QPixmap::fromImage(
        image.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
}

/// The moving parts: sunbeams slanting in from the top-left corner, swaying and breathing
/// slowly, and dust motes drifting lazily that light up as they float through a beam. Step it
/// every frame, then Paint.
class Scene {
public:
    /// Customize's choices for the background and its animation (UISettings::home_bg_* and
    /// home_anim_*).
    struct Options {
        /// Theme is the theme's warm gradient (Customize > Home background > Theme); whether a
        /// background moves is `animate`, for every kind.
        enum class Kind { Theme = 0, Picture = 1, Solid = 2, Gradient = 3 };
        enum class Particles { None = 0, Dust = 1, Embers = 2, Both = 3 };
        enum class Motion { Natural = 0, Rise = 1, Fall = 2, Swirl = 3 };
        Kind kind = Kind::Theme;
        QString picture;
        QColor color{0x2E, 0x0C, 0x08};
        QColor color2{0x0F, 0x08, 0x06};
        int angle = 135;
        bool animate = true;
        bool rays = true;
        Particles particles = Particles::Both;
        Motion motion = Motion::Natural;
        int amount = 100; ///< Percent of the usual number of particles
        int speed = 100;  ///< Percent of the usual pace
        bool game_glow = true;
        bool pulse = true;
        bool vignette = true;
        bool operator==(const Options&) const = default;

        static Options FromSettings() {
            const auto& v = UISettings::values;
            Options o;
            o.kind = static_cast<Kind>(std::min<u32>(v.home_bg_kind.GetValue(), 3));
            o.picture = QString::fromStdString(v.home_bg_picture.GetValue());
            if (const QColor c{QString::fromStdString(v.home_bg_color.GetValue())}; c.isValid()) {
                o.color = c;
            }
            if (const QColor c{QString::fromStdString(v.home_bg_color2.GetValue())}; c.isValid()) {
                o.color2 = c;
            }
            o.angle = static_cast<int>(v.home_bg_angle.GetValue());
            o.animate = v.home_anim_enabled.GetValue();
            o.rays = v.home_anim_rays.GetValue();
            o.particles = static_cast<Particles>(std::min<u32>(v.home_anim_particles.GetValue(), 3));
            o.motion = static_cast<Motion>(std::min<u32>(v.home_anim_motion.GetValue(), 3));
            o.amount = static_cast<int>(v.home_anim_amount.GetValue());
            o.speed = static_cast<int>(v.home_anim_speed.GetValue());
            // The banner background is the same layer, stronger and sharper.
            o.game_glow =
                v.home_anim_game_glow.GetValue() || v.carousel_banner_background.GetValue();
            o.pulse = v.home_anim_pulse.GetValue();
            o.vignette = v.home_anim_vignette.GetValue();
            return o;
        }
    };

    /// Takes new choices; the particles are scattered again when their kind or number changes.
    void SetOptions(const Options& new_options) {
        if (new_options == options) {
            return;
        }
        if (new_options.particles != options.particles || new_options.amount != options.amount ||
            new_options.motion != options.motion) {
            motes.clear();
        }
        options = new_options;
        ++style_serial;
    }
    [[nodiscard]] const Options& GetOptions() const {
        return options;
    }
    /// Whether anything moves: the view's frame timer can stop when nothing does.
    [[nodiscard]] bool Moving() const {
        return options.animate &&
               (options.rays || options.pulse || options.particles != Options::Particles::None);
    }

    void Step(qreal dt, const QRectF& area) {
        if (!options.animate) {
            return;
        }
        dt *= options.speed / 100.0;
        time += dt;
        if (area.isEmpty()) {
            return;
        }
        auto* const random = QRandomGenerator::global();
        if (motes.empty() || bounds != area) {
            bounds = area;
            const int usual = std::clamp(int(area.width() * area.height() / 12000), 40, 90);
            motes.resize(options.particles == Options::Particles::None
                             ? 0
                             : std::max(1, usual * options.amount / 100));
            for (Mote& mote : motes) {
                Spawn(mote, area, random, options.particles);
            }
        }
        for (Mote& mote : motes) {
            if (options.motion == Options::Motion::Fall) {
                // Falling like snow: down, with a gentle side-to-side sway.
                const qreal sway = std::sin(time * mote.pulse_rate * 0.5 + mote.seed);
                mote.pos.rx() += sway * mote.speed * 0.6 * dt;
                mote.pos.ry() += (mote.speed * 1.1 + 6.0) * dt;
                if (mote.pos.y() > area.bottom() + 12) {
                    mote.pos.setY(area.top() - 10);
                    mote.pos.setX(area.left() + random->generateDouble() * area.width());
                }
                continue;
            }
            if (options.motion == Options::Motion::Swirl) {
                // A slow whirl around the middle, each particle on its own ring.
                const QPointF centre = area.center();
                const QPointF d = mote.pos - centre;
                const qreal radius = std::max(20.0, std::hypot(d.x(), d.y()));
                const qreal angle = std::atan2(d.y(), d.x()) + (mote.speed * 1.4 + 4.0) / radius * dt;
                mote.pos = centre + QPointF(std::cos(angle), std::sin(angle)) * radius;
                continue;
            }
            if (mote.is_ember || options.motion == Options::Motion::Rise) {
                // Rising incandescent ember: buoyant upward drift with gentle side-to-side harmonic sway
                const qreal sway = std::sin(time * mote.pulse_rate + mote.seed);
                mote.pos.rx() += sway * (mote.speed * 0.75) * dt;
                mote.pos.ry() -= (mote.speed * 1.25 + 5.0) * dt;
                // If it rises above top, wrap to bottom with new random x
                if (mote.pos.y() < area.top() - 12) {
                    mote.pos.setY(area.bottom() + 10);
                    mote.pos.setX(area.left() + random->generateDouble() * area.width());
                } else if (mote.pos.x() < area.left() - 10) {
                    mote.pos.setX(area.right() + 8);
                } else if (mote.pos.x() > area.right() + 10) {
                    mote.pos.setX(area.left() - 8);
                }
            } else {
                // Lazy, wandering drift: each mote's heading turns slowly on its own.
                mote.heading += std::sin(time * mote.turn + mote.seed) * 0.6 * dt;
                const QPointF velocity{std::cos(mote.heading), std::sin(mote.heading) * 0.6 + 0.15};
                mote.pos += velocity * mote.speed * dt;
                // Wrap around to the other side, so the room never empties.
                if (mote.pos.x() < area.left() - 10) {
                    mote.pos.setX(area.right() + 8);
                } else if (mote.pos.x() > area.right() + 10) {
                    mote.pos.setX(area.left() - 8);
                }
                if (mote.pos.y() < area.top() - 10) {
                    mote.pos.setY(area.bottom() + 8);
                } else if (mote.pos.y() > area.bottom() + 10) {
                    mote.pos.setY(area.top() - 8);
                }
            }
        }
    }

    /// The average colour of the layers that don't move inside `rect` (view coordinates), as
    /// last painted: what text drawn there stands on. Invalid before the first Paint.
    [[nodiscard]] QColor AverageBehind(const QRect& rect) const {
        const QRect clipped = rect.intersected(QRect(QPoint(0, 0), still.size()));
        if (still.isNull() || clipped.isEmpty()) {
            return {};
        }
        const auto key = std::make_tuple(still_serial, clipped.x(), clipped.y(), clipped.width(),
                                         clipped.height());
        if (const auto it = average_cache.find(key); it != average_cache.end()) {
            return it->second;
        }
        const QImage small = still.copy(clipped)
                                 .scaled(8, 8, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                                 .toImage()
                                 .convertToFormat(QImage::Format_RGB32);
        qint64 r = 0, g = 0, b = 0;
        for (int y = 0; y < small.height(); ++y) {
            for (int x = 0; x < small.width(); ++x) {
                const QRgb pixel = small.pixel(x, y);
                r += qRed(pixel);
                g += qGreen(pixel);
                b += qBlue(pixel);
            }
        }
        const qint64 n = qint64(small.width()) * small.height();
        const QColor average(int(r / n), int(g / n), int(b / n));
        if (average_cache.size() > 32) {
            average_cache.clear();
        }
        average_cache.emplace(key, average);
        return average;
    }

    void Paint(QPainter& p, const QRect& area, const QPixmap& ambient, qreal ambient_strength,
               const QPixmap& previous = {}, qreal previous_strength = 0.0) const {
        p.save();
        p.setRenderHint(QPainter::Antialiasing);
        // The layers that don't move (base, the game's blurred picture, the glow where the light
        // comes in, the vignette) in one picture, made again only when they change.
        const auto strength = [](qreal v) { return int(std::clamp(v, 0.0, 1.0) * 64 + 0.5); };
        if (!options.game_glow) {
            ambient_strength = previous_strength = 0.0;
        }
        const std::array<qint64, 7> key{area.width(),         area.height(),
                                        ambient.cacheKey(),   previous.cacheKey(),
                                        strength(ambient_strength), strength(previous_strength),
                                        qint64(style_serial)};
        if (still_generation != PhoenixTheme::Generation()) {
            still_generation = PhoenixTheme::Generation();
            still_key = {}; // the Customize menu's colours changed
        }
        if (key != still_key || still.size() != area.size()) {
            still_key = key;
            ++still_serial;
            still = QPixmap(area.size());
            QPainter s{&still};
            s.setRenderHint(QPainter::Antialiasing);
            const QRect local{QPoint(0, 0), area.size()};
            PaintBase(s, local);
            const auto draw_ambient = [&](const QPixmap& pixmap, qreal opacity) {
                if (pixmap.isNull() || opacity <= 0.0) {
                    return;
                }
                s.setOpacity(opacity);
                s.drawPixmap(0, 0, pixmap);
                s.setOpacity(1.0);
            };
            draw_ambient(previous, previous_strength);
            draw_ambient(ambient, ambient_strength);
            const QPointF source = Source(QRectF(local));
            const qreal reach = std::hypot(local.width(), local.height()) * 1.15;
            s.setPen(Qt::NoPen);
            s.setCompositionMode(QPainter::CompositionMode_Screen);
            QRadialGradient bloom{source, options.rays ? reach * 0.6 : 1.0};
            bloom.setColorAt(0, PhoenixTheme::Recolor(QColor(0xFF, 0xE2, 0x9A, 64)));
            bloom.setColorAt(0.5, PhoenixTheme::Recolor(QColor(0xFF, 0xC8, 0x5A, 16)));
            bloom.setColorAt(1, PhoenixTheme::Recolor(QColor(0xFF, 0xC8, 0x5A, 0)));
            s.setBrush(bloom);
            s.drawRect(local);
            s.setCompositionMode(QPainter::CompositionMode_SourceOver);
            const qreal unit = std::max(local.width(), local.height());
            QRadialGradient vignette{QPointF(local.width() * 0.35, local.height() * 0.3),
                                     unit * 0.95};
            vignette.setColorAt(0.45, QColor(0, 0, 0, 0));
            vignette.setColorAt(1, QColor(0, 0, 0, options.vignette ? 170 : 0));
            s.setBrush(vignette);
            s.drawRect(local);
        }
        p.drawPixmap(area.topLeft(), still);

        p.setCompositionMode(QPainter::CompositionMode_Screen);
        // Dynamic subtle radiant warmth that breathes softly from the source corner
        const qreal reach = std::hypot(area.width(), area.height()) * 1.15;
        const qreal breath = options.pulse ? 0.5 + 0.5 * std::sin(time * 0.65) : 0.0;
        if (breath > 0.01) {
            p.setOpacity(0.06 * breath);
            QRadialGradient warm_pulse{Source(QRectF(area)), reach * 0.85};
            warm_pulse.setColorAt(0, PhoenixTheme::Recolor(PhoenixTheme::DefaultAccent));
            warm_pulse.setColorAt(1, Qt::transparent);
            p.setBrush(warm_pulse);
            p.drawRect(area);
            p.setOpacity(1.0);
        }

        if (options.rays) {
            PaintSun(p, area);
        }
        PaintMotes(p, area);
        p.restore();
    }

private:
    /// The bottom layer: the theme's warm gradient, a picture, one colour or a gradient.
    void PaintBase(QPainter& s, const QRect& local) const {
        switch (options.kind) {
        case Options::Kind::Picture: {
            const QPixmap picture{options.picture};
            if (!picture.isNull()) {
                const qreal scale = std::max(qreal(local.width()) / picture.width(),
                                             qreal(local.height()) / picture.height());
                const QSizeF shown{local.width() / scale, local.height() / scale};
                s.drawPixmap(QRectF(local), picture,
                             QRectF((picture.width() - shown.width()) / 2,
                                    (picture.height() - shown.height()) / 2, shown.width(),
                                    shown.height()));
                return;
            }
            break; // a picture that went missing: the theme's gradient
        }
        case Options::Kind::Solid:
            s.fillRect(local, options.color);
            return;
        case Options::Kind::Gradient: {
            // Across the view at the chosen angle (0 is left to right, 90 top to bottom).
            const qreal radians = options.angle * 3.14159265 / 180.0;
            const QPointF centre = QRectF(local).center();
            const qreal half = 0.5 * (std::abs(local.width() * std::cos(radians)) +
                                      std::abs(local.height() * std::sin(radians)));
            const QPointF along{std::cos(radians) * half, std::sin(radians) * half};
            QLinearGradient gradient{centre - along, centre + along};
            gradient.setColorAt(0, options.color);
            gradient.setColorAt(1, options.color2);
            s.fillRect(local, gradient);
            return;
        }
        case Options::Kind::Theme:
            break;
        }
        QLinearGradient base{local.topLeft(), local.bottomLeft()};
        base.setColorAt(0, PhoenixTheme::Recolor(QColor(0x24, 0x1B, 0x12)));
        base.setColorAt(1, PhoenixTheme::Recolor(QColor(0x10, 0x0C, 0x08)));
        s.fillRect(local, base);
    }

    struct Beam {
        qreal angle;     // radians from the source, pointing down and right
        qreal spread;    // half width, radians
        qreal intensity; // 0..1
    };

    struct Mote {
        QPointF pos;
        qreal heading = 0;
        qreal turn = 0;
        qreal seed = 0;
        qreal speed = 0;
        qreal size = 0;
        bool is_ember = false;
        qreal pulse_rate = 1.0;
    };

    static void Spawn(Mote& mote, const QRectF& area, QRandomGenerator* random,
                      Options::Particles particles) {
        const auto uniform = [random] { return random->generateDouble(); };
        mote.pos = {area.left() + uniform() * area.width(), area.top() + uniform() * area.height()};
        mote.heading = uniform() * 6.2832;
        mote.turn = 0.15 + uniform() * 0.35;
        mote.seed = uniform() * 100;
        mote.speed = 3 + uniform() * 9;
        mote.size = 0.9 + uniform() * 1.8;
        mote.is_ember = particles == Options::Particles::Embers ||
                        (particles == Options::Particles::Both && uniform() < 0.38);
        mote.pulse_rate = 1.2 + uniform() * 2.8;
    }

    static QPointF Source(const QRectF& area) {
        return {area.left() - area.width() * 0.08, area.top() - area.height() * 0.22};
    }

    /// The beams now: a slow sway and breathing, each at its own pace.
    std::array<Beam, 6> Beams() const {
        // angle (deg), spread (deg), sway speed, breathe speed
        static constexpr std::array<std::array<qreal, 4>, 6> shape{{
            {{28, 4.5, 0.050, 0.21}},
            {{36, 2.8, 0.070, 0.17}},
            {{44, 5.5, 0.040, 0.13}},
            {{52, 3.2, 0.060, 0.23}},
            {{60, 4.8, 0.035, 0.19}},
            {{69, 2.6, 0.055, 0.15}},
        }};
        constexpr qreal to_rad = 3.14159265 / 180;
        std::array<Beam, 6> beams;
        for (size_t i = 0; i < beams.size(); ++i) {
            const qreal deg = shape[i][0];
            const qreal spread = shape[i][1];
            const qreal sway = shape[i][2];
            const qreal breathe = shape[i][3];
            const qreal pulse = 0.5 + 0.5 * std::sin(time * breathe * 1.9 + i * 2.3);
            beams[i] = {(deg + 2.2 * std::sin(time * sway * 6.2832 + i * 1.7)) * to_rad,
                        spread * to_rad * (0.85 + 0.15 * std::sin(time * breathe * 4.1 + i)),
                        0.55 + 0.45 * pulse * pulse};
        }
        return beams;
    }

    /// The sunlight across the beams at `angle` (radians from the source): soft bell-shaped
    /// beams with a faint halo, broken into finer shafts that drift and shimmer.
    qreal AngleLight(const std::array<Beam, 6>& beams, qreal angle) const {
        qreal light = 0;
        for (const Beam& beam : beams) {
            const qreal across = (angle - beam.angle) / beam.spread;
            const qreal core = std::exp(-across * across * 1.6);
            const qreal halo = std::exp(-across * across * 0.18) * 0.22;
            light += (core + halo) * beam.intensity;
        }
        const qreal shafts = 0.72 + 0.16 * std::sin(angle * 97.0 + time * 0.37) +
                             0.12 * std::sin(angle * 211.0 - time * 0.23);
        return std::min(1.0, light * shafts);
    }

    /// How the light fades with `distance` (share of the beams' reach): strongest a little way
    /// in from the source, then fading out.
    static qreal Fall(qreal distance) {
        if (distance >= 1.0) {
            return 0.0;
        }
        return std::pow(1.0 - distance, 1.6) * std::min(1.0, distance * 6.0 + 0.35);
    }

    qreal LightAt(const std::array<Beam, 6>& beams, qreal angle, qreal distance) const {
        return AngleLight(beams, angle) * Fall(distance);
    }

    void PaintSun(QPainter& p, const QRect& area) const {
        const QPointF source = Source(area);
        const qreal reach = std::hypot(area.width(), area.height()) * 1.15;
        // The beams, per pixel at a third of the size and scaled up smoothly (no edges),
        // recomputed 15 times a second. Each pixel's angle bin and distance falloff are kept
        // for the size; a frame computes only the light per angle.
        const int w = std::max(16, area.width() / 3);
        const int h = std::max(16, area.height() / 3);
        constexpr int bins = 2048;
        const bool resized = rays.width() != w || rays.height() != h;
        if (resized) {
            rays = QImage(w, h, QImage::Format_ARGB32_Premultiplied);
            pixel_bin.resize(size_t(w) * h);
            pixel_fall.resize(size_t(w) * h);
            const qreal sx = qreal(area.width()) / w;
            const qreal sy = qreal(area.height()) / h;
            angle_min = 10.0;
            angle_max = -10.0;
            std::vector<qreal> angles(size_t(w) * h);
            for (int y = 0; y < h; ++y) {
                for (int x = 0; x < w; ++x) {
                    const qreal px = area.left() + (x + 0.5) * sx - source.x();
                    const qreal py = area.top() + (y + 0.5) * sy - source.y();
                    const size_t i = size_t(y) * w + x;
                    angles[i] = std::atan2(py, px);
                    pixel_fall[i] = float(Fall(std::hypot(px, py) / reach));
                    angle_min = std::min(angle_min, angles[i]);
                    angle_max = std::max(angle_max, angles[i]);
                }
            }
            for (size_t i = 0; i < angles.size(); ++i) {
                pixel_bin[i] = static_cast<uint16_t>(std::clamp(
                    int((angles[i] - angle_min) / (angle_max - angle_min) * (bins - 1) + 0.5), 0,
                    bins - 1));
            }
        }
        if (!resized && time - rays_time < 1.0 / 15.0 && rays_scaled.size() == area.size()) {
            p.drawPixmap(area.topLeft(), rays_scaled);
            return;
        }
        rays_time = time;
        const auto beams = Beams();
        std::array<float, bins> table;
        for (int k = 0; k < bins; ++k) {
            table[k] = float(AngleLight(beams, angle_min + (angle_max - angle_min) * k / (bins - 1)));
        }
        // Warm, a little whiter where it is brightest: between these two (Customize recolours).
        const QColor dim = PhoenixTheme::Recolor(QColor(255, 220, 150));
        const QColor bright = PhoenixTheme::Recolor(QColor(255, 250, 220));
        const int dim_r = dim.red(), dim_g = dim.green(), dim_b = dim.blue();
        const int span_r = bright.red() - dim_r, span_g = bright.green() - dim_g,
                  span_b = bright.blue() - dim_b;
        static constexpr int dither[4][4] = {
            {0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
        for (int y = 0; y < h; ++y) {
            auto* const row = reinterpret_cast<QRgb*>(rays.scanLine(y));
            const size_t base = size_t(y) * w;
            for (int x = 0; x < w; ++x) {
                const float light = table[pixel_bin[base + x]] * pixel_fall[base + x];
                const int a = std::min(255, int(light * 120.0f + dither[y & 3][x & 3] / 16.0f));
                // Warm, a little whiter where it is brightest (premultiplied).
                row[x] = qRgba(a * (dim_r + int(light * span_r)) / 255,
                               a * (dim_g + int(light * span_g)) / 255,
                               a * (dim_b + int(light * span_b)) / 255, a);
            }
        }
        // Scaled up smoothly once per update, not every frame.
        rays_scaled = QPixmap::fromImage(
            rays.scaled(area.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
        p.drawPixmap(area.topLeft(), rays_scaled);
    }

    void PaintMotes(QPainter& p, const QRect& area) const {
        const QPointF source = Source(area);
        const qreal reach = std::hypot(area.width(), area.height()) * 1.15;
        const auto beams = Beams();
        p.setPen(Qt::NoPen);
        for (const Mote& mote : motes) {
            if (mote.is_ember) {
                // Rising ember: self-luminous, pulsing with thermal spark intensity
                const qreal pulse = 0.55 + 0.45 * std::sin(time * mote.pulse_rate + mote.seed);
                const qreal alpha = std::clamp(pulse * 0.95, 0.15, 1.0);
                const qreal r = mote.size * (1.2 + 0.8 * pulse) * 2.2;
                p.setOpacity(alpha);
                p.drawPixmap(QRectF(mote.pos.x() - r, mote.pos.y() - r, r * 2, r * 2), EmberSprite(),
                             QRectF(0, 0, 64, 64));
            } else {
                // Motes catch the light: bright inside a beam, barely there in the shade.
                const QPointF d = mote.pos - source;
                // Without the sunbeams there is nothing to catch: an even glow instead.
                const qreal light =
                    options.rays ? std::min(1.0, 1.5 * LightAt(beams, std::atan2(d.y(), d.x()),
                                                               std::hypot(d.x(), d.y()) / reach))
                                 : 0.45;
                const qreal twinkle = 0.8 + 0.2 * std::sin(time * 2.3 + mote.seed);
                const qreal alpha = (30 + 225 * light) * twinkle / 255.0;
                const qreal r = mote.size * (1.0 + 1.6 * light) * 2.4;
                p.setOpacity(std::min(1.0, alpha));
                p.drawPixmap(QRectF(mote.pos.x() - r, mote.pos.y() - r, r * 2, r * 2), MoteSprite(),
                             QRectF(0, 0, 64, 64));
            }
        }
        p.setOpacity(1.0);
    }

    /// An ember's fiery glowing particle sprite with an intense bright core and chromatic corona.
    static const QPixmap& EmberSprite() {
        static QPixmap sprite;
        static u32 sprite_generation = ~0U;
        if (sprite_generation == PhoenixTheme::Generation()) {
            return sprite;
        }
        sprite_generation = PhoenixTheme::Generation();
        sprite = [] {
            QPixmap pixmap(64, 64);
            pixmap.fill(Qt::transparent);
            QPainter painter{&pixmap};
            painter.setRenderHint(QPainter::Antialiasing);
            QRadialGradient glow{QPointF(32, 32), 32};
            const QColor core = PhoenixTheme::Recolor(QColor(0xFF, 0xFD, 0xF0, 255));
            const QColor mid = PhoenixTheme::Recolor(PhoenixTheme::DefaultAccent);
            QColor mid_alpha = mid;
            mid_alpha.setAlpha(180);
            QColor edge = mid.darker(150);
            edge.setAlpha(0);
            glow.setColorAt(0, core);
            glow.setColorAt(0.22, mid_alpha);
            glow.setColorAt(0.65, QColor(mid.red(), mid.green(), mid.blue(), 45));
            glow.setColorAt(1, edge);
            painter.setPen(Qt::NoPen);
            painter.setBrush(glow);
            painter.drawEllipse(QPointF(32, 32), 32, 32);
            return pixmap;
        }();
        return sprite;
    }

    /// A mote's soft glow, drawn once.
    static const QPixmap& MoteSprite() {
        static QPixmap sprite;
        static u32 sprite_generation = ~0U;
        if (sprite_generation == PhoenixTheme::Generation()) {
            return sprite;
        }
        sprite_generation = PhoenixTheme::Generation();
        sprite = [] {
            QPixmap pixmap(64, 64);
            pixmap.fill(Qt::transparent);
            QPainter painter{&pixmap};
            painter.setRenderHint(QPainter::Antialiasing);
            QRadialGradient glow{QPointF(32, 32), 32};
            glow.setColorAt(0, PhoenixTheme::Recolor(QColor(0xFF, 0xF4, 0xD6, 255)));
            glow.setColorAt(0.35, PhoenixTheme::Recolor(QColor(0xFF, 0xE2, 0x9A, 128)));
            glow.setColorAt(1, PhoenixTheme::Recolor(QColor(0xFF, 0xD0, 0x70, 0)));
            painter.setPen(Qt::NoPen);
            painter.setBrush(glow);
            painter.drawEllipse(QPointF(32, 32), 32, 32);
            return pixmap;
        }();
        return sprite;
    }

    mutable QImage rays; ///< PaintSun's light, at a third of the size
    mutable qreal rays_time = -1.0;
    mutable QPixmap rays_scaled;              ///< `rays` at the view's size
    mutable QPixmap still;                    ///< The layers that don't move (Paint)
    mutable std::array<qint64, 7> still_key{};
    mutable u32 still_generation = ~0U; ///< PhoenixTheme::Generation `still` was drawn for
    mutable u64 still_serial = 0;       ///< Counts the times `still` was drawn again
    mutable std::map<std::tuple<u64, int, int, int, int>, QColor> average_cache;
    mutable std::vector<uint16_t> pixel_bin;  ///< Per pixel of `rays`: its angle bin
    mutable std::vector<float> pixel_fall;    ///< Per pixel of `rays`: the distance falloff
    mutable qreal angle_min = 0.0;
    mutable qreal angle_max = 1.0;

    std::vector<Mote> motes;
    Options options;
    u64 style_serial = 0; ///< Counts option changes, part of `still`'s key
    QRectF bounds;
    qreal time = 0;
};

} // namespace PhoenixBackdrop
