// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2019 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QBuffer>
#include <QByteArray>
#include <QGraphicsOpacityEffect>
#include <QIODevice>
#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <QPropertyAnimation>
#include <QPainterPath>
#include <QPushButton>
#include <QRadialGradient>
#include <QStyleOption>
#include <QElapsedTimer>
#include <QTimer>
#include <cmath>
#include <functional>
#include "common/container/unordered_map.h"
#include "phoenix/util/phoenix_theme.h"
#include "core/frontend/framebuffer_layout.h"
#include "core/loader/loader.h"
#include "ui_loading_screen.h"
#include "video_core/rasterizer_interface.h"
#include "video_core/shader_warm/shader_warm_builder.h"
#include "phoenix/loading_screen.h"
#include "phoenix/util/phoenix_backdrop.h"

// Mingw seems to not have QMovie at all. If QMovie is missing then use a single frame instead of an
// showing the full animation
#if !PHOENIX_QT_MOVIE_MISSING
#include <QMovie>
#endif

constexpr char PROGRESSBAR_STYLE_PREPARE[] = R"(
QProgressBar {}
QProgressBar::chunk {})";

constexpr char PROGRESSBAR_STYLE_BUILD[] = R"(
QProgressBar {
  background-color: black;
  border: 2px solid white;
  border-radius: 4px;
  padding: 2px;
}
QProgressBar::chunk {
  background-color: #ff3c28;
  width: 1px;
})";

constexpr char SKIP_BUTTON_STYLE[] = R"(
QPushButton {
  background-color: rgba(255, 122, 26, 40);
  color: #FFB36B;
  border: 2px solid #FF7A1A;
  border-radius: 17px;
  padding: 6px 28px;
  font-size: 12pt;
  font-weight: bold;
}
QPushButton:hover {
  background-color: #FF7A1A;
  color: #2A0E00;
}
QPushButton:disabled {
  color: #7A705C;
  border-color: #4A3E2C;
  background-color: transparent;
})";

namespace {

const QColor Flame{0xFF, 0x7A, 0x1A};
const QColor FlameDeep{0xD4, 0x24, 0x0E};
const QColor FlameCore{0xFF, 0xD2, 0x3F};
const QColor Cream{0xFF, 0xF4, 0xDA};

/// The loading bar: a rounded track that burns from left to right, with flames flickering along
/// its top (tallest at the leading edge) and sparks rising from it; while the length is unknown
/// a small fire glides to and fro.
class FlameProgress : public QWidget {
public:
    explicit FlameProgress(QWidget* parent) : QWidget{parent} {
        setMinimumHeight(88);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setAttribute(Qt::WA_TranslucentBackground);
        // 30 frames a second keeps the fill gliding smoothly while freeing CPU for shader compilation and loading.
        timer.setInterval(33);
        timer.setTimerType(Qt::CoarseTimer);
        clock.start();
        connect(&timer, &QTimer::timeout, this, [this] {
            const qreal dt = std::min(0.05, clock.restart() / 1000.0);
            phase += dt;
            const qreal target =
                maximum > minimum
                    ? std::clamp(double(value - minimum) / (maximum - minimum), 0.0, 1.0)
                    : 0.0;
            shown += (target - shown) * (1.0 - std::exp(-dt * 7.0));
            if (on_frame) {
                on_frame(dt); // the screen behind moves too
            } else {
                update();
            }
        });
    }

    /// Called every frame with the time since the last one (the loading screen steps its
    /// backdrop and repaints, this bar included).
    std::function<void(qreal)> on_frame;

    void SetRange(int min_value, int max_value) {
        if (max_value > min_value && maximum <= minimum) {
            shown = 0; // a new length: fill from the start
        }
        minimum = min_value;
        maximum = max_value;
        update();
    }
    void SetValue(int new_value) {
        value = new_value;
        update();
    }

protected:
    void showEvent(QShowEvent*) override {
        timer.start();
    }
    void hideEvent(QHideEvent*) override {
        timer.stop();
    }

    void paintEvent(QPaintEvent*) override {
        QPainter painter{this};
        painter.setRenderHint(QPainter::Antialiasing);
        const int width = std::min(this->width() - 80, 560);
        // A fat, soft track, set low in the widget: the flames need the room above it.
        const QRectF track{(this->width() - width) / 2.0, height() / 2.0 - 4, double(width), 32};
        const qreal radius = track.height() / 2;
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0, 70));
        painter.drawRoundedRect(track.translated(0, 4), radius, radius);
        painter.setBrush(PhoenixTheme::Recolor(QColor(0x3A, 0x26, 0x1A)));
        painter.drawRoundedRect(track, radius, radius);
        painter.setBrush(PhoenixTheme::Recolor(QColor(0x1E, 0x14, 0x0E)));
        painter.drawRoundedRect(track.adjusted(4, 4, -4, -4), radius - 4, radius - 4);

        const bool busy = maximum <= minimum;
        const QRectF inner = track.adjusted(4, 4, -4, -4);
        QRectF fill;
        bool forward = true; // which end of the fire leads
        if (busy) {
            // A small fire easing back and forth.
            const qreal piece = inner.width() * 0.32;
            const qreal swing = (1 - std::cos(phase * 2.2)) / 2;
            forward = std::sin(phase * 2.2) >= 0;
            fill = {inner.left() + (inner.width() - piece) * swing, inner.top(), piece,
                    inner.height()};
        } else {
            fill = {inner.left(), inner.top(), std::max(inner.height(), inner.width() * shown),
                    inner.height()};
        }
        const qreal r = fill.height() / 2;

        // The flames along the top of the burning part: tongues that flicker on their own,
        // lean back from the leading edge and stand tallest there. `scale` shrinks them for the
        // hotter layers inside.
        const auto tongues = [&](qreal scale) {
            QPainterPath path;
            const qreal left = fill.left() + r * 0.4;
            const qreal right = fill.right() - r * 0.4;
            const qreal base = fill.top() + 4;
            const int count = std::max(1, int((right - left) / 18.0));
            const qreal span = (right - left) / count;
            path.moveTo(left, fill.center().y());
            path.lineTo(left, base);
            for (int i = 0; i < count; ++i) {
                const qreal x0 = left + span * i;
                const qreal middle = x0 + span / 2;
                const qreal lead = std::exp(-(forward ? right - middle : middle - left) / 46.0);
                const qreal flick = 0.5 + 0.5 * std::sin(phase * 9.0 + i * 1.9) *
                                              std::sin(phase * 4.3 + i * 0.7);
                const qreal tall = scale * (8.0 + 9.0 * flick + 15.0 * lead);
                const qreal lean = (forward ? -1.0 : 1.0) * (2.0 + 3.0 * lead) +
                                   2.5 * std::sin(phase * 5.0 + i * 1.3);
                path.quadTo(QPointF(x0 + span * 0.12, base - tall * 0.4),
                            QPointF(middle + lean, base - tall));
                path.quadTo(QPointF(x0 + span * 0.88, base - tall * 0.4),
                            QPointF(x0 + span, base));
            }
            path.lineTo(right, fill.center().y());
            path.closeSubpath();
            return path;
        };
        const QColor deep = FlameDeep; // fire keeps its colours whatever the accent
        const QColor flame = Flame;
        const QColor core = FlameCore;

        // A glow around the fire.
        QRadialGradient halo{fill.center(), fill.width() / 2 + 40};
        QColor halo_colour = flame;
        halo_colour.setAlpha(46);
        halo.setColorAt(0, halo_colour);
        halo_colour.setAlpha(0);
        halo.setColorAt(1, halo_colour);
        painter.setBrush(halo);
        painter.drawRoundedRect(fill.adjusted(-30, -30, 30, 18), r + 24, r + 24);

        // The burning part: red at the bottom, yellow hot on top.
        painter.setBrush(deep);
        painter.drawPath(tongues(1.0));
        QLinearGradient gradient{fill.topLeft(), fill.bottomLeft()};
        gradient.setColorAt(0, core);
        gradient.setColorAt(0.45, flame);
        gradient.setColorAt(1, deep);
        painter.setBrush(gradient);
        painter.drawRoundedRect(fill, r, r);
        painter.setBrush(flame);
        painter.drawPath(tongues(0.68));
        painter.setBrush(core);
        painter.drawPath(tongues(0.36));

        // Inside it: heat rolling along, and a white-hot spot at the leading edge.
        painter.save();
        QPainterPath body;
        body.addRoundedRect(fill, r, r);
        painter.setClipPath(body);
        QPainterPath heat;
        heat.moveTo(fill.left(), fill.bottom());
        for (qreal x = fill.left(); x <= fill.right() + 6; x += 6) {
            heat.lineTo(x, fill.center().y() + 3 +
                               3.0 * std::sin(x / 16.0 - phase * 4.0) +
                               1.5 * std::sin(x / 7.0 + phase * 6.0));
        }
        heat.lineTo(fill.right(), fill.bottom());
        heat.closeSubpath();
        QColor ember = deep;
        ember.setAlpha(150);
        painter.setBrush(ember);
        painter.drawPath(heat);
        const QPointF hot{forward ? fill.right() - r : fill.left() + r, fill.center().y()};
        QRadialGradient spot{hot, r * 2.2};
        spot.setColorAt(0, QColor(255, 250, 225, 230));
        spot.setColorAt(0.4, QColor(core.red(), core.green(), core.blue(), 150));
        spot.setColorAt(1, QColor(core.red(), core.green(), core.blue(), 0));
        painter.setBrush(spot);
        painter.drawRect(fill);
        painter.restore();

        // Sparks rising off the fire, each on its own short life.
        for (int i = 0; i < 16; ++i) {
            const qreal life = std::fmod(phase * (0.45 + 0.05 * (i % 5)) + i * 0.173, 1.0);
            const qreal along = std::fmod(i * 0.618034, 1.0);
            const qreal x = fill.left() + r + (fill.width() - 2 * r) * along +
                            6.0 * std::sin(phase * 3.0 + i) * life;
            const qreal y = fill.top() - 4 - life * 34.0;
            QColor spark = core;
            spark.setAlphaF(0.9 * (1.0 - life) * (1.0 - life));
            painter.setBrush(spark);
            const qreal size = 1.0 + 1.4 * (1.0 - life) * (0.6 + 0.4 * ((i * 7) % 3) / 2.0);
            painter.drawEllipse(QPointF(x, y), size, size);
        }

        if (!busy) {
            QFont font = this->font();
            font.setBold(true);
            font.setPixelSize(15);
            painter.setFont(font);
            painter.setPen(Cream);
            const int percent =
                int(100.0 * std::clamp(double(value - minimum) / (maximum - minimum), 0.0, 1.0));
            painter.drawText(QRectF(track.right() + 12, track.top(), 70, track.height()),
                             Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("%1%").arg(percent));
        }
    }

private:
    QTimer timer;
    qreal phase = 0;
    qreal shown = 0; ///< The filled share on screen, easing towards the real one
    QElapsedTimer clock;
    int minimum = 0;
    int maximum = 0;
    int value = 0;
};

} // Anonymous namespace

constexpr char PROGRESSBAR_STYLE_COMPLETE[] = R"(
QProgressBar {
  background-color: #0ab9e6;
  border: 2px solid white;
  border-radius: 4px;
  padding: 2px;
}
QProgressBar::chunk {
  background-color: #ff3c28;
})";

LoadingScreen::LoadingScreen(QWidget* parent)
    : QWidget(parent), ui(std::make_unique<Ui::LoadingScreen>()),
      previous_stage(VideoCore::LoadCallbackStage::Complete) {
    ui->setupUi(this);
    setMinimumSize(Layout::MinimumSize::Width, Layout::MinimumSize::Height);

    // PhoenixEmu's look: the background is painted (paintEvent), the labels sit on it.
    setStyleSheet(QString{});
    ui->fade_parent->setStyleSheet(QStringLiteral("background: transparent;"));
    ui->logo->setStyleSheet(QStringLiteral("background: transparent;"));
    ui->banner->setStyleSheet(QStringLiteral("background: transparent;"));
    ui->stage->setStyleSheet(QStringLiteral(
        "background: transparent; color: #FFF4DA; font-size: 18pt; font-weight: bold;"));
    ui->value->setStyleSheet(
        QStringLiteral("background: transparent; color: #E8D9B5; font-size: 12pt;"));
    // The progress bar's values drive the fire bar drawn in its place; its frames move the
    // backdrop too.
    scene = std::make_shared<PhoenixBackdrop::Scene>();
    flame_progress = new FlameProgress(this);
    static_cast<FlameProgress*>(flame_progress)->on_frame = [this](qreal dt) {
        scene->SetOptions(PhoenixBackdrop::Scene::Options::FromSettings());
        scene->Step(dt, QRectF(rect()));
        update();
    };
    ui->verticalLayout->insertWidget(ui->verticalLayout->indexOf(ui->progress_bar) + 1,
                                     flame_progress);
    ui->progress_bar->setMaximumHeight(0);
    ui->progress_bar->setVisible(false);

    // Create a fade out effect to hide this loading screen widget.
    // When fading opacity, it will fade to the parent widgets background color, which is why we
    // create an internal widget named fade_widget that we use the effect on, while keeping the
    // loading screen widget's background color black. This way we can create a fade to black effect
    opacity_effect = new QGraphicsOpacityEffect(this);
    opacity_effect->setOpacity(1);
    ui->fade_parent->setGraphicsEffect(opacity_effect);
    fadeout_animation = std::make_unique<QPropertyAnimation>(opacity_effect, "opacity");
    fadeout_animation->setDuration(500);
    fadeout_animation->setStartValue(1);
    fadeout_animation->setEndValue(0);
    fadeout_animation->setEasingCurve(QEasingCurve::OutBack);

    // After the fade completes, hide the widget and reset the opacity
    connect(fadeout_animation.get(), &QPropertyAnimation::finished, [this] {
        hide();
        opacity_effect->setOpacity(1);
        emit Hidden();
    });
    connect(this, &LoadingScreen::LoadProgress, this, &LoadingScreen::OnLoadProgress,
            Qt::QueuedConnection);

    // Shown while shaders are precached: starts the game without waiting for the rest.
    skip_button = new QPushButton(tr("Skip"), this);
    skip_button->setStyleSheet(PhoenixTheme::Transform(QString::fromUtf8(SKIP_BUTTON_STYLE)));
    skip_button->setToolTip(tr("Start the game now. The shaders not built yet are built the next "
                               "time the game starts, or while you play when the game needs "
                               "them."));
    skip_button->hide();
    ui->verticalLayout->insertWidget(2, skip_button, 0, Qt::AlignHCenter | Qt::AlignTop);
    connect(skip_button, &QPushButton::clicked, this, [this] {
        VideoCommon::ShaderWarm::RequestSkipWarm();
        skip_button->setEnabled(false);
    });
    qRegisterMetaType<VideoCore::LoadCallbackStage>();

    stage_translations = {
        {VideoCore::LoadCallbackStage::Prepare, tr("Igniting your game...")},
        {VideoCore::LoadCallbackStage::Build, tr("Loading saved shaders %1 / %2")},
        {VideoCore::LoadCallbackStage::Complete, tr("Here we go!")},
        {VideoCore::LoadCallbackStage::Warm, tr("Building shaders %1 / %2 (just this once)")},
    };
    progressbar_style = {
        {VideoCore::LoadCallbackStage::Prepare, PROGRESSBAR_STYLE_PREPARE},
        {VideoCore::LoadCallbackStage::Build, PROGRESSBAR_STYLE_BUILD},
        {VideoCore::LoadCallbackStage::Complete, PROGRESSBAR_STYLE_COMPLETE},
        {VideoCore::LoadCallbackStage::Warm, PROGRESSBAR_STYLE_BUILD},
    };
}

LoadingScreen::~LoadingScreen() = default;

void LoadingScreen::Prepare(Loader::AppLoader& loader) {
    std::vector<u8> buffer;
    if (loader.ReadBanner(buffer) == Loader::ResultStatus::Success) {
#ifdef PHOENIX_QT_MOVIE_MISSING
        QPixmap map;
        map.loadFromData(buffer.data(), buffer.size());
        ui->banner->setPixmap(map);
#else
        backing_mem = std::make_unique<QByteArray>(reinterpret_cast<char*>(buffer.data()),
                                                   static_cast<int>(buffer.size()));
        backing_buf = std::make_unique<QBuffer>(backing_mem.get());
        backing_buf->open(QIODevice::ReadOnly);
        animation = std::make_unique<QMovie>(backing_buf.get(), QByteArray());
        animation->start();
        ui->banner->setMovie(animation.get());
#endif
        buffer.clear();
    }
    // The game's icon, drawn as a big tile over the stage text (paintEvent).
    game_icon = {};
    if (std::vector<u8> icon; loader.ReadIcon(icon) == Loader::ResultStatus::Success) {
        game_icon.loadFromData(icon.data(), static_cast<uint>(icon.size()));
    }
    ambient = {}; // made at the screen's size when painted
    if (loader.ReadLogo(buffer) == Loader::ResultStatus::Success) {
        QPixmap map;
        map.loadFromData(buffer.data(), static_cast<uint>(buffer.size()));
        ui->logo->setPixmap(map);
    }

    slow_shader_compile_start = false;
    OnLoadProgress(VideoCore::LoadCallbackStage::Prepare, 0, 0);
}

void LoadingScreen::OnLoadComplete() {
    fadeout_animation->start(QPropertyAnimation::KeepWhenStopped);
}

void LoadingScreen::OnLoadProgress(VideoCore::LoadCallbackStage stage, std::size_t value,
                                   std::size_t total) {
    using namespace std::chrono;
    const auto now = steady_clock::now();
    // reset the timer if the stage changes
    if (stage != previous_stage) {
        // The fire bar shows from the first stage on; the old bar stays hidden.
        flame_progress->setVisible(true);
        skip_button->setVisible(stage == VideoCore::LoadCallbackStage::Warm);
        skip_button->setEnabled(true);
        previous_stage = stage;
        // reset back to fast shader compiling since the stage changed
        slow_shader_compile_start = false;
    }
    // update the max of the progress bar if the number of shaders change
    if (total != previous_total) {
        ui->progress_bar->setMaximum(static_cast<int>(total));
        previous_total = total;
    }
    // Reset the progress bar ranges if compilation is done
    if (stage == VideoCore::LoadCallbackStage::Complete) {
        ui->progress_bar->setRange(0, 0);
    }

    // Throttle high-frequency UI updates during heavy shader loading to save CPU cycles
    const bool is_ending = stage == VideoCore::LoadCallbackStage::Complete || value >= total;
    if (!is_ending && stage == previous_stage && now - previous_time < milliseconds{33}) {
        return;
    }

    QString estimate;
    // If there's a drastic slowdown in the rate, then display an estimate
    if (now - previous_time > milliseconds{50} || slow_shader_compile_start) {
        if (!slow_shader_compile_start) {
            slow_shader_start = steady_clock::now();
            slow_shader_compile_start = true;
            slow_shader_first_value = value;
        }
        // only calculate an estimate time after a second has passed since stage change
        const auto diff = duration_cast<milliseconds>(now - slow_shader_start);
        if (diff > seconds{1} && value > slow_shader_first_value) {
            const auto eta_mseconds =
                static_cast<long>(static_cast<double>(total - slow_shader_first_value) /
                                  (value - slow_shader_first_value) * diff.count());
            estimate =
                tr("Estimated Time %1")
                    .arg(QTime(0, 0, 0, 0)
                             .addMSecs(std::max<long>(eta_mseconds - diff.count() + 1000, 1000))
                             .toString(QStringLiteral("mm:ss")));
        }
    }

    // update labels and progress bar
    if (stage == VideoCore::LoadCallbackStage::Warm && total == 0) {
        // Before the count is known: the game's files are read for their shader programs.
        ui->stage->setText(tr("Gathering this game's shaders (once, about a minute)..."));
        ui->progress_bar->setRange(0, 0);
    } else if (stage == VideoCore::LoadCallbackStage::Build ||
               stage == VideoCore::LoadCallbackStage::Warm) {
        ui->stage->setText(stage_translations[stage].arg(value).arg(total));
    } else {
        ui->stage->setText(stage_translations[stage]);
    }
    ui->value->setText(estimate);
    ui->progress_bar->setValue(static_cast<int>(value));
    auto* const bar = static_cast<FlameProgress*>(flame_progress);
    if (stage == VideoCore::LoadCallbackStage::Prepare) {
        bar->SetRange(0, 0); // no length known yet: the gliding piece
    } else {
        bar->SetRange(ui->progress_bar->minimum(), ui->progress_bar->maximum());
    }
    bar->SetValue(static_cast<int>(value));
    previous_time = now;

    // TEST: PHOENIXEMU_LOADING_SHOT=<png> saves a picture of the loading screen 3 s in.
    if (static bool shot_taken = false; !shot_taken) {
        if (const QByteArray shot = qgetenv("PHOENIXEMU_LOADING_SHOT"); !shot.isEmpty()) {
            shot_taken = true;
            QTimer::singleShot(3000, this, [this, path = QString::fromUtf8(shot)] {
                window()->grab().save(path);
            });
        }
    }
}

void LoadingScreen::paintEvent(QPaintEvent* event) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRect area = rect();
    // The home view's moving backdrop, with the game's icon as the ambient glow.
    if (ambient.size() != area.size() && !game_icon.isNull()) {
        ambient = PhoenixBackdrop::MakeAmbient(game_icon, area.size());
    }
    scene->Paint(p, area, ambient, 0.45);

    // The game's icon as a glowing rounded tile above the stage text.
    if (!game_icon.isNull()) {
        const int tile = std::clamp(area.height() / 4, 96, 220);
        const QPoint stage_top = ui->stage->mapTo(this, QPoint(0, 0));
        const QRectF tile_rect{(area.width() - tile) / 2.0,
                               std::max(24.0, stage_top.y() - tile - 28.0), double(tile),
                               double(tile)};
        const qreal radius = tile * 0.12;
        for (int i = 6; i >= 1; --i) {
            QColor glow = Flame;
            glow.setAlpha(12 + (6 - i) * 6);
            p.setPen(QPen(glow, 2));
            p.setBrush(Qt::NoBrush);
            const qreal spread = 3.0 + i * 2.5;
            p.drawRoundedRect(tile_rect.adjusted(-spread, -spread, spread, spread),
                              radius + spread, radius + spread);
        }
        p.save();
        QPainterPath clip;
        clip.addRoundedRect(tile_rect, radius, radius);
        p.setClipPath(clip);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.drawPixmap(tile_rect.toRect(), game_icon);
        p.restore();
        p.setPen(QPen(Flame, 4));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(tile_rect, radius, radius);
    }
    QWidget::paintEvent(event);
}

void LoadingScreen::Clear() {
#ifndef PHOENIX_QT_MOVIE_MISSING
    animation.reset();
    backing_buf.reset();
    backing_mem.reset();
#endif
}
