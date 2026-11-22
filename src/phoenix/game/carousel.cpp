// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>

#include <QDateTime>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QLabel>
#include <QSlider>
#include <QPushButton>
#include <QResizeEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmapCache>
#include <QWheelEvent>
#include <QRadialGradient>
#include <QTimer>
#include <QPropertyAnimation>
#include <QRandomGenerator>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QScrollBar>
#include <QVariantAnimation>
#include "qt_common/config/uisettings.h"
#include "phoenix/util/phoenix_theme.h"
#include "qt_common/game_list/game_list_p.h"
#include "phoenix/util/phoenix_backdrop.h"
#include "qt_common/game_list/model.h"
#include "phoenix/game/common.h"
#include "phoenix/game/case_box.h"
#include "phoenix/game/game_card.h"
#include "phoenix/game/carousel.h"

namespace {

/// Two overlapping cards: the size slider's icon.
QPixmap CardsIcon() {
    QPixmap pixmap(40, 40);
    pixmap.fill(Qt::transparent);
    QPainter painter{&pixmap};
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(2.0, 2.0);
    const QColor colour = PhoenixTheme::Recolor(QColor(0xFF, 0xD9, 0x66));
    painter.setPen(QPen(colour, 1.4));
    painter.setBrush(PhoenixTheme::Recolor(QColor(0xFF, 0xD9, 0x66, 60)));
    painter.drawRoundedRect(QRectF(2.5, 6.5, 9, 11), 2, 2);
    painter.setBrush(colour);
    painter.drawRoundedRect(QRectF(8.5, 2.5, 9, 11), 2, 2);
    return pixmap;
}

/// A speaker, with sound waves when the music plays and a cross when it is muted.
QIcon SpeakerIcon(bool playing) {
    QPixmap pixmap(48, 48);
    pixmap.fill(Qt::transparent);
    QPainter painter{&pixmap};
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(48.0 / 20.0, 48.0 / 20.0);
    const QColor colour = PhoenixTheme::Recolor(QColor(0xFF, 0xD9, 0x66));
    QPainterPath speaker;
    speaker.moveTo(2.5, 7.5);
    speaker.lineTo(6, 7.5);
    speaker.lineTo(10, 3.5);
    speaker.lineTo(10, 16.5);
    speaker.lineTo(6, 12.5);
    speaker.lineTo(2.5, 12.5);
    speaker.closeSubpath();
    painter.setPen(Qt::NoPen);
    painter.setBrush(colour);
    painter.drawPath(speaker);
    QPen pen{colour, 1.6};
    pen.setCapStyle(Qt::RoundCap);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    if (playing) {
        painter.drawArc(QRectF(8.5, 6.5, 5, 7), -60 * 16, 120 * 16);
        painter.drawArc(QRectF(8.5, 3.5, 9, 13), -60 * 16, 120 * 16);
    } else {
        painter.drawLine(QPointF(12.5, 7.5), QPointF(17, 12.5));
        painter.drawLine(QPointF(17, 7.5), QPointF(12.5, 12.5));
    }
    return QIcon{pixmap};
}

} // Anonymous namespace

namespace {

struct Quote {
    const char* text;
    const char* author;
};

/// Short lines from philosophers and old proverbs (public domain), for under the greeting.
constexpr std::array<Quote, 24> Quotes{{
    {QT_TRANSLATE_NOOP("GameCarousel", "A journey of a thousand miles begins with a single step."),
     QT_TRANSLATE_NOOP("GameCarousel", "Lao Tzu")},
    {QT_TRANSLATE_NOOP("GameCarousel", "Nature does not hurry, yet everything is accomplished."),
     QT_TRANSLATE_NOOP("GameCarousel", "Lao Tzu")},
    {QT_TRANSLATE_NOOP("GameCarousel", "We suffer more often in imagination than in reality."),
     QT_TRANSLATE_NOOP("GameCarousel", "Seneca")},
    {QT_TRANSLATE_NOOP("GameCarousel", "While we wait for life, life passes."),
     QT_TRANSLATE_NOOP("GameCarousel", "Seneca")},
    {QT_TRANSLATE_NOOP("GameCarousel", "Waste no more time arguing what a good man should be. Be one."),
     QT_TRANSLATE_NOOP("GameCarousel", "Marcus Aurelius")},
    {QT_TRANSLATE_NOOP("GameCarousel", "What stands in the way becomes the way."),
     QT_TRANSLATE_NOOP("GameCarousel", "Marcus Aurelius")},
    {QT_TRANSLATE_NOOP("GameCarousel",
                       "Dwell on the beauty of life. Watch the stars, and see yourself running with them."),
     QT_TRANSLATE_NOOP("GameCarousel", "Marcus Aurelius")},
    {QT_TRANSLATE_NOOP("GameCarousel", "No man ever steps in the same river twice."),
     QT_TRANSLATE_NOOP("GameCarousel", "Heraclitus")},
    {QT_TRANSLATE_NOOP("GameCarousel", "The unexamined life is not worth living."),
     QT_TRANSLATE_NOOP("GameCarousel", "Socrates")},
    {QT_TRANSLATE_NOOP("GameCarousel", "Know thyself."),
     QT_TRANSLATE_NOOP("GameCarousel", "Inscribed at Delphi")},
    {QT_TRANSLATE_NOOP("GameCarousel", "First say to yourself what you would be; then do what you have to do."),
     QT_TRANSLATE_NOOP("GameCarousel", "Epictetus")},
    {QT_TRANSLATE_NOOP("GameCarousel", "Wealth consists not in having great possessions, but in having few wants."),
     QT_TRANSLATE_NOOP("GameCarousel", "Epictetus")},
    {QT_TRANSLATE_NOOP("GameCarousel", "Happiness depends upon ourselves."),
     QT_TRANSLATE_NOOP("GameCarousel", "Aristotle")},
    {QT_TRANSLATE_NOOP("GameCarousel", "Well begun is half done."),
     QT_TRANSLATE_NOOP("GameCarousel", "Aristotle")},
    {QT_TRANSLATE_NOOP("GameCarousel", "Hope is a waking dream."),
     QT_TRANSLATE_NOOP("GameCarousel", "Aristotle")},
    {QT_TRANSLATE_NOOP("GameCarousel", "Real knowledge is to know the extent of one's ignorance."),
     QT_TRANSLATE_NOOP("GameCarousel", "Confucius")},
    {QT_TRANSLATE_NOOP("GameCarousel", "He who has a why to live can bear almost any how."),
     QT_TRANSLATE_NOOP("GameCarousel", "Friedrich Nietzsche")},
    {QT_TRANSLATE_NOOP("GameCarousel", "Adopt the pace of nature: her secret is patience."),
     QT_TRANSLATE_NOOP("GameCarousel", "Ralph Waldo Emerson")},
    {QT_TRANSLATE_NOOP("GameCarousel", "Rather than love, than money, than fame, give me truth."),
     QT_TRANSLATE_NOOP("GameCarousel", "Henry David Thoreau")},
    {QT_TRANSLATE_NOOP("GameCarousel", "Nothing is so strong as gentleness, nothing so gentle as real strength."),
     QT_TRANSLATE_NOOP("GameCarousel", "Francis de Sales")},
    {QT_TRANSLATE_NOOP("GameCarousel", "Patience and time do more than strength or passion."),
     QT_TRANSLATE_NOOP("GameCarousel", "Jean de La Fontaine")},
    {QT_TRANSLATE_NOOP("GameCarousel", "The best way out is always through."),
     QT_TRANSLATE_NOOP("GameCarousel", "Robert Frost")},
    {QT_TRANSLATE_NOOP("GameCarousel", "Fall seven times, stand up eight."),
     QT_TRANSLATE_NOOP("GameCarousel", "Japanese proverb")},
    {QT_TRANSLATE_NOOP("GameCarousel", "Little by little, one travels far."),
     QT_TRANSLATE_NOOP("GameCarousel", "Spanish proverb")},
}};

} // Anonymous namespace

void GameCarousel::PickQuote() {
    // Never the same one twice in a row.
    const int count = static_cast<int>(Quotes.size());
    const int next = QRandomGenerator::global()->bounded(count - 1);
    m_quote = next >= m_quote ? next + 1 : next;
    viewport()->update();
}

void GameCarousel::showEvent(QShowEvent* event) {
    QListView::showEvent(event);
    PickQuote();
}

GameCarousel::GameCarousel(QWidget* parent) : QListView{parent} {
    m_quote = QRandomGenerator::global()->bounded(static_cast<int>(Quotes.size()));
    auto* const quote_timer = new QTimer(this);
    connect(quote_timer, &QTimer::timeout, this, &GameCarousel::PickQuote);
    quote_timer->start(10 * 60 * 1000);
    m_gameCard = new GameCard(this);
    setItemDelegate(m_gameCard);

    setViewMode(QListView::IconMode);
    setMovement(QListView::Static);
    setUniformItemSizes(true);
    setSelectionMode(QAbstractItemView::SingleSelection);

    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);

    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // The row glides by setting the bar's value; the stack has nothing to scroll.
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setContextMenuPolicy(Qt::CustomContextMenu);

    setSpacing(10);
    setWordWrap(true);
    setTextElideMode(Qt::ElideRight);
    setFlow(QListView::LeftToRight);
    setWrapping(false);

    // The home view: the background is painted here (paintEvent), the clock kept current.
    setObjectName(QStringLiteral("GameCarousel"));
    setFrameShape(QFrame::NoFrame);
    setMouseTracking(true);
    // Adding a game folder: the tree view has a row for it, the home view this button.
    m_addButton = new QPushButton(tr("+ Add games"), viewport());
    m_addButton->setObjectName(QStringLiteral("HomeAddGamesButton"));
    m_addButton->setCursor(Qt::PointingHandCursor);
    m_addButton->setToolTip(tr("Add a folder with games"));
    connect(m_addButton, &QPushButton::clicked, this, &GameCarousel::AddDirectoryRequested);
    m_userButton = new QPushButton(viewport());
    m_userButton->setObjectName(QStringLiteral("HomeUserButton"));
    m_userButton->setCursor(Qt::PointingHandCursor);
    m_userButton->setToolTip(tr("Switch user"));
    m_userButton->setVisible(false);
    connect(m_userButton, &QPushButton::clicked, this, [this] {
        emit UserMenuRequested(m_userButton->mapToGlobal(QPoint(0, m_userButton->height())));
    });
    m_customizeButton = new QPushButton(tr("Customize"), viewport());
    m_customizeButton->setObjectName(QStringLiteral("HomeUserButton")); // the same pill
    m_customizeButton->setCursor(Qt::PointingHandCursor);
    m_customizeButton->setToolTip(tr("Customize the home screen"));
    connect(m_customizeButton, &QPushButton::clicked, this, &GameCarousel::CustomizeRequested);
    m_controllersButton = new QPushButton(viewport());
    m_controllersButton->setObjectName(QStringLiteral("HomeUserButton")); // the same pill
    m_controllersButton->setCursor(Qt::PointingHandCursor);
    m_controllersButton->setToolTip(tr("Connected controllers and their buttons"));
    connect(m_controllersButton, &QPushButton::clicked, this,
            &GameCarousel::ControllersRequested);
    SetControllerCount(0);
    // Console hero play button: launches the picked game directly
    m_playButton = new QPushButton(tr("▶  Play Game"), viewport());
    m_playButton->setObjectName(QStringLiteral("HomeHeroPlayButton"));
    m_playButton->setCursor(Qt::PointingHandCursor);
    m_playButton->setToolTip(tr("Launch the selected game"));
    m_playButton->setVisible(false);
    connect(m_playButton, &QPushButton::clicked, this, [this] {
        if (currentIndex().isValid()) {
            emit activated(currentIndex());
        }
    });
    // The tiles' size: a slider in the bottom-right corner, remembered.
    m_sizeSlider = new QSlider(Qt::Horizontal, viewport());
    m_sizeSlider->setObjectName(QStringLiteral("HomeTileSizeSlider"));
    m_sizeSlider->setRange(60, 160);
    m_sizeSlider->setValue(static_cast<int>(UISettings::values.home_tile_scale.GetValue()));
    m_sizeSlider->setFixedWidth(150);
    m_sizeSlider->setCursor(Qt::PointingHandCursor);
    m_sizeSlider->setToolTip(tr("Game card size"));
    m_sizeIcon = new QLabel(viewport());
    m_sizeIcon->setPixmap(CardsIcon());
    m_sizeIcon->setScaledContents(true);
    m_sizeIcon->setFixedSize(20, 20);
    m_sizeIcon->setToolTip(tr("Game card size"));
    m_sizeIcon->setAttribute(Qt::WA_TranslucentBackground);
    connect(m_sizeSlider, &QSlider::valueChanged, this, [this](int value) {
        UISettings::values.home_tile_scale = static_cast<u32>(value);
        UpdateIconSize();
        viewport()->update();
    });
    // The menu sounds (util/menu_sounds.cpp: the game's music, the hum, clicks and the chime):
    // the speaker mutes them all, the slider next to it is their volume (the same setting as
    // Configure > General > UI > Sounds > Volume).
    m_musicButton = new QPushButton(viewport());
    m_musicButton->setObjectName(QStringLiteral("HomeMusicButton"));
    m_musicButton->setCursor(Qt::PointingHandCursor);
    m_musicButton->setFixedSize(32, 32);
    m_musicButton->setIconSize(QSize(20, 20));
    connect(m_musicButton, &QPushButton::clicked, this, [this] {
        const bool silent = UISettings::values.menu_muted.GetValue() ||
                            UISettings::values.menu_sound_volume.GetValue() == 0;
        if (silent && UISettings::values.menu_sound_volume.GetValue() == 0) {
            // Unmuting at volume 0 would still be silent: back to the default level.
            UISettings::values.menu_sound_volume = 60;
            m_volumeSlider->setValue(60);
        }
        UISettings::values.menu_muted = !silent;
        UpdateMusicButton();
    });
    m_volumeSlider = new QSlider(Qt::Horizontal, viewport());
    m_volumeSlider->setObjectName(QStringLiteral("HomeVolumeSlider"));
    m_volumeSlider->setRange(0, 100);
    m_volumeSlider->setValue(static_cast<int>(UISettings::values.menu_sound_volume.GetValue()));
    m_volumeSlider->setFixedWidth(110);
    m_volumeSlider->setCursor(Qt::PointingHandCursor);
    m_volumeSlider->setToolTip(tr("Music and sound volume"));
    connect(m_volumeSlider, &QSlider::valueChanged, this, [this](int value) {
        UISettings::values.menu_sound_volume = static_cast<u32>(value);
        // Moving the slider up unmutes.
        if (value > 0) {
            UISettings::values.menu_muted = false;
        }
        UpdateMusicButton();
    });
    UpdateMusicButton();

    // The backdrop moves at 30 frames a second while the view is shown and PhoenixEmu is the
    // active window (it drifts slowly; a frame cost ~1/4 of a CPU core at 60).
    m_scene = std::make_unique<PhoenixBackdrop::Scene>();
    m_frameClock = new QElapsedTimer();
    m_frameClock->start();
    auto* const motion = new QTimer(this);
    motion->setTimerType(Qt::PreciseTimer);
    connect(motion, &QTimer::timeout, this, [this] {
        const qreal dt = std::min(0.05, m_frameClock->restart() / 1000.0);
        if (!isVisible() || QGuiApplication::applicationState() != Qt::ApplicationActive) {
            return;
        }
        // Customize's background and animation choices, picked up as they change.
        m_scene->SetOptions(PhoenixBackdrop::Scene::Options::FromSettings());
        if (UISettings::values.carousel_banner_background.GetValue() != m_ambientSoft) {
            RebuildAmbient();
            m_ambientPrevious = {};
            viewport()->update();
        }
        if (!m_scene->Moving()) {
            // A still background: repaint only when something changed (no CPU spent per frame).
            if (m_scene->GetOptions() != m_lastStillOptions) {
                m_lastStillOptions = m_scene->GetOptions();
                viewport()->update();
            }
            return;
        }
        m_lastStillOptions = {};
        m_scene->Step(dt, QRectF(viewport()->rect()));
        viewport()->update();
    });
    motion->start(33);

    // The stack slides between games.
    m_positionAnimation = new QVariantAnimation(this);
    m_positionAnimation->setDuration(240);
    m_positionAnimation->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_positionAnimation, &QVariantAnimation::valueChanged, this,
            [this](const QVariant& value) {
                m_position = value.toReal();
                viewport()->update();
            });
}

GameCarousel::~GameCarousel() {
    delete m_frameClock;
}

void GameCarousel::RebuildAmbient() {
    m_ambientSoft = UISettings::values.carousel_banner_background.GetValue();
    m_ambient = PhoenixBackdrop::MakeAmbient(m_ambientSource, viewport()->size(), m_ambientSoft,
                                             m_ambientIsBanner);
}

void GameCarousel::SetAmbientSource(const QModelIndex& index) {
    const QPixmap banner = index.data(GameListItemPath::BannerRole).value<QPixmap>();
    m_ambientIsBanner = !banner.isNull();
    m_ambientSource =
        m_ambientIsBanner ? banner : index.data(GameListItemPath::LargePictureRole).value<QPixmap>();
}

void GameCarousel::resizeEvent(QResizeEvent* event) {
    QListView::resizeEvent(event);
    PlaceAddButton();
    if (m_ambient.size() != viewport()->size()) {
        RebuildAmbient();
        m_ambientPrevious = {};
    }
}

void GameCarousel::SetUserName(const QString& name) {
    if (m_userButton == nullptr) {
        return;
    }
    m_userButton->setText(QStringLiteral("☺  %1  ▾").arg(name));
    m_userButton->setVisible(!name.isEmpty());
    PlaceAddButton();
}

void GameCarousel::SetControllerCount(int count) {
    if (m_controllersButton == nullptr || count == m_controllerCount) {
        return;
    }
    m_controllerCount = count;
    m_controllersButton->setText(count == 0   ? tr("No controllers")
                                 : count == 1 ? tr("1 controller")
                                              : tr("%1 controllers").arg(count));
    PlaceAddButton();
}

void GameCarousel::PlaceAddButton() {
    if (m_addButton == nullptr) {
        return;
    }
    // Under the clock, on the right (the viewport has its final size only once it is shown).
    m_addButton->adjustSize();
    const int clock_bottom = 18 + std::clamp(viewport()->height() / 18, 18, 30) + 16;
    const QPoint place{viewport()->width() - 40 - m_addButton->width(), clock_bottom};
    if (m_addButton->pos() != place) {
        m_addButton->move(place);
    }
    int left_edge = place.x();
    if (m_userButton != nullptr && m_userButton->isVisible()) {
        m_userButton->adjustSize();
        const QPoint user_place{place.x() - 10 - m_userButton->width(), clock_bottom};
        if (m_userButton->pos() != user_place) {
            m_userButton->move(user_place);
        }
        left_edge = user_place.x();
    }
    if (m_customizeButton != nullptr) {
        m_customizeButton->adjustSize();
        const QPoint brush_place{left_edge - 10 - m_customizeButton->width(), clock_bottom};
        if (m_customizeButton->pos() != brush_place) {
            m_customizeButton->move(brush_place);
        }
        left_edge = brush_place.x();
    }
    if (m_controllersButton != nullptr) {
        m_controllersButton->adjustSize();
        const QPoint pads_place{left_edge - 10 - m_controllersButton->width(), clock_bottom};
        if (m_controllersButton->pos() != pads_place) {
            m_controllersButton->move(pads_place);
        }
    }
    if (m_playButton != nullptr) {
        const bool has_game = currentIndex().isValid();
        m_playButton->setVisible(has_game);
        if (has_game) {
            m_playButton->adjustSize();
            const int play_y = viewport()->height() - 36 - (m_playButton->height() - 20) / 2;
            const QPoint play_place{40, play_y};
            if (m_playButton->pos() != play_place) {
                m_playButton->move(play_place);
            }
        }
    }
    if (m_sizeSlider != nullptr) {
        // The stack's card size is Customize's; the slider sizes the row's tiles.
        m_sizeSlider->setVisible(!Stacked());
        m_sizeIcon->setVisible(!Stacked());
        const QPoint slider_place{viewport()->width() - 40 - m_sizeSlider->width(),
                                  viewport()->height() - 36};
        if (m_sizeSlider->pos() != slider_place) {
            m_sizeSlider->move(slider_place);
        }
        const int middle = slider_place.y() + m_sizeSlider->height() / 2;
        const QPoint icon_place{slider_place.x() - 8 - m_sizeIcon->width(),
                                middle - m_sizeIcon->height() / 2};
        if (m_sizeIcon->pos() != icon_place) {
            m_sizeIcon->move(icon_place);
        }
        if (m_musicButton != nullptr && m_volumeSlider != nullptr) {
            const QPoint volume_place{icon_place.x() - 28 - m_volumeSlider->width(),
                                      middle - m_volumeSlider->height() / 2};
            if (m_volumeSlider->pos() != volume_place) {
                m_volumeSlider->move(volume_place);
            }
            const QPoint music_place{volume_place.x() - 8 - m_musicButton->width(),
                                     middle - m_musicButton->height() / 2};
            if (m_musicButton->pos() != music_place) {
                m_musicButton->move(music_place);
            }
            // Configure may have changed them.
            const int volume = static_cast<int>(UISettings::values.menu_sound_volume.GetValue());
            if (m_volumeSlider->value() != volume && !m_volumeSlider->isSliderDown()) {
                const QSignalBlocker blocker{m_volumeSlider};
                m_volumeSlider->setValue(volume);
            }
            const bool playing = !UISettings::values.menu_muted.GetValue() && volume > 0;
            if (m_musicButton->property("playing").toBool() != playing) {
                UpdateMusicButton();
            }
        }
    }
}

void GameCarousel::UpdateButtonContrast() {
    if (m_addButton == nullptr || m_userButton == nullptr) {
        return;
    }
    // Their pill is a light wash of the accent over whatever is behind it.
    const QColor accent = PhoenixTheme::Recolor(QColor(0xFF, 0xC8, 0x3D));
    QColor hardest;
    for (QPushButton* const button :
         {m_addButton, m_userButton, m_customizeButton, m_controllersButton}) {
        if (!button->isVisible()) {
            continue;
        }
        const QColor behind = m_scene->AverageBehind(button->geometry());
        if (!behind.isValid()) {
            return;
        }
        const QColor pill = QColor::fromRgbF(behind.redF() * 0.85 + accent.redF() * 0.15,
                                             behind.greenF() * 0.85 + accent.greenF() * 0.15,
                                             behind.blueF() * 0.85 + accent.blueF() * 0.15);
        if (!hardest.isValid() || PhoenixTheme::Luminance(pill) > PhoenixTheme::Luminance(hardest)) {
            hardest = pill; // the lightest pill is the hardest to read on
        }
    }
    if (!hardest.isValid()) {
        return;
    }
    const QColor text = PhoenixTheme::Readable(PhoenixTheme::Recolor(QColor(0xFF, 0xD9, 0x66)), hardest);
    const QString name = text.name(QColor::HexRgb);
    if (name == m_buttonTextColor) {
        return;
    }
    m_buttonTextColor = name;
    const QString style = QStringLiteral("QPushButton:!hover { color: %1; }").arg(name);
    m_addButton->setStyleSheet(style);
    m_userButton->setStyleSheet(style);
    m_customizeButton->setStyleSheet(style);
    m_controllersButton->setStyleSheet(style);
}

void GameCarousel::UpdateMusicButton() {
    const bool playing = !UISettings::values.menu_muted.GetValue() &&
                         UISettings::values.menu_sound_volume.GetValue() > 0;
    m_musicButton->setProperty("playing", playing);
    m_musicButton->setIcon(SpeakerIcon(playing));
    m_musicButton->setToolTip(playing ? tr("Mute the music and sounds")
                                      : tr("Turn the music and sounds back on"));
}

void GameCarousel::paintEvent(QPaintEvent* event) {
    PlaceAddButton();
    {
        QPainter painter{viewport()};
        painter.setRenderHint(QPainter::Antialiasing);
        const QRect area = viewport()->rect();

        if (m_ambient.size() != area.size() && !m_ambientSource.isNull()) {
            RebuildAmbient();
        }
        // The picked game's picture: a soft glow of its colours, or (banner) nearly the picture.
        const qreal strength = m_ambientSoft ? 0.72 : 0.5;
        m_scene->Paint(painter, area, m_ambient, strength * m_ambientFade, m_ambientPrevious,
                       strength * (1.0 - m_ambientFade));

        // A greeting on the left, the clock on the right.
        const int hour = QTime::currentTime().hour();
        const QString greeting = hour < 5    ? tr("Up late?")
                                 : hour < 12 ? tr("Good morning!")
                                 : hour < 18 ? tr("Good afternoon!")
                                             : tr("Good evening!");
        QFont big = font();
        big.setBold(true);
        big.setPixelSize(std::clamp(area.height() / 18, 18, 30));
        QFont small = font();
        small.setPixelSize(std::clamp(area.height() / 34, 12, 16));
        const int left = 40;
        const int top = 18;
        // The text stands on the game's blurred picture and the theme's colours: each line is
        // made readable against what is behind it, with a shadow of the opposite brightness.
        const auto draw_readable = [&](const QPoint& baseline, const QString& text,
                                       const QColor& color) {
            const QFontMetrics metrics{painter.font()};
            const QRect box{baseline.x(), baseline.y() - metrics.ascent(),
                            metrics.horizontalAdvance(text), metrics.height()};
            const QColor behind = m_scene->AverageBehind(box);
            const QColor shown = behind.isValid() ? PhoenixTheme::Readable(color, behind) : color;
            const bool light = PhoenixTheme::Luminance(shown) > 0.35;
            painter.setPen(light ? QColor(0, 0, 0, 120) : QColor(255, 255, 255, 110));
            painter.drawText(baseline + QPoint(1, 1), text);
            painter.setPen(shown);
            painter.drawText(baseline, text);
        };
        painter.setFont(big);
        const QFontMetrics big_metrics{big};
        draw_readable(QPoint(left, top + big_metrics.ascent()), greeting,
                      PhoenixTheme::Recolor(QColor(0xFF, 0xF4, 0xDA)));
        painter.setFont(small);
        const QFontMetrics small_metrics{small};
        draw_readable(QPoint(left + 2, top + big_metrics.height() + small_metrics.ascent()),
                      model() != nullptr && model()->rowCount() > 0
                          ? tr("Pick a game and let's play.")
                          : tr("Add a game folder to get started."),
                      PhoenixTheme::Recolor(QColor(0xE8, 0xD9, 0xB5, 190)));
        // A quote under it, smaller, in italics and fainter, cut short with "..." if the window
        // is narrow (it stays clear of the clock).
        QFont tiny = font();
        tiny.setItalic(true);
        tiny.setPixelSize(std::clamp(area.height() / 44, 11, 13));
        const QFontMetrics tiny_metrics{tiny};
        const Quote& quote = Quotes[static_cast<size_t>(m_quote) % Quotes.size()];
        const QString line = QStringLiteral("\u201C%1\u201D  \u2014 %2")
                                 .arg(tr(quote.text), tr(quote.author));
        const int room = area.width() - 2 * left -
                         big_metrics.horizontalAdvance(QStringLiteral("00:00")) - 24;
        const QString shown = tiny_metrics.elidedText(line, Qt::ElideRight, (std::max)(room, 80));
        const QPoint at{left + 2, top + big_metrics.height() + small_metrics.height() + 4 +
                                      tiny_metrics.ascent()};
        painter.setFont(tiny);
        draw_readable(at, shown, PhoenixTheme::Recolor(QColor(0xF0, 0xE2, 0xC0, 175)));
        painter.setFont(big);
        const QString time = QTime::currentTime().toString(QStringLiteral("h:mm"));
        draw_readable(QPoint(area.width() - left - big_metrics.horizontalAdvance(time),
                             top + big_metrics.ascent()),
                      time, PhoenixTheme::Recolor(QColor(0xFF, 0xC8, 0x3D)));

        if (currentIndex().isValid() && m_playButton != nullptr && m_playButton->isVisible()) {
            const QModelIndex current = currentIndex();
            const u64 pid = current.data(GameListItemPath::ProgramIdRole).toULongLong();
            const int hero_y = m_playButton->y() + m_playButton->height() / 2;
            const int text_x = m_playButton->x() + m_playButton->width() + 16;

            QFont meta_font = font();
            meta_font.setBold(true);
            meta_font.setPixelSize(11);
            painter.setFont(meta_font);
            const QFontMetrics meta_metrics{meta_font};

            const QString badge = current.data(GameListItemPath::Switch2Role).toBool()
                                      ? tr("SWITCH 2")
                                      : tr("SWITCH");
            const int badge_w = meta_metrics.horizontalAdvance(badge) + 14;
            const int badge_h = 22;
            const QRect badge_rect{text_x, hero_y - badge_h / 2, badge_w, badge_h};
            painter.setPen(Qt::NoPen);
            painter.setBrush(PhoenixTheme::Recolor(QColor(0xFF, 0x52, 0x30, 50)));
            painter.drawRoundedRect(badge_rect, 5, 5);
            painter.setPen(PhoenixTheme::Recolor(QColor(0xFF, 0x8A, 0x3D)));
            painter.drawRoundedRect(badge_rect, 5, 5);
            painter.drawText(badge_rect, Qt::AlignCenter, badge);

            if (pid != 0) {
                const QString id_str = QStringLiteral("0x%1").arg(pid, 16, 16, QLatin1Char('0')).toUpper();
                painter.setPen(PhoenixTheme::Recolor(QColor(0xE8, 0xD9, 0xB5, 175)));
                painter.drawText(QPoint(text_x + badge_w + 12, hero_y + meta_metrics.ascent() / 2), id_str);
            }
        }
    }
    UpdateButtonContrast();
    if (Stacked()) {
        QPainter painter{viewport()};
        PaintStack(painter);
        return;
    }
    QListView::paintEvent(event);
}

void GameCarousel::SetModel(GameListModel* model) {
    QListView::setModel(model);
    UpdateIconSize();
    // Games arrive after the view is set up (the scan runs in the background): pick the first
    // one as soon as there is one, as a console's home screen does.
    const auto pick_first = [this] {
        if (!currentIndex().isValid() && this->model() != nullptr && this->model()->rowCount() > 0) {
            setCurrentIndex(this->model()->index(0, 0));
        }
    };
    connect(model, &QAbstractItemModel::rowsInserted, this, pick_first);
    connect(model, &QAbstractItemModel::modelReset, this, pick_first);
    // Pictures are looked up after the games are listed: the picked game's may arrive later.
    connect(model, &QAbstractItemModel::dataChanged, this,
            [this](const QModelIndex& top_left, const QModelIndex& bottom_right) {
                const QModelIndex current = currentIndex();
                if (!current.isValid() || current.row() < top_left.row() ||
                    current.row() > bottom_right.row()) {
                    return;
                }
                const QPixmap before = m_ambientSource;
                SetAmbientSource(current);
                if (before.cacheKey() != m_ambientSource.cacheKey()) {
                    RebuildAmbient();
                    viewport()->update();
                }
            });

    // The picked tile springs up: a short animation with a little overshoot.
    if (m_liftAnimation == nullptr) {
        m_liftAnimation = new QVariantAnimation(this);
        m_liftAnimation->setStartValue(0.0);
        m_liftAnimation->setEndValue(1.0);
        m_liftAnimation->setDuration(380);
        m_liftAnimation->setEasingCurve(QEasingCurve::OutBack);
        connect(m_liftAnimation, &QVariantAnimation::valueChanged, this,
                [this](const QVariant& lift) {
                    m_gameCard->setLift(lift.toReal());
                    viewport()->update();
                });
    }
    connect(selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex& current) {
                m_liftAnimation->stop();
                m_liftAnimation->start();
                PlaceAddButton();
                // The backdrop takes the picked game's colours, cross-fading from the last.
                m_ambientPrevious = m_ambient;
                SetAmbientSource(current);
                RebuildAmbient();
                if (m_ambientAnimation == nullptr) {
                    m_ambientAnimation = new QVariantAnimation(this);
                    m_ambientAnimation->setStartValue(0.0);
                    m_ambientAnimation->setEndValue(1.0);
                    m_ambientAnimation->setDuration(600);
                    m_ambientAnimation->setEasingCurve(QEasingCurve::InOutSine);
                    connect(m_ambientAnimation, &QVariantAnimation::valueChanged, this,
                            [this](const QVariant& fade) { m_ambientFade = fade.toReal(); });
                }
                m_ambientAnimation->stop();
                m_ambientAnimation->start();
                if (!current.isValid()) {
                    return;
                }
                if (Stacked()) {
                    MoveTo(PositionOf(current.row()));
                    return;
                }
                // Glide the row so the picked game is in view (no jump).
                const QRect tile = visualRect(current);
                QScrollBar* const bar = horizontalScrollBar();
                const int target = std::clamp(
                    bar->value() + tile.center().x() - viewport()->width() / 2, bar->minimum(),
                    bar->maximum());
                if (target == bar->value()) {
                    return;
                }
                auto* const glide = new QPropertyAnimation(bar, "value", this);
                glide->setDuration(320);
                glide->setEasingCurve(QEasingCurve::OutCubic);
                glide->setStartValue(bar->value());
                glide->setEndValue(target);
                glide->start(QAbstractAnimation::DeleteWhenStopped);
            });
}

void GameCarousel::ApplyFilter(const QString& edit_filter_text, GameListModel* model) {
    int row_count = model->rowCount();

    for (int i = 0; i < row_count; ++i) {
        QStandardItem* item = model->item(i, 0);
        if (!item)
            continue;

        if (Phoenix::FilterMatches(edit_filter_text, item)) {
            setRowHidden(i, false);
        } else {
            setRowHidden(i, true);
        }
    }
}

void GameCarousel::UpdateIconSize() {
    // Big tiles that follow the window's height, whatever the game list's icon size.
    const int tile =
        std::clamp(static_cast<int>(viewport()->height() * 9 / 20 *
                                    UISettings::values.home_tile_scale.GetValue() / 100),
                   90, 460);
    const QSize grid_size(tile + tile / 3, std::max(tile + 120, viewport()->height()));
    if (gridSize() != grid_size) {
        setUpdatesEnabled(false);
        setGridSize(grid_size);
        m_gameCard->setSize(grid_size, QSize(tile, tile), 0, 0);
        setUpdatesEnabled(true);
    }
    // Something is always picked, as on a console's home screen.
    if (!currentIndex().isValid() && model() != nullptr && model()->rowCount() > 0) {
        setCurrentIndex(model()->index(0, 0));
    }
}

QModelIndex GameCarousel::indexAt(const QPoint& point) const {
    if (Stacked()) {
        return StackIndexAt(point);
    }
    QModelIndex index = QListView::indexAt(point);
    if (!index.isValid())
        return {};
    if (m_gameCard && !m_gameCard->hitTest(point, index, this, visualRect(index)))
        return {};
    return index;
}

// The stack: one big card in the middle, its neighbours tucked behind it on both sides, each
// smaller, tilted and fogged with its distance (geometry after Eglantine's home screen
// redesign, src/yuzu/game/carousel.cpp on its ui/homescreen-redesign branch).
namespace {

constexpr std::array<qreal, 5> CardSizeFractions{1.0, 0.75, 2.0 / 3.0, 0.5, 1.0 / 3.0};
constexpr qreal PeekFraction = 0.12;
constexpr qreal ScaleStep = 0.90;
constexpr qreal MinScale = 0.40;
constexpr qreal TiltSqueeze = 0.14;
constexpr qreal TiltHeight = 0.95;
constexpr int FogPerStep = 55;
constexpr int FogMax = 170;
constexpr int StackTitleReserve = 56;

struct StackMetrics {
    QPointF center;
    qreal card_w = 0;
    qreal card_h = 0;
    qreal peek = 0;
};

StackMetrics MetricsFor(const QRect& area) {
    StackMetrics m;
    const size_t size_index =
        std::min<size_t>(UISettings::values.carousel_card_size.GetValue(), CardSizeFractions.size() - 1);
    m.card_h = std::max<qreal>(120.0, area.height() * CardSizeFractions[size_index]);
    m.card_w = GameCard::IsFramed(GameCard::CurrentStyle())
                   ? m.card_h * GameCard::FrameAspect(GameCard::CurrentStyle())
                   : m.card_h;
    m.center = QRectF(area).center();
    m.peek = std::clamp(m.card_w * PeekFraction, qreal(28), qreal(96));
    return m;
}

/// The card's untilted rectangle `depth` steps from the middle: a card further out tucks under
/// the one in front, peeking `peek` past its edge. Fractional depths slide smoothly.
QRectF CardRect(qreal depth, const StackMetrics& m) {
    const qreal d = std::abs(depth);
    const qreal fade = std::min(d, 1.0);
    const qreal scale = std::max(MinScale, std::pow(ScaleStep, d));
    const qreal w = m.card_w * scale * (1.0 - TiltSqueeze * fade);
    const qreal h = m.card_h * scale;
    const qreal y = m.center.y() - h / 2.0;
    const qreal centered = m.center.x() - m.card_w / 2.0;
    if (depth > 0.0) {
        const qreal stacked = m.center.x() + m.card_w / 2.0 + m.peek - w;
        const qreal left = depth < 1.0 ? centered + (stacked - centered) * depth
                                       : stacked + (depth - 1.0) * m.peek;
        return {left, y, w, h};
    }
    if (depth < 0.0) {
        const qreal stacked = m.center.x() - m.card_w / 2.0 - m.peek;
        const qreal left = depth > -1.0 ? centered + (stacked - centered) * (-depth)
                                        : stacked + (depth + 1.0) * m.peek;
        return {left, y, w, h};
    }
    return {m.center.x() - w / 2.0, y, w, h};
}

/// Solid near the middle, dissolving at the stack's far ends.
qreal StackAlpha(qreal depth, int count) {
    const qreal d = std::abs(depth);
    const qreal solid = std::max<qreal>(1.0, count - 2.0);
    if (d <= solid) {
        return 1.0;
    }
    const qreal t = std::min<qreal>(1.0, (d - solid) / std::max<qreal>(1.0, count - solid));
    return 1.0 - t * t * (3.0 - 2.0 * t);
}

/// The game's picture cover-cropped to `size` device pixels, once per picture and size.
QPixmap CoverArt(const QPixmap& picture, QSize size) {
    if (picture.isNull() || size.isEmpty()) {
        return {};
    }
    // In 16 px steps, so the slide reuses a few sizes.
    size = QSize((size.width() + 15) / 16 * 16, (size.height() + 15) / 16 * 16);
    const QString key = QStringLiteral("phoenix-stack-%1-%2x%3")
                            .arg(picture.cacheKey())
                            .arg(size.width())
                            .arg(size.height());
    QPixmap art;
    if (QPixmapCache::find(key, &art)) {
        return art;
    }
    const QPixmap scaled =
        picture.scaled(size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    art = scaled.copy((scaled.width() - size.width()) / 2, (scaled.height() - size.height()) / 2,
                      size.width(), size.height());
    QPixmapCache::insert(key, art);
    return art;
}

QPixmap PictureOf(const QModelIndex& index) {
    QPixmap picture = index.data(GameListItemPath::LargePictureRole).value<QPixmap>();
    if (picture.isNull()) {
        picture = index.data(Qt::DecorationRole).value<QPixmap>();
    }
    return picture;
}


/// The Shelf layout (the Case look): the cases standing side by side like a bookshelf, spines
/// out, the picked one pulled out and turned to show its front (after PyroCade's bookshelf).
bool ShelfLayout() {
    return UISettings::values.carousel_layout.GetValue() == 2 &&
           GameCard::CurrentStyle() == GameCard::Style::Case;
}

/// A spine's width on the shelf, and the gap between spines.
qreal ShelfPitch(const StackMetrics& m) {
    return m.card_w * 0.92 * 0.098 * 1.08 + 3.0;
}

/// The case rectangle (its full size, centred where it stands) `depth` places from the middle.
QRectF ShelfRect(qreal depth, const StackMetrics& m) {
    const qreal d = std::abs(depth);
    const qreal pitch = ShelfPitch(m);
    // The picked case turned to its rest angle spans about 0.4 of its width either side.
    const qreal first = m.card_w * 0.42 + 6.0 + pitch / 2;
    const qreal offset = d < 1.0 ? first * d : first + (d - 1.0) * pitch;
    const qreal x = m.center.x() + (depth < 0 ? -offset : offset);
    return {x - m.card_w / 2, m.center.y() - m.card_h / 2, m.card_w, m.card_h};
}

/// How a case on the shelf is turned: the picked one shows its front, the others their spines.
qreal ShelfAngle(qreal depth) {
    return CaseRestAngle + (90.0 - CaseRestAngle) * std::min(std::abs(depth), 1.0);
}

void DrawStackCard(QPainter& p, const QModelIndex& index, const QRectF& rect, qreal depth,
                   qreal alpha, qreal dpr, qreal case_angle) {
    const qreal d = std::abs(depth);
    const bool centered = d < 0.01;
    const GameCard::Style style = GameCard::CurrentStyle();
    const bool cartridge = GameCard::IsFramed(style);
    const QRectF local{0, 0, rect.width(), rect.height()};

    // The card maps onto a trapezoid: the edge facing the middle keeps its height, the far edge
    // is shorter, so it reads as turned away.
    QTransform tilt = QTransform::fromTranslate(rect.left(), rect.top());
    if (!centered) {
        const qreal far_h = rect.height() * (1.0 + (TiltHeight - 1.0) * std::min(d, 1.0));
        const qreal yc = rect.center().y();
        const QPolygonF from{local.topLeft(), local.topRight(), local.bottomRight(),
                             local.bottomLeft()};
        QPolygonF to;
        if (depth > 0.0) {
            to << rect.topLeft() << QPointF(rect.right(), yc - far_h / 2)
               << QPointF(rect.right(), yc + far_h / 2) << rect.bottomLeft();
        } else {
            to << QPointF(rect.left(), yc - far_h / 2) << rect.topRight() << rect.bottomRight()
               << QPointF(rect.left(), yc + far_h / 2);
        }
        if (!QTransform::quadToQuad(from, to, tilt)) {
            return;
        }
    }

    p.save();
    p.setOpacity(alpha);
    p.setTransform(tilt);
    QPixmap picture = PictureOf(index);
    const QRectF body = cartridge ? GameCard::FrameBody(style, local) : local;
    const qreal radius =
        cartridge ? GameCard::FrameRadius(style, local.width()) : local.width() * 0.08;
    if (centered && style == GameCard::Style::Case) {
        DrawCaseBox(p, index, local, dpr, case_angle, picture);
        p.restore();
        return;
    }
    if (centered) {
        // A soft shadow under the card in the middle.
        p.setPen(Qt::NoPen);
        for (int spread = 1; spread <= 5; ++spread) {
            p.setBrush(QColor(0, 0, 0, 14));
            p.drawRoundedRect(body.adjusted(-spread, -spread + 3, spread, spread + 3),
                              radius + spread, radius + spread);
        }
    }
    QPainterPath body_clip;
    body_clip.addRoundedRect(body, radius, radius);
    p.setClipPath(body_clip);
    QRectF art_rect = local;
    if (cartridge) {
        QPixmap frame;
        if (style == GameCard::Style::Case) {
            GameCard::CaseParts(index, local, qRound(local.height()), dpr, frame, art_rect,
                                picture);
        } else {
            frame = GameCard::FramePixmap(style, qRound(local.height()), dpr,
                                          index.data(GameListItemPath::Switch2Role).toBool());
            art_rect = GameCard::FrameArtRect(style, local);
        }
        p.drawPixmap(local, frame, QRectF(frame.rect()));
        QPainterPath label_clip;
        label_clip.addRoundedRect(art_rect, art_rect.width() * 0.04, art_rect.width() * 0.04);
        p.setClipPath(label_clip, Qt::IntersectClip);
    }
    const QPixmap art = CoverArt(picture, (art_rect.size() * dpr).toSize());
    if (!art.isNull()) {
        p.drawPixmap(art_rect, art, QRectF(art.rect()));
    } else {
        QLinearGradient fill{art_rect.topLeft(), art_rect.bottomRight()};
        fill.setColorAt(0, PhoenixTheme::Recolor(QColor(0xFF, 0xC8, 0x3D)));
        fill.setColorAt(1, PhoenixTheme::Recolor(QColor(0xE8, 0x9E, 0x10)));
        p.fillRect(art_rect, fill);
    }
    p.setClipPath(body_clip);
    if (style == GameCard::Style::Badge) {
        p.drawPixmap(local,
                     GameCard::PlatformBorderPixmap(index.data(GameListItemPath::Switch2Role).toBool(),
                                                    qRound(local.width()), dpr),
                     QRectF(QPointF(0, 0), QSizeF(qRound(local.width() * dpr),
                                                  qRound(local.width() * dpr))));
    }
    // Further back, further into the fog.
    if (!centered) {
        p.fillRect(body, PhoenixTheme::Recolor(QColor(0x14, 0x10, 0x0C,
                                                      std::min(FogMax, qRound(d * FogPerStep)))));
    }
    p.setClipping(false);
    if (centered && style == GameCard::Style::Plain) {
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(PhoenixTheme::Recolor(QColor(0xFF, 0xC8, 0x3D)), 3));
        p.drawRoundedRect(local.adjusted(1.5, 1.5, -1.5, -1.5), radius, radius);
    }
    p.restore();
}

} // Anonymous namespace

bool GameCarousel::Stacked() {
    // The shelf (2) slides like the stack, with the cases standing side by side.
    return UISettings::values.carousel_layout.GetValue() == 0 ||
           UISettings::values.carousel_layout.GetValue() == 2;
}

std::vector<int> GameCarousel::VisibleRows() const {
    std::vector<int> rows;
    if (model() == nullptr) {
        return rows;
    }
    for (int row = 0; row < model()->rowCount(); ++row) {
        if (!isRowHidden(row)) {
            rows.push_back(row);
        }
    }
    return rows;
}

int GameCarousel::PositionOf(int row) const {
    const std::vector<int> rows = VisibleRows();
    const auto it = std::find(rows.begin(), rows.end(), row);
    return it == rows.end() ? -1 : static_cast<int>(it - rows.begin());
}

void GameCarousel::MoveTo(int position) {
    if (position < 0) {
        return;
    }
    m_positionAnimation->stop();
    if (!isVisible() || std::abs(m_position - position) > 12.0) {
        m_position = position; // a jump across the list: no slide past every game
        viewport()->update();
        return;
    }
    m_positionAnimation->setStartValue(m_position);
    m_positionAnimation->setEndValue(qreal(position));
    m_positionAnimation->start();
}

QRect GameCarousel::StackArea() const {
    // Below the greeting and the buttons under the clock, above the play button row.
    const int clock_bottom = 18 + std::clamp(viewport()->height() / 18, 18, 30) + 16;
    const int top = clock_bottom + 44;
    const int title = UISettings::values.show_game_name.GetValue() ? StackTitleReserve : 8;
    const int bottom = viewport()->height() - 64 - title;
    return QRect(0, top, viewport()->width(), std::max(120, bottom - top));
}

void GameCarousel::PaintStack(QPainter& p) {
    const std::vector<int> rows = VisibleRows();
    if (rows.empty()) {
        return;
    }
    // Rows still arriving (the scan) or a filter move the picked game: follow it when no slide
    // is running.
    if (m_positionAnimation->state() != QAbstractAnimation::Running && currentIndex().isValid()) {
        const auto it = std::find(rows.begin(), rows.end(), currentIndex().row());
        if (it != rows.end()) {
            m_position = static_cast<qreal>(it - rows.begin());
        }
    }
    m_position = std::clamp(m_position, 0.0, qreal(rows.size() - 1));
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    const StackMetrics m = MetricsFor(StackArea());
    const int count = static_cast<int>(UISettings::values.carousel_side_count.GetValue());
    const qreal dpr = p.device() ? p.device()->devicePixelRatioF() : 1.0;

    struct Card {
        int position;
        qreal depth;
    };
    std::vector<Card> cards;
    const int base = static_cast<int>(std::floor(m_position));
    for (int position = std::max(0, base - count - 1);
         position <= std::min<int>(static_cast<int>(rows.size()) - 1, base + count + 1);
         ++position) {
        cards.push_back({position, position - m_position});
    }
    // The farthest first, so the middle card sits on top.
    std::sort(cards.begin(), cards.end(), [](const Card& a, const Card& b) {
        if (!qFuzzyCompare(std::abs(a.depth) + 1.0, std::abs(b.depth) + 1.0)) {
            return std::abs(a.depth) > std::abs(b.depth);
        }
        return a.depth > b.depth;
    });
    if (ShelfLayout()) {
        // The shelf under the cases.
        const qreal floor_y = m.center.y() + m.card_h * 0.47;
        const QRectF plank{0, floor_y, static_cast<qreal>(viewport()->width()), m.card_h * 0.045};
        QLinearGradient wood{plank.topLeft(), plank.bottomLeft()};
        wood.setColorAt(0.0, QColor(0x5A, 0x3E, 0x28));
        wood.setColorAt(0.25, QColor(0x3E, 0x2A, 0x1A));
        wood.setColorAt(1.0, QColor(0x22, 0x16, 0x0D));
        p.fillRect(plank, wood);
        p.fillRect(QRectF(plank.left(), plank.top(), plank.width(), 1.5), QColor(255, 230, 190, 70));
        QLinearGradient under{plank.bottomLeft(), QPointF(plank.left(), plank.bottom() + 18)};
        under.setColorAt(0.0, QColor(0, 0, 0, 90));
        under.setColorAt(1.0, QColor(0, 0, 0, 0));
        p.fillRect(QRectF(plank.left(), plank.bottom(), plank.width(), 18), under);
        // Every case that fits, the far ones first so the middle one sits on top.
        const qreal pitch = ShelfPitch(m);
        const int reach = static_cast<int>(viewport()->width() / 2 / pitch) + 2;
        std::vector<Card> shelf;
        for (int position = std::max(0, base - reach);
             position <= std::min<int>(static_cast<int>(rows.size()) - 1, base + reach);
             ++position) {
            shelf.push_back({position, position - m_position});
        }
        std::sort(shelf.begin(), shelf.end(), [](const Card& a, const Card& b) {
            return std::abs(a.depth) > std::abs(b.depth);
        });
        for (const Card& card : shelf) {
            const QRectF rect = ShelfRect(card.depth, m);
            if (rect.center().x() + pitch < 0.0 || rect.center().x() - pitch > viewport()->width()) {
                continue;
            }
            const QModelIndex index = model()->index(rows[static_cast<size_t>(card.position)], 0);
            QPixmap picture = index.data(GameListItemPath::LargePictureRole).value<QPixmap>();
            if (picture.isNull()) {
                picture = index.data(Qt::DecorationRole).value<QPixmap>();
            }
            DrawCaseBox(p, index, rect, dpr, ShelfAngle(card.depth), picture);
        }
        cards = shelf;
        std::sort(cards.begin(), cards.end(), [](const Card& a, const Card& b) {
            return std::abs(a.depth) > std::abs(b.depth);
        });
    } else
    for (const Card& card : cards) {
        const qreal alpha = StackAlpha(card.depth, count);
        const QRectF rect = CardRect(card.depth, m);
        if (alpha <= 0.0 || rect.right() < 0.0 || rect.left() >= viewport()->width()) {
            continue;
        }
        DrawStackCard(p, model()->index(rows[static_cast<size_t>(card.position)], 0), rect,
                      card.depth, alpha, dpr, CaseRestAngle);
    }

    // The middle card's name in a pill under it, fading in as it settles.
    if (!UISettings::values.show_game_name.GetValue() || cards.empty()) {
        return;
    }
    const Card& nearest = cards.back();
    if (std::abs(nearest.depth) >= 0.5) {
        return;
    }
    const QString title = model()
                              ->index(rows[static_cast<size_t>(nearest.position)], 0)
                              .data(Qt::DisplayRole)
                              .toString()
                              .split(QLatin1Char('\n'))
                              .first();
    QFont font = p.font();
    font.setBold(true);
    font.setPixelSize(std::clamp(static_cast<int>(m.card_w / 13), 13, 22));
    const QFontMetrics metrics{font};
    const QString shown = metrics.elidedText(title, Qt::ElideRight, static_cast<int>(m.card_w * 1.6));
    const int width = metrics.horizontalAdvance(shown);
    const QRectF slot = CardRect(0.0, m);
    const QRectF pill{slot.center().x() - width / 2.0 - 16, slot.bottom() + 14, width + 32.0,
                      metrics.height() + 12.0};
    p.setOpacity(std::max(0.0, 1.0 - 2.0 * std::abs(nearest.depth)));
    p.setFont(font);
    p.setPen(QPen(PhoenixTheme::Recolor(QColor(0xFF, 0xC8, 0x3D)).darker(150), 1));
    p.setBrush(PhoenixTheme::Recolor(QColor(0x20, 0x16, 0x10, 235)));
    p.drawRoundedRect(pill, pill.height() / 2.0, pill.height() / 2.0);
    p.setPen(PhoenixTheme::Recolor(QColor(0xFF, 0xF4, 0xDA)));
    p.drawText(pill, Qt::AlignCenter, shown);
    p.setOpacity(1.0);
}

QModelIndex GameCarousel::StackIndexAt(const QPoint& point) const {
    const std::vector<int> rows = VisibleRows();
    if (rows.empty()) {
        return {};
    }
    const StackMetrics m = MetricsFor(StackArea());
    const GameCard::Style style = GameCard::CurrentStyle();
    const bool cartridge = GameCard::IsFramed(style);
    const int count = static_cast<int>(UISettings::values.carousel_side_count.GetValue()) + 1;
    int best = -1;
    qreal best_depth = 0;
    const bool shelf = ShelfLayout();
    const qreal pitch = ShelfPitch(m);
    for (int position = 0; position < static_cast<int>(rows.size()); ++position) {
        const qreal depth = position - m_position;
        if (!shelf && std::abs(depth) > count) {
            continue;
        }
        QRectF hit;
        if (shelf) {
            // The picked case: its turned front; the others: their spines.
            const QRectF rect = ShelfRect(depth, m);
            const qreal half = std::abs(depth) < 0.5 ? m.card_w * 0.42 : pitch / 2;
            hit = QRectF(rect.center().x() - half, rect.top(), half * 2, rect.height());
        } else {
            const QRectF rect = CardRect(depth, m);
            hit = cartridge ? GameCard::FrameBody(style, rect) : rect;
        }
        if (hit.contains(QPointF(point)) && (best < 0 || std::abs(depth) < std::abs(best_depth))) {
            best = position;
            best_depth = depth;
        }
    }
    return best < 0 ? QModelIndex{} : model()->index(rows[static_cast<size_t>(best)], 0);
}

QRect GameCarousel::visualRect(const QModelIndex& index) const {
    if (!Stacked()) {
        return QListView::visualRect(index);
    }
    const int position = index.isValid() ? PositionOf(index.row()) : -1;
    if (position < 0) {
        return {};
    }
    return CardRect(position - m_position, MetricsFor(StackArea())).toAlignedRect();
}

void GameCarousel::scrollTo(const QModelIndex& index, ScrollHint hint) {
    if (!Stacked()) {
        QListView::scrollTo(index, hint);
        return;
    }
    if (index.isValid()) {
        MoveTo(PositionOf(index.row()));
    }
}

QModelIndex GameCarousel::moveCursor(CursorAction cursor_action, Qt::KeyboardModifiers modifiers) {
    if (!Stacked()) {
        return QListView::moveCursor(cursor_action, modifiers);
    }
    const std::vector<int> rows = VisibleRows();
    if (rows.empty()) {
        return {};
    }
    const int last = static_cast<int>(rows.size()) - 1;
    int position = currentIndex().isValid() ? std::max(0, PositionOf(currentIndex().row())) : 0;
    switch (cursor_action) {
    case MoveLeft:
    case MoveUp:
        position = std::max(0, position - 1);
        break;
    case MoveRight:
    case MoveDown:
        position = std::min(last, position + 1);
        break;
    case MoveHome:
        position = 0;
        break;
    case MoveEnd:
        position = last;
        break;
    case MovePageUp:
        position = std::max(0, position - 4);
        break;
    case MovePageDown:
        position = std::min(last, position + 4);
        break;
    default:
        break;
    }
    return model()->index(rows[static_cast<size_t>(position)], 0);
}

void GameCarousel::setSelection(const QRect& rect, QItemSelectionModel::SelectionFlags command) {
    if (!Stacked()) {
        QListView::setSelection(rect, command);
        return;
    }
    if (const QModelIndex index = StackIndexAt(rect.center()); index.isValid()) {
        selectionModel()->select(index, command);
    }
}

QRegion GameCarousel::visualRegionForSelection(const QItemSelection& selection) const {
    if (!Stacked()) {
        return QListView::visualRegionForSelection(selection);
    }
    QRegion region;
    for (const QItemSelectionRange& range : selection) {
        for (int row = range.top(); row <= range.bottom(); ++row) {
            region += visualRect(model()->index(row, 0));
        }
    }
    return region;
}

void GameCarousel::mousePressEvent(QMouseEvent* event) {
    // The Case look: a click on the picked game's case opens it (PyroCade's open case); the
    // Play button, Enter and a double click on another game still start a game directly.
    if (event->button() == Qt::LeftButton &&
        GameCard::CurrentStyle() == GameCard::Style::Case) {
        const QModelIndex hit = indexAt(event->position().toPoint());
        if (hit.isValid() && currentIndex().isValid() && hit.row() == currentIndex().row()) {
            // The case comes forward to be turned round and opened (CaseViewer).
            emit CaseOpenRequested(currentIndex());
            event->accept();
            return;
        }
    }
    QListView::mousePressEvent(event);
}

void GameCarousel::wheelEvent(QWheelEvent* event) {
    if (!Stacked()) {
        QListView::wheelEvent(event);
        return;
    }
    // One step of the wheel, one game.
    const int delta = event->angleDelta().x() + event->angleDelta().y();
    const std::vector<int> rows = VisibleRows();
    if (delta == 0 || rows.empty()) {
        event->ignore();
        return;
    }
    int position = currentIndex().isValid() ? std::max(0, PositionOf(currentIndex().row())) : 0;
    position = std::clamp(position + (delta > 0 ? -1 : 1), 0, static_cast<int>(rows.size()) - 1);
    setCurrentIndex(model()->index(rows[static_cast<size_t>(position)], 0));
    event->accept();
}
