// SPDX-FileCopyrightText: 2026 PhoenixEmu contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <fstream>
#include <QCheckBox>
#include <QDateTime>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

#include <fmt/format.h>

#include "common/fs/fs_paths.h"
#include "common/fs/path_util.h"
#include "common/settings.h"
#include "core/core.h"
#include "main_window.h"
#include "phoenix/bootmanager.h"
#include "phoenix/render/quick_menu_overlay.h"
#include "phoenix/util/phoenix_theme.h"
#include "qt_common/config/uisettings.h"
#include "qt_common/qt_common.h"

namespace {

std::filesystem::path GetGameActiveSaveDir() {
    const u64 pid = QtCommon::system ? QtCommon::system->GetApplicationProcessProgramID() : 0;
    if (pid == 0) return {};
    const std::string pid_str = fmt::format("{:016X}", pid);

    const std::vector<std::filesystem::path> search_roots = {
        Common::FS::GetEdenPath(Common::FS::EdenPath::NANDDir) / "user" / "save" / "0000000000000000",
        Common::FS::GetEdenPath(Common::FS::EdenPath::SaveDir) / "user" / "save" / "0000000000000000",
        Common::FS::GetEdenPath(Common::FS::EdenPath::NANDDir) / "user" / "save",
    };

    std::error_code ec;
    for (const auto& root : search_roots) {
        if (!std::filesystem::exists(root, ec)) continue;
        for (const auto& user_entry : std::filesystem::directory_iterator(root, ec)) {
            if (!user_entry.is_directory(ec)) continue;
            for (const auto& title_entry : std::filesystem::directory_iterator(user_entry.path(), ec)) {
                if (title_entry.is_directory(ec)) {
                    std::string fname = title_entry.path().filename().string();
                    std::transform(fname.begin(), fname.end(), fname.begin(), ::toupper);
                    if (fname == pid_str) {
                        return title_entry.path();
                    }
                }
            }
        }
    }
    return {};
}

} // namespace

QuickMenuOverlay::QuickMenuOverlay(MainWindow* parent)
    : QWidget(parent), m_main_window(parent) {
    setAttribute(Qt::WA_TranslucentBackground);
    setWindowFlags(Qt::FramelessWindowHint | Qt::Tool);
    setFocusPolicy(Qt::StrongFocus);
    setFixedWidth(460);

    auto* const root_layout = new QVBoxLayout(this);
    root_layout->setContentsMargins(22, 22, 22, 22);
    root_layout->setSpacing(13);

    // Title & Header
    auto* const header_layout = new QHBoxLayout();
    auto* const title_label = new QLabel(tr("⚡ PHOENIX QUICK SETTINGS"), this);
    QFont title_font = title_label->font();
    title_font.setBold(true);
    title_font.setPixelSize(14);
    title_label->setFont(title_font);
    title_label->setStyleSheet(QStringLiteral("color: #FF5230; letter-spacing: 0.5px;"));

    auto* const close_button = new QPushButton(QStringLiteral("✕"), this);
    close_button->setFixedSize(28, 28);
    close_button->setCursor(Qt::PointingHandCursor);
    close_button->setStyleSheet(
        QStringLiteral("QPushButton { background: rgba(255,255,255,20); border: 1px solid rgba(255,255,255,40); "
                       "border-radius: 14px; color: #FFF; font-weight: bold; font-size: 13px; } "
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

    // Frame Generation & ANR
    auto* const fg_group = new QVBoxLayout();
    fg_group->setSpacing(4);

    m_frame_gen_check = new QCheckBox(tr("Frame Generation (FSR 3.1 / Optical Flow)"), this);
    m_frame_gen_check->setChecked(Settings::values.frame_gen.GetValue());
    m_frame_gen_check->setStyleSheet(QStringLiteral("color: #F0E2C0; font-weight: bold;"));
    m_frame_gen_check->setCursor(Qt::PointingHandCursor);
    connect(m_frame_gen_check, &QCheckBox::toggled, this, [](bool enabled) {
        Settings::values.frame_gen.SetValue(enabled);
    });

    m_anr_check = new QCheckBox(tr("⚡ Apex Neural Rendering (ANR - All GPUs)"), this);
    const bool is_anr = Settings::values.frame_gen_type.GetValue() == Settings::FrameGenType::Anr;
    m_anr_check->setChecked(is_anr);
    m_anr_check->setStyleSheet(QStringLiteral("color: #64FF78; font-weight: bold;"));
    m_anr_check->setCursor(Qt::PointingHandCursor);
    connect(m_anr_check, &QCheckBox::toggled, this, [this](bool enabled) {
        if (enabled) {
            Settings::values.frame_gen_type.SetValue(Settings::FrameGenType::Anr);
            Settings::values.scaling_filter.SetValue(Settings::ScalingFilter::Anr);
            Settings::values.frame_gen.SetValue(true);
            m_frame_gen_check->setChecked(true);
        } else {
            Settings::values.frame_gen_type.SetValue(Settings::FrameGenType::Fsr3);
        }
    });

    fg_group->addWidget(m_frame_gen_check);
    fg_group->addWidget(m_anr_check);
    root_layout->addLayout(fg_group);

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

    // Save State Slots Section
    auto* const save_group_label = new QLabel(tr("💾 Save State Slots"), this);
    save_group_label->setStyleSheet(QStringLiteral("color: #FFC83D; font-weight: bold; font-size: 12px; margin-top: 4px;"));
    root_layout->addWidget(save_group_label);

    auto* const slot_btn_row = new QHBoxLayout();
    slot_btn_row->setSpacing(6);
    for (int i = 0; i < 5; ++i) {
        const int slot_num = i + 1;
        auto* const btn = new QPushButton(QStringLiteral("Slot %1").arg(slot_num), this);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFixedHeight(28);
        connect(btn, &QPushButton::clicked, this, [this, slot_num] {
            UpdateSlotInfo(slot_num);
        });
        m_slot_buttons[i] = btn;
        slot_btn_row->addWidget(btn);
    }
    root_layout->addLayout(slot_btn_row);

    // Card frame containing thumbnail, metadata, and controls
    auto* const slot_card = new QFrame(this);
    slot_card->setStyleSheet(QStringLiteral(
        "QFrame { background: rgba(0, 0, 0, 55); border: 1px solid rgba(255, 255, 255, 30); border-radius: 10px; }"));
    auto* const slot_card_layout = new QVBoxLayout(slot_card);
    slot_card_layout->setContentsMargins(12, 12, 12, 12);
    slot_card_layout->setSpacing(10);

    auto* const slot_info_row = new QHBoxLayout();
    slot_info_row->setSpacing(12);

    m_slot_thumbnail = new QLabel(slot_card);
    m_slot_thumbnail->setFixedSize(128, 72);
    m_slot_thumbnail->setStyleSheet(QStringLiteral("background: #110E0C; border: 1px solid rgba(255,255,255,40); border-radius: 6px;"));
    m_slot_thumbnail->setAlignment(Qt::AlignCenter);

    m_slot_meta_label = new QLabel(slot_card);
    m_slot_meta_label->setStyleSheet(QStringLiteral("color: #E8D9B5; font-size: 11px; line-height: 140%;"));
    m_slot_meta_label->setWordWrap(true);
    m_slot_meta_label->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);

    slot_info_row->addWidget(m_slot_thumbnail);
    slot_info_row->addWidget(m_slot_meta_label, 1);
    slot_card_layout->addLayout(slot_info_row);

    auto* const slot_action_row = new QHBoxLayout();
    slot_action_row->setSpacing(8);
    m_slot_save_btn = new QPushButton(tr("💾 Save State"), slot_card);
    m_slot_load_btn = new QPushButton(tr("📂 Load State"), slot_card);
    m_slot_save_btn->setFixedHeight(30);
    m_slot_load_btn->setFixedHeight(30);
    m_slot_save_btn->setCursor(Qt::PointingHandCursor);
    m_slot_load_btn->setCursor(Qt::PointingHandCursor);
    m_slot_save_btn->setStyleSheet(QStringLiteral(
        "QPushButton { background: rgba(255,82,48,200); border: 1px solid #FF5230; border-radius: 6px; "
        "color: #FFF; font-weight: bold; font-size: 12px; padding: 4px 12px; } "
        "QPushButton:hover { background: #FF5230; }"));
    m_slot_load_btn->setStyleSheet(QStringLiteral(
        "QPushButton { background: rgba(255,255,255,30); border: 1px solid rgba(255,255,255,60); border-radius: 6px; "
        "color: #FFF; font-weight: bold; font-size: 12px; padding: 4px 12px; } "
        "QPushButton:hover { background: rgba(255,255,255,50); } "
        "QPushButton:disabled { background: rgba(255,255,255,10); border: 1px solid rgba(255,255,255,15); color: #555; }"));

    connect(m_slot_save_btn, &QPushButton::clicked, this, &QuickMenuOverlay::SaveSelectedSlot);
    connect(m_slot_load_btn, &QPushButton::clicked, this, &QuickMenuOverlay::LoadSelectedSlot);

    slot_action_row->addWidget(m_slot_save_btn);
    slot_action_row->addWidget(m_slot_load_btn);
    slot_card_layout->addLayout(slot_action_row);

    m_slot_restart_btn = new QPushButton(tr("🔄 Restart Game (Apply Restored Save)"), slot_card);
    m_slot_restart_btn->setFixedHeight(28);
    m_slot_restart_btn->setCursor(Qt::PointingHandCursor);
    m_slot_restart_btn->setStyleSheet(QStringLiteral(
        "QPushButton { background: rgba(0,180,90,190); border: 1px solid #64FF78; border-radius: 6px; "
        "color: #FFF; font-weight: bold; font-size: 11px; padding: 4px 10px; } "
        "QPushButton:hover { background: #00C864; } "
        "QPushButton:disabled { background: rgba(255,255,255,10); border: 1px solid rgba(255,255,255,15); color: #555; }"));
    m_slot_restart_btn->setEnabled(false);
    connect(m_slot_restart_btn, &QPushButton::clicked, this, [this] {
        if (m_main_window) {
            m_main_window->OnRestartGame();
            hide();
        }
    });
    slot_card_layout->addWidget(m_slot_restart_btn);

    root_layout->addWidget(slot_card);

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
    if (m_anr_check) {
        m_anr_check->setChecked(Settings::values.frame_gen.GetValue() &&
                                Settings::values.frame_gen_type.GetValue() == Settings::FrameGenType::Anr);
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
    UpdateSlotInfo(m_selected_slot);
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

std::filesystem::path QuickMenuOverlay::GetSlotDir(int slot_index) const {
    const u64 pid = QtCommon::system ? QtCommon::system->GetApplicationProcessProgramID() : 0;
    return Common::FS::GetEdenPath(Common::FS::EdenPath::NANDDir) / "user" / "save_slots" /
           fmt::format("{:016X}", pid) / fmt::format("slot_{}", slot_index);
}

void QuickMenuOverlay::UpdateSlotInfo(int slot_index) {
    m_selected_slot = slot_index;
    std::error_code ec;

    for (size_t i = 0; i < m_slot_buttons.size(); ++i) {
        if (!m_slot_buttons[i]) continue;
        const int s_num = static_cast<int>(i + 1);
        const auto s_dir = GetSlotDir(s_num);
        const bool has_save = std::filesystem::exists(s_dir / "meta.txt", ec);
        const QString label = has_save ? QStringLiteral("Slot %1 ●").arg(s_num) : QStringLiteral("Slot %1").arg(s_num);
        m_slot_buttons[i]->setText(label);

        if (s_num == slot_index) {
            m_slot_buttons[i]->setStyleSheet(QStringLiteral(
                "QPushButton { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #FF5230, stop:1 #D9381E); "
                "border: 1px solid #FF7050; border-radius: 6px; color: #FFF; font-weight: bold; font-size: 11px; }"));
        } else {
            m_slot_buttons[i]->setStyleSheet(QStringLiteral(
                "QPushButton { background: rgba(255,255,255,18); border: 1px solid rgba(255,255,255,30); "
                "border-radius: 6px; color: #BBB; font-weight: bold; font-size: 11px; } "
                "QPushButton:hover { background: rgba(255,255,255,35); color: #FFF; }"));
        }
    }

    const auto slot_dir = GetSlotDir(slot_index);
    const auto thumb_path = slot_dir / "thumb.png";
    const auto meta_path = slot_dir / "meta.txt";

    m_slot_thumbnail->clear();
    if (std::filesystem::exists(thumb_path, ec)) {
        QPixmap pix(QString::fromStdString(thumb_path.string()));
        if (!pix.isNull()) {
            m_slot_thumbnail->setPixmap(pix.scaled(m_slot_thumbnail->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
            m_slot_thumbnail->setStyleSheet(QStringLiteral("background: #110E0C; border: 1px solid rgba(255,255,255,40); border-radius: 6px;"));
        } else {
            m_slot_thumbnail->setText(tr("No Image"));
            m_slot_thumbnail->setStyleSheet(QStringLiteral("background: #110E0C; border: 1px dashed rgba(255,255,255,30); color: #777; font-size: 10px; border-radius: 6px;"));
        }
    } else {
        m_slot_thumbnail->setText(tr("Empty Slot"));
        m_slot_thumbnail->setStyleSheet(QStringLiteral("background: #110E0C; border: 1px dashed rgba(255,255,255,30); color: #777; font-size: 10px; border-radius: 6px;"));
    }

    if (std::filesystem::exists(meta_path, ec)) {
        std::ifstream meta_file{meta_path};
        std::string date_str;
        std::getline(meta_file, date_str);
        m_slot_meta_label->setText(QStringLiteral(
            "<b>Slot %1</b><br><span style='color:#64FF78;'>● State Saved</span><br><span style='color:#AAA; font-size:10px;'>%2</span>")
            .arg(slot_index).arg(QString::fromStdString(date_str)));
        m_slot_load_btn->setEnabled(true);
    } else {
        m_slot_meta_label->setText(QStringLiteral(
            "<b>Slot %1</b><br><span style='color:#777;'><i>[Empty Slot]</i></span><br><span style='color:#666; font-size:10px;'>Click Save State to capture</span>")
            .arg(slot_index));
        m_slot_load_btn->setEnabled(false);
    }
    if (m_slot_restart_btn) {
        m_slot_restart_btn->setEnabled(false);
    }
}

void QuickMenuOverlay::SaveSelectedSlot() {
    const auto slot_dir = GetSlotDir(m_selected_slot);
    std::error_code ec;
    std::filesystem::create_directories(slot_dir, ec);

    const auto thumb_path = slot_dir / "thumb.png";
    if (m_main_window && m_main_window->render_window) {
        m_main_window->render_window->CaptureScreenshot(QString::fromStdString(thumb_path.string()));
    }

    const auto active_dir = GetGameActiveSaveDir();
    if (!active_dir.empty()) {
        const auto data_dir = slot_dir / "data";
        std::filesystem::remove_all(data_dir, ec);
        std::filesystem::create_directories(data_dir, ec);
        for (const auto& entry : std::filesystem::recursive_directory_iterator(active_dir, ec)) {
            if (ec) break;
            const auto rel_path = std::filesystem::relative(entry.path(), active_dir, ec);
            const auto dest_path = data_dir / rel_path;
            if (entry.is_directory(ec)) {
                std::filesystem::create_directories(dest_path, ec);
                ec.clear();
            } else if (entry.is_regular_file(ec)) {
                std::filesystem::create_directories(dest_path.parent_path(), ec);
                ec.clear();
                std::filesystem::copy_file(entry.path(), dest_path, std::filesystem::copy_options::overwrite_existing, ec);
            }
        }
    }

    const auto meta_path = slot_dir / "meta.txt";
    std::ofstream meta_file{meta_path, std::ios::trunc};
    meta_file << QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss")).toStdString();
    meta_file.close();

    UpdateSlotInfo(m_selected_slot);
}

void QuickMenuOverlay::LoadSelectedSlot() {
    const auto slot_dir = GetSlotDir(m_selected_slot);
    const auto data_dir = slot_dir / "data";
    std::error_code ec;
    if (!std::filesystem::exists(data_dir, ec)) {
        return;
    }

    const auto active_dir = GetGameActiveSaveDir();
    if (!active_dir.empty()) {
        std::filesystem::create_directories(active_dir, ec);
        for (const auto& entry : std::filesystem::recursive_directory_iterator(data_dir, ec)) {
            if (ec) break;
            const auto rel_path = std::filesystem::relative(entry.path(), data_dir, ec);
            const auto dest_path = active_dir / rel_path;
            if (entry.is_directory(ec)) {
                std::filesystem::create_directories(dest_path, ec);
                ec.clear();
            } else if (entry.is_regular_file(ec)) {
                std::filesystem::create_directories(dest_path.parent_path(), ec);
                ec.clear();
                std::filesystem::copy_file(entry.path(), dest_path, std::filesystem::copy_options::overwrite_existing, ec);
            }
        }
        m_slot_meta_label->setText(QStringLiteral(
            "<b>Slot %1</b><br><span style='color:#64FF78;'><b>✔ State Restored to Disk!</b></span><br>"
            "<span style='color:#FFF; font-size:10px;'>Click Restart below to load into memory</span>")
            .arg(m_selected_slot));
        if (m_slot_restart_btn) {
            m_slot_restart_btn->setEnabled(true);
        }
    }
}
