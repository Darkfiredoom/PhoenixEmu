// SPDX-FileCopyrightText: 2026 PhoenixEmu contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QCheckBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

#include "common/settings.h"
#include "core/core.h"
#include "main_window.h"
#include "phoenix/render/quick_menu_overlay.h"
#include "phoenix/util/phoenix_theme.h"
#include "qt_common/config/uisettings.h"
#include "qt_common/qt_common.h"

QuickMenuOverlay::QuickMenuOverlay(MainWindow* parent)
    : QWidget(parent), m_main_window(parent) {
    setAttribute(Qt::WA_TranslucentBackground);
    setWindowFlags(Qt::FramelessWindowHint | Qt::Tool);
    setFocusPolicy(Qt::StrongFocus);
    setFixedWidth(380);

    auto* const root_layout = new QVBoxLayout(this);
    root_layout->setContentsMargins(20, 20, 20, 20);
    root_layout->setSpacing(14);

    // Title & Header
    auto* const header_layout = new QHBoxLayout();
    auto* const title_label = new QLabel(tr("⚡ PHOENIX QUICK SETTINGS"), this);
    QFont title_font = title_label->font();
    title_font.setBold(true);
    title_font.setPixelSize(14);
    title_label->setFont(title_font);
    title_label->setStyleSheet(QStringLiteral("color: #FF5230; letter-spacing: 1px;"));

    auto* const close_button = new QPushButton(QStringLiteral("✕"), this);
    close_button->setFixedSize(26, 26);
    close_button->setCursor(Qt::PointingHandCursor);
    close_button->setStyleSheet(
        QStringLiteral("QPushButton { background: rgba(255,255,255,20); border: 1px solid rgba(255,255,255,40); "
                       "border-radius: 13px; color: #FFF; font-weight: bold; } "
                       "QPushButton:hover { background: rgba(255,82,48,160); }"));
    connect(close_button, &QPushButton::clicked, this, &QuickMenuOverlay::hide);

    header_layout->addWidget(title_label);
    header_layout->addStretch();
    header_layout->addWidget(close_button);
    root_layout->addLayout(header_layout);

    // Game Title Label
    m_game_title = new QLabel(this);
    QFont sub_font = m_game_title->font();
    sub_font.setPixelSize(12);
    m_game_title->setFont(sub_font);
    m_game_title->setStyleSheet(QStringLiteral("color: #E8D9B5;"));
    m_game_title->setWordWrap(true);
    root_layout->addWidget(m_game_title);

    // Separator line
    auto* const sep1 = new QFrame(this);
    sep1->setFrameShape(QFrame::HLine);
    sep1->setStyleSheet(QStringLiteral("background: rgba(255,255,255,25); max-height: 1px;"));
    root_layout->addWidget(sep1);

    // Volume Section
    auto* const volume_row = new QHBoxLayout();
    auto* const volume_label = new QLabel(tr("Audio Volume:"), this);
    volume_label->setStyleSheet(QStringLiteral("color: #F0E2C0; font-weight: bold;"));
    m_volume_slider = new QSlider(Qt::Horizontal, this);
    m_volume_slider->setRange(0, 100);
    m_volume_slider->setValue(Settings::values.volume.GetValue());
    m_volume_slider->setCursor(Qt::PointingHandCursor);
    connect(m_volume_slider, &QSlider::valueChanged, this, [this](int val) {
        Settings::values.audio_muted = false;
        Settings::values.volume.SetValue(static_cast<u8>(val));
        if (m_main_window) {
            m_main_window->UpdateVolumeUI();
        }
    });

    m_mute_button = new QPushButton(Settings::values.audio_muted.GetValue() ? tr("Unmute") : tr("Mute"), this);
    m_mute_button->setCursor(Qt::PointingHandCursor);
    m_mute_button->setStyleSheet(
        QStringLiteral("QPushButton { background: rgba(255,255,255,25); border: 1px solid rgba(255,255,255,50); "
                       "border-radius: 6px; padding: 4px 10px; color: #FFF; } "
                       "QPushButton:hover { background: rgba(255,82,48,140); }"));
    connect(m_mute_button, &QPushButton::clicked, this, [this] {
        const bool muted = !Settings::values.audio_muted.GetValue();
        Settings::values.audio_muted = muted;
        m_mute_button->setText(muted ? tr("Unmute") : tr("Mute"));
        if (m_main_window) {
            m_main_window->UpdateVolumeUI();
        }
    });

    volume_row->addWidget(volume_label);
    volume_row->addWidget(m_volume_slider);
    volume_row->addWidget(m_mute_button);
    root_layout->addLayout(volume_row);

    // Docked / Handheld Mode
    auto* const mode_row = new QHBoxLayout();
    auto* const mode_label = new QLabel(tr("Console Mode:"), this);
    mode_label->setStyleSheet(QStringLiteral("color: #F0E2C0; font-weight: bold;"));

    const bool is_docked = Settings::values.use_docked_mode.GetValue() == Settings::ConsoleMode::Docked;
    m_docked_button = new QPushButton(is_docked ? tr("Docked (1080p)") : tr("Handheld (720p)"), this);
    m_docked_button->setCursor(Qt::PointingHandCursor);
    m_docked_button->setStyleSheet(
        QStringLiteral("QPushButton { background: rgba(255,255,255,25); border: 1px solid rgba(255,255,255,50); "
                       "border-radius: 6px; padding: 5px 12px; color: #FFC83D; font-weight: bold; } "
                       "QPushButton:hover { background: rgba(255,82,48,140); color: #FFF; }"));
    connect(m_docked_button, &QPushButton::clicked, this, [this] {
        if (m_main_window) {
            m_main_window->OnToggleDockedMode();
            const bool docked = Settings::values.use_docked_mode.GetValue() == Settings::ConsoleMode::Docked;
            m_docked_button->setText(docked ? tr("Docked (1080p)") : tr("Handheld (720p)"));
        }
    });
    mode_row->addWidget(mode_label);
    mode_row->addStretch();
    mode_row->addWidget(m_docked_button);
    root_layout->addLayout(mode_row);

    // AMD Frame Generation
    m_frame_gen_check = new QCheckBox(tr("AMD FidelityFX™ Frame Generation (FSR 3.1)"), this);
    m_frame_gen_check->setChecked(Settings::values.frame_gen.GetValue());
    m_frame_gen_check->setStyleSheet(QStringLiteral("color: #F0E2C0; font-weight: bold;"));
    m_frame_gen_check->setCursor(Qt::PointingHandCursor);
    connect(m_frame_gen_check, &QCheckBox::toggled, this, [](bool enabled) {
        Settings::values.frame_gen.SetValue(enabled);
    });
    root_layout->addWidget(m_frame_gen_check);

    // FSR Sharpness
    auto* const sharp_row = new QHBoxLayout();
    auto* const sharp_label = new QLabel(tr("FSR Sharpness:"), this);
    sharp_label->setStyleSheet(QStringLiteral("color: #F0E2C0; font-weight: bold;"));

    m_sharpness_slider = new QSlider(Qt::Horizontal, this);
    m_sharpness_slider->setRange(0, 100);
    const int current_sharp = Settings::values.fsr_sharpening_slider.GetValue();
    m_sharpness_slider->setValue(current_sharp);
    m_sharpness_slider->setCursor(Qt::PointingHandCursor);

    m_sharpness_label = new QLabel(QStringLiteral("%1%").arg(current_sharp), this);
    m_sharpness_label->setStyleSheet(QStringLiteral("color: #FFC83D; font-weight: bold; min-width: 38px;"));

    connect(m_sharpness_slider, &QSlider::valueChanged, this, [this](int val) {
        Settings::values.fsr_sharpening_slider.SetValue(val);
        m_sharpness_label->setText(QStringLiteral("%1%").arg(val));
    });
    sharp_row->addWidget(sharp_label);
    sharp_row->addWidget(m_sharpness_slider);
    sharp_row->addWidget(m_sharpness_label);
    root_layout->addLayout(sharp_row);

    // Performance Overlay Toggle
    m_perf_overlay_check = new QCheckBox(tr("Show Performance Overlay (HUD)"), this);
    m_perf_overlay_check->setChecked(UISettings::values.show_perf_overlay.GetValue());
    m_perf_overlay_check->setStyleSheet(QStringLiteral("color: #F0E2C0; font-weight: bold;"));
    m_perf_overlay_check->setCursor(Qt::PointingHandCursor);
    connect(m_perf_overlay_check, &QCheckBox::toggled, this, [this](bool) {
        if (m_main_window) {
            m_main_window->OnTogglePerfOverlay();
        }
    });
    root_layout->addWidget(m_perf_overlay_check);

    // Separator line
    auto* const sep2 = new QFrame(this);
    sep2->setFrameShape(QFrame::HLine);
    sep2->setStyleSheet(QStringLiteral("background: rgba(255,255,255,25); max-height: 1px;"));
    root_layout->addWidget(sep2);

    // Bottom Action Row: Screenshot & Resume
    auto* const action_row = new QHBoxLayout();
    m_screenshot_button = new QPushButton(tr("📷 Screenshot"), this);
    m_screenshot_button->setCursor(Qt::PointingHandCursor);
    m_screenshot_button->setStyleSheet(
        QStringLiteral("QPushButton { background: rgba(255,255,255,25); border: 1px solid rgba(255,255,255,50); "
                       "border-radius: 8px; padding: 8px 16px; color: #FFF; font-weight: bold; } "
                       "QPushButton:hover { background: rgba(255,255,255,45); }"));
    connect(m_screenshot_button, &QPushButton::clicked, this, [this] {
        if (m_main_window) {
            m_main_window->OnCaptureScreenshot();
        }
    });

    m_resume_button = new QPushButton(tr("▶ Resume Game"), this);
    m_resume_button->setCursor(Qt::PointingHandCursor);
    m_resume_button->setStyleSheet(
        QStringLiteral("QPushButton { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #FF5230, stop:1 #D9381E); "
                       "border: 1px solid rgba(255,255,255,180); border-radius: 8px; padding: 8px 20px; "
                       "color: #FFF; font-weight: bold; } "
                       "QPushButton:hover { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #FF7050, stop:1 #FF5230); }"));
    connect(m_resume_button, &QPushButton::clicked, this, &QuickMenuOverlay::hide);

    action_row->addWidget(m_screenshot_button);
    action_row->addStretch();
    action_row->addWidget(m_resume_button);
    root_layout->addLayout(action_row);

    if (m_main_window) {
        m_main_window->installEventFilter(this);
    }
}

QuickMenuOverlay::~QuickMenuOverlay() = default;

void QuickMenuOverlay::RefreshValues() {
    if (m_volume_slider) {
        m_volume_slider->setValue(Settings::values.volume.GetValue());
    }
    if (m_mute_button) {
        m_mute_button->setText(Settings::values.audio_muted.GetValue() ? tr("Unmute") : tr("Mute"));
    }
    if (m_docked_button) {
        const bool docked = Settings::values.use_docked_mode.GetValue() == Settings::ConsoleMode::Docked;
        m_docked_button->setText(docked ? tr("Docked (1080p)") : tr("Handheld (720p)"));
    }
    if (m_frame_gen_check) {
        m_frame_gen_check->setChecked(Settings::values.frame_gen.GetValue());
    }
    if (m_sharpness_slider) {
        const int sharp = Settings::values.fsr_sharpening_slider.GetValue();
        m_sharpness_slider->setValue(sharp);
        if (m_sharpness_label) {
            m_sharpness_label->setText(QStringLiteral("%1%").arg(sharp));
        }
    }
    if (m_perf_overlay_check) {
        m_perf_overlay_check->setChecked(UISettings::values.show_perf_overlay.GetValue());
    }
    if (m_game_title && QtCommon::system) {
        const std::string path = QtCommon::system->GetCurrentApplicationFilePath();
        const u64 pid = QtCommon::system->GetApplicationProcessProgramID();
        if (pid != 0) {
            m_game_title->setText(QStringLiteral("Current Title: 0x%1").arg(pid, 16, 16, QLatin1Char('0')).toUpper());
        } else if (!path.empty()) {
            m_game_title->setText(QString::fromStdString(path));
        } else {
            m_game_title->setText(tr("Emulation Active"));
        }
    }
}

void QuickMenuOverlay::Toggle() {
    if (isVisible()) {
        hide();
    } else {
        RefreshValues();
        Reposition();
        show();
        raise();
        activateWindow();
    }
}

void QuickMenuOverlay::Reposition() {
    if (!m_main_window) {
        return;
    }
    const QRect parent_rect = m_main_window->geometry();
    const int x = parent_rect.center().x() - width() / 2;
    const int y = parent_rect.center().y() - height() / 2;
    move(x, y);
}

void QuickMenuOverlay::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const QRect r = rect().adjusted(1, 1, -1, -1);
    const qreal radius = 16.0;

    // Translucent dark glass background
    QPainterPath path;
    path.addRoundedRect(r, radius, radius);
    painter.setClipPath(path);

    painter.fillRect(r, PhoenixTheme::Recolor(QColor(0x18, 0x13, 0x10, 238)));

    // Top specular sheen
    QLinearGradient sheen(r.topLeft(), QPointF(r.left(), r.top() + 70));
    sheen.setColorAt(0.0, QColor(255, 255, 255, 45));
    sheen.setColorAt(1.0, QColor(255, 255, 255, 0));
    painter.fillRect(QRect(r.left(), r.top(), r.width(), 70), sheen);

    // Glowing accent border
    painter.setClipping(false);
    painter.setPen(QPen(PhoenixTheme::Recolor(PhoenixTheme::DefaultAccent), 1.5));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(r, radius, radius);
}

void QuickMenuOverlay::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape || event->key() == Qt::Key_F10) {
        hide();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

bool QuickMenuOverlay::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_main_window) {
        if (event->type() == QEvent::Move || event->type() == QEvent::Resize) {
            if (isVisible()) {
                Reposition();
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}
