// SPDX-FileCopyrightText: 2026 PhoenixEmu contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cmath>
#include <fstream>
#include <functional>
#include <QButtonGroup>
#include <QComboBox>
#include <QIcon>
#include <QDateTime>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

#include <fmt/format.h>

#include "common/fs/fs_paths.h"
#include "common/memory_detect.h"
#include "common/fs/path_util.h"
#include "common/settings.h"
#include "core/core.h"
#include "core/loader/loader.h"
#include "core/perf_stats.h"
#include "hid_core/frontend/emulated_controller.h"
#include "hid_core/hid_core.h"
#include "frontend_common/play_time_manager.h"
#include "main_window.h"
#include "phoenix/bootmanager.h"
#include "phoenix/controllers_dialog.h"
#include "phoenix/game/game_list.h"
#include "qt_common/game_list/game_list_p.h"
#include "phoenix/render/quick_menu_overlay.h"
#include "phoenix/util/phoenix_theme.h"
#include "qt_common/config/uisettings.h"
#include "qt_common/qt_common.h"
#include "video_core/gpu.h"
#include "video_core/renderer_base.h"

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

namespace {

enum class Action { Pause, Restart, Stop, Capture, Album, Fullscreen, Settings };

/// A small line glyph for an action tile.
QIcon ActionIcon(Action action) {
    QPixmap pixmap(QSize(44, 44));
    pixmap.fill(Qt::transparent);
    pixmap.setDevicePixelRatio(2.0);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(0xFF, 0xF4, 0xDA), 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    switch (action) {
    case Action::Pause:
        p.drawRoundedRect(QRectF(6, 5, 3.4, 12), 1, 1);
        p.drawRoundedRect(QRectF(12.6, 5, 3.4, 12), 1, 1);
        break;
    case Action::Restart:
        p.drawArc(QRectF(4.5, 4.5, 13, 13), 60 * 16, 290 * 16);
        p.drawLine(QPointF(14.5, 3.5), QPointF(15.5, 7.5));
        p.drawLine(QPointF(15.5, 7.5), QPointF(11.5, 8));
        break;
    case Action::Stop:
        p.drawLine(QPointF(6, 6), QPointF(16, 16));
        p.drawLine(QPointF(16, 6), QPointF(6, 16));
        break;
    case Action::Capture:
        p.drawRoundedRect(QRectF(3.5, 6.5, 15, 11), 2, 2);
        p.drawEllipse(QRectF(8, 8.8, 6, 6));
        p.drawLine(QPointF(8, 6.5), QPointF(9.5, 4.5));
        p.drawLine(QPointF(9.5, 4.5), QPointF(12.5, 4.5));
        p.drawLine(QPointF(12.5, 4.5), QPointF(14, 6.5));
        break;
    case Action::Album:
        // Two pictures, one behind the other.
        p.drawRoundedRect(QRectF(6.5, 3.5, 12, 9.5), 1.6, 1.6);
        p.drawRoundedRect(QRectF(3.5, 7.5, 12, 10), 1.6, 1.6);
        p.drawLine(QPointF(5.5, 15.5), QPointF(9, 12));
        p.drawLine(QPointF(9, 12), QPointF(13.5, 15.5));
        break;
    case Action::Fullscreen:
        p.drawRoundedRect(QRectF(3.5, 4.5, 15, 10.5), 1.8, 1.8);
        p.drawLine(QPointF(8, 18), QPointF(14, 18));
        break;
    case Action::Settings:
        for (const qreal y : {6.0, 11.0, 16.0}) {
            p.drawLine(QPointF(4, y), QPointF(18, y));
        }
        p.setBrush(QColor(0xFF, 0xF4, 0xDA));
        p.drawEllipse(QPointF(14, 6), 1.8, 1.8);
        p.drawEllipse(QPointF(8, 11), 1.8, 1.8);
        p.drawEllipse(QPointF(12, 16), 1.8, 1.8);
        break;
    }
    p.end();
    return QIcon(pixmap);
}

QLabel* RowLabel(const QString& text, QWidget* parent) {
    auto* const label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("QuickRowLabel"));
    return label;
}

// The kiln palette (dist/qt_themes/phoenix/style.qss).
const QColor HudText{0xF6, 0xEB, 0xDD};
const QColor HudMuted{0xA8, 0x99, 0x8A};
const QColor HudEmber{0xFF, 0x5A, 0x1F};
const QColor HudGold{0xFF, 0xC2, 0x3D};
const QColor HudGood{0x7F, 0xD4, 0x8A};

/// A floating panel over the game that paints itself (the performance card, the status strip).
class HudPanel : public QWidget {
public:
    HudPanel(QWidget* parent, std::function<void(QPainter&, const QRect&)> paint_)
        : QWidget(parent), paint{std::move(paint_)} {
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_ShowWithoutActivating);
        setWindowFlags(Qt::FramelessWindowHint | Qt::Tool | Qt::WindowDoesNotAcceptFocus);
        hide();
    }
    std::function<void(const QPoint&)> on_click;

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::TextAntialiasing);
        paint(p, rect());
    }
    void mousePressEvent(QMouseEvent* event) override {
        if (on_click) {
            on_click(event->position().toPoint());
        }
    }

private:
    std::function<void(QPainter&, const QRect&)> paint;
};

/// The glass behind a panel: near-black kiln brown, a faint ember along the top.
void PaintGlass(QPainter& p, const QRectF& r, qreal radius) {
    QPainterPath path;
    path.addRoundedRect(r, radius, radius);
    p.fillPath(path, PhoenixTheme::Recolor(QColor(0x14, 0x10, 0x0D, 236)));
    QLinearGradient top{r.topLeft(), QPointF(r.left(), r.top() + 40)};
    top.setColorAt(0.0, QColor(0xFF, 0x5A, 0x1F, 26));
    top.setColorAt(1.0, QColor(0xFF, 0x5A, 0x1F, 0));
    p.fillPath(path, top);
    p.setPen(QPen(QColor(0x3A, 0x2E, 0x25), 1.2));
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);
}

/// The enabled mods of a game: its folders under load/<title id>, less the ones turned off.
int EnabledModCount(u64 program_id) {
    if (program_id == 0) {
        return 0;
    }
    const auto dir = Common::FS::GetEdenPath(Common::FS::EdenPath::LoadDir) /
                     fmt::format("{:016X}", program_id);
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) {
        return 0;
    }
    const auto disabled_it = Settings::values.disabled_addons.find(program_id);
    int count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (!entry.is_directory(ec)) {
            continue;
        }
        const std::string name = Common::FS::PathToUTF8String(entry.path().filename());
        if (disabled_it != Settings::values.disabled_addons.end() &&
            std::find(disabled_it->second.begin(), disabled_it->second.end(), name) !=
                disabled_it->second.end()) {
            continue;
        }
        ++count;
    }
    return count;
}

} // Anonymous namespace

QuickMenuOverlay::QuickMenuOverlay(MainWindow* parent)
    : QWidget(parent), m_main_window(parent) {
    setAttribute(Qt::WA_TranslucentBackground);
    setWindowFlags(Qt::FramelessWindowHint | Qt::Tool);
    setFocusPolicy(Qt::StrongFocus);
    setFixedWidth(470);
    setObjectName(QStringLiteral("QuickMenu"));
    setStyleSheet(QStringLiteral(R"(
        QWidget#QuickMenu QLabel { color: #F3E9D2; background: transparent; }
        QLabel#QuickGameTitle { font-size: 15px; font-weight: 800; color: #FFF4DA; }
        QLabel#QuickGamePlayed { font-size: 11px; color: rgba(233,220,194,160); }
        QLabel#QuickSection { font-size: 11px; font-weight: 700; color: rgba(233,220,194,150); }
        QLabel#QuickRowLabel { font-size: 13px; font-weight: 700; color: #FFF4DA; }
        QLabel#QuickHint { font-size: 10px; color: rgba(233,220,194,130); }
        QLabel#QuickActionName { font-size: 11px; font-weight: 700; color: #FFF4DA; }
        QLabel#QuickNote { font-size: 11px; color: #FFC83D; }
        QPushButton#QuickAction { background: rgba(255,230,180,16); border: 1px solid rgba(255,230,180,28);
            border-radius: 14px; min-width: 46px; max-width: 46px; min-height: 46px; max-height: 46px; padding: 0px; margin: 0px; }
        QPushButton#QuickAction:hover { background: rgba(255,138,61,70); border: 1px solid #FF8A3D; }
        QPushButton#QuickChip { background: rgba(255,230,180,14); border: 1px solid rgba(255,230,180,30);
            border-radius: 14px; padding: 5px 12px; min-width: 0px; min-height: 0px; margin: 0px; color: #E9DCC2; font-weight: 700; }
        QPushButton#QuickChip:checked { background: rgba(255,90,31,46); border: 1px solid #FF5A1F; color: #FFE3D2; }
        QPushButton#QuickChip:hover { border: 1px solid #FF8A3D; }
        QPushButton#QuickSmall { background: rgba(255,230,180,16); border: 1px solid rgba(255,230,180,30);
            border-radius: 12px; padding: 5px 12px; min-width: 0px; min-height: 0px; margin: 0px; color: #FFF4DA; font-weight: 700; }
        QPushButton#QuickSmall:hover { background: rgba(255,90,31,60); border: 1px solid #FF5A1F; }
        QPushButton#QuickStep { background: rgba(255,230,180,16); border: 1px solid rgba(255,230,180,30);
            border-radius: 12px; min-width: 24px; max-width: 24px; min-height: 24px; max-height: 24px; padding: 0px; margin: 0px;
            color: #FFF4DA; font-weight: 800; font-size: 14px; }
        QPushButton#QuickStep:hover { background: rgba(255,90,31,60); border: 1px solid #FF5A1F; }
        QLabel#QuickKey { font-size: 11px; font-weight: 700; color: rgba(233,220,194,150); background: rgba(255,230,180,14);
            border: 1px solid rgba(255,230,180,26); border-radius: 6px; padding: 1px 6px; }
        QPushButton#QuickSegment { background: transparent; border: none; border-radius: 12px; padding: 4px 9px; min-width: 0px; min-height: 0px; margin: 0px;
            color: #E9DCC2; font-weight: 700; }
        QPushButton#QuickSegment:checked { background: qlineargradient(x1:0,y1:0,x2:1,y2:0, stop:0 #FF5230, stop:1 #FF8A3D);
            color: #FFF6E8; }
        QFrame#QuickSegments { background: rgba(255,230,180,12); border: 1px solid rgba(255,230,180,26); border-radius: 15px; }
        QComboBox { background: rgba(255,230,180,16); border: 1px solid rgba(255,230,180,30); border-radius: 12px;
            padding: 4px 12px; color: #FFF4DA; font-weight: 700; min-width: 150px; }
        QPushButton#QuickLink { background: rgba(255,230,180,14); border: 1px solid rgba(255,230,180,28);
            border-radius: 14px; padding: 10px 14px; min-width: 0px; margin: 0px; color: #FFF4DA; font-weight: 800; text-align: left; }
        QPushButton#QuickLink:hover { background: rgba(255,138,61,60); border: 1px solid #FF8A3D; }
        QPushButton#QuickClose { background: rgba(255,255,255,18); border: 1px solid rgba(255,255,255,36);
            border-radius: 13px; color: #FFF; font-weight: bold; min-width: 26px; max-width: 26px; min-height: 26px; max-height: 26px; padding: 0px; margin: 0px; }
        QPushButton#QuickClose:hover { background: rgba(255,82,48,170); }
        QPushButton#QuickSlot { background: rgba(255,230,180,14); border: 1px solid rgba(255,230,180,26);
            border-radius: 10px; color: #E9DCC2; font-weight: 700; font-size: 11px; min-height: 26px; max-height: 26px; min-width: 0px; padding: 0px; margin: 0px; }
        QPushButton#QuickSlot:checked { background: rgba(255,138,61,90); border: 1px solid #FF8A3D; color: #FFF; }
        QPushButton#QuickSlotAction { background: rgba(255,230,180,18); border: 1px solid rgba(255,230,180,34);
            border-radius: 10px; color: #FFF4DA; font-weight: 700; padding: 4px 10px; min-width: 0px; min-height: 0px; margin: 0px; }
        QPushButton#QuickSlotAction:hover { background: rgba(255,138,61,70); }
        QPushButton#QuickSlotAction:disabled { color: rgba(233,220,194,70); }
    )"));

    auto* const root = new QVBoxLayout(this);
    root->setContentsMargins(20, 18, 20, 18);
    root->setSpacing(12);

    // The game: its picture, name and how long it has been played.
    auto* const header = new QHBoxLayout;
    header->setSpacing(12);
    m_game_icon = new QLabel(this);
    m_game_icon->setFixedSize(48, 48);
    m_game_icon->setScaledContents(true);
    auto* const header_text = new QVBoxLayout;
    header_text->setSpacing(1);
    m_game_title = new QLabel(this);
    m_game_title->setObjectName(QStringLiteral("QuickGameTitle"));
    m_game_played = new QLabel(this);
    m_game_played->setObjectName(QStringLiteral("QuickGamePlayed"));
    header_text->addWidget(m_game_title);
    header_text->addWidget(m_game_played);
    auto* const close_button = new QPushButton(QStringLiteral("✕"), this);
    close_button->setObjectName(QStringLiteral("QuickClose"));
    close_button->setCursor(Qt::PointingHandCursor);
    connect(close_button, &QPushButton::clicked, this, &QuickMenuOverlay::hide);
    header->addWidget(m_game_icon);
    header->addLayout(header_text, 1);
    header->addWidget(close_button, 0, Qt::AlignTop);
    root->addLayout(header);

    // The actions, each a tile with its name and key.
    auto* const actions = new QHBoxLayout;
    actions->setSpacing(4);
    const auto add_action = [&](Action action, const QString& name, const QString& key,
                                auto&& on_click) {
        auto* const column = new QVBoxLayout;
        column->setSpacing(3);
        auto* const button = new QPushButton(this);
        button->setObjectName(QStringLiteral("QuickAction"));
        button->setIcon(ActionIcon(action));
        button->setIconSize(QSize(22, 22));
        button->setCursor(Qt::PointingHandCursor);
        connect(button, &QPushButton::clicked, this, on_click);
        auto* const label = new QLabel(name, this);
        label->setObjectName(QStringLiteral("QuickActionName"));
        label->setAlignment(Qt::AlignHCenter);
        auto* const hint = new QLabel(key, this);
        hint->setObjectName(QStringLiteral("QuickHint"));
        hint->setAlignment(Qt::AlignHCenter);
        column->addWidget(button, 0, Qt::AlignHCenter);
        column->addWidget(label);
        column->addWidget(hint);
        actions->addLayout(column, 1);
        return button;
    };
    m_pause_button = add_action(Action::Pause, tr("Pause"), KeyFor("Continue/Pause Emulation"), [this] {
        m_main_window->OnPauseContinueGame();
        RefreshValues();
    });
    add_action(Action::Restart, tr("Restart"), KeyFor("Restart Emulation"), [this] {
        hide();
        m_main_window->OnRestartGame();
    });
    add_action(Action::Stop, tr("Stop"), KeyFor("Stop Emulation"), [this] {
        hide();
        m_main_window->OnStopGame();
    });
    add_action(Action::Capture, tr("Capture"), KeyFor("Capture Screenshot"), [this] {
        m_main_window->OnCaptureScreenshot();
        ShowNote(tr("Screenshot saved to the album."));
    });
    add_action(Action::Album, tr("Album"), QString{}, [this] {
        // The game pauses while its pictures are looked at, as on the console.
        hide();
        m_main_window->OnAlbumDialog();
    });
    add_action(Action::Fullscreen, tr("Full screen"), KeyFor("Fullscreen"), [this] {
        m_main_window->ToggleFullscreen();
        Reposition();
    });
    root->addLayout(actions);

    // What is on now: chips that turn a feature on or off at once.
    auto* const on_now = new QLabel(tr("On now"), this);
    on_now->setObjectName(QStringLiteral("QuickSection"));
    root->addWidget(on_now);
    auto* const chips = new QHBoxLayout;
    chips->setSpacing(6);
    const auto add_chip = [&](const QString& text, auto&& on_toggle) {
        auto* const chip = new QPushButton(text, this);
        chip->setObjectName(QStringLiteral("QuickChip"));
        chip->setCheckable(true);
        chip->setCursor(Qt::PointingHandCursor);
        connect(chip, &QPushButton::toggled, this, [this, on_toggle](bool on) {
            if (!m_refreshing) {
                on_toggle(on);
            }
        });
        chips->addWidget(chip);
        return chip;
    };
    m_frame_gen_chip = add_chip(tr("✦ Frame gen"), [this](bool on) {
        Settings::values.frame_gen.SetValue(on);
        RefreshValues();
    });
    m_turbo_chip = add_chip(tr("⚡ Turbo"), [](bool on) {
        // Turbo: the game runs as fast as it can, past its own frame limit.
        Settings::values.use_speed_limit.SetValue(!on);
    });
    m_hud_chip = add_chip(tr("▦ HUD"), [this](bool) { m_main_window->OnTogglePerfOverlay(); });
    // Mods: how many are on; a click opens the game's mods to turn them on and off.
    m_mods_chip = add_chip(tr("Mods"), [](bool) {});
    m_mods_chip->setCheckable(false);
    connect(m_mods_chip, &QPushButton::clicked, this, [this] {
        const u64 pid = QtCommon::system ? QtCommon::system->GetApplicationProcessProgramID() : 0;
        hide();
        m_main_window->OnModManagerDialog(pid);
    });
    chips->addStretch(1);
    root->addLayout(chips);

    // A segmented row of buttons (preset, console mode).
    const auto segments = [&](const QStringList& names, QButtonGroup*& group) {
        auto* const frame = new QFrame(this);
        frame->setObjectName(QStringLiteral("QuickSegments"));
        auto* const row = new QHBoxLayout(frame);
        row->setContentsMargins(3, 3, 3, 3);
        row->setSpacing(2);
        group = new QButtonGroup(frame);
        group->setExclusive(true);
        for (int i = 0; i < names.size(); ++i) {
            auto* const button = new QPushButton(names[i], frame);
            button->setObjectName(QStringLiteral("QuickSegment"));
            button->setCheckable(true);
            button->setCursor(Qt::PointingHandCursor);
            group->addButton(button, i);
            row->addWidget(button);
        }
        return frame;
    };
    const auto add_row = [&](const QString& name, const QString& hint, QWidget* control) {
        auto* const row = new QHBoxLayout;
        auto* const text = new QVBoxLayout;
        text->setSpacing(0);
        text->addWidget(RowLabel(name, this));
        QLabel* sub = nullptr;
        if (!hint.isEmpty()) {
            sub = new QLabel(hint, this);
            sub->setObjectName(QStringLiteral("QuickHint"));
            text->addWidget(sub);
        }
        row->addLayout(text, 1);
        row->addWidget(control);
        root->addLayout(row);
        return sub;
    };
    const auto section = [&](const QString& text) {
        auto* const label = new QLabel(text, this);
        label->setObjectName(QStringLiteral("QuickSection"));
        root->addWidget(label);
    };

    section(tr("Picture"));
    // Its hint says whether these are the game's own settings or the global ones.
    m_preset_hint = add_row(tr("Preset"), tr("Global · from the next start"),
                            segments({tr("Low"), tr("Native"), tr("High"), tr("Ultra")}, m_preset_group));
    connect(m_preset_group, &QButtonGroup::idClicked, this, &QuickMenuOverlay::ApplyPreset);

    m_resolution_box = new QComboBox(this);
    const std::pair<QString, Settings::ResolutionSetup> scales[] = {
        {tr("0.5x · 360p"), Settings::ResolutionSetup::Res1_2X},
        {tr("0.75x · 540p"), Settings::ResolutionSetup::Res3_4X},
        {tr("1x · Native"), Settings::ResolutionSetup::Res1X},
        {tr("1.5x · 1080p"), Settings::ResolutionSetup::Res3_2X},
        {tr("2x · 1440p"), Settings::ResolutionSetup::Res2X},
        {tr("3x · 4K"), Settings::ResolutionSetup::Res3X},
        {tr("4x"), Settings::ResolutionSetup::Res4X},
    };
    for (const auto& [name, value] : scales) {
        m_resolution_box->addItem(name, static_cast<int>(value));
    }
    connect(m_resolution_box, &QComboBox::activated, this, [this](int index) {
        Settings::values.resolution_setup.SetValue(
            static_cast<Settings::ResolutionSetup>(m_resolution_box->itemData(index).toInt()));
        ShowNote(tr("The resolution changes the next time the game starts."));
        RefreshValues();
    });
    add_row(tr("Resolution"), QString{}, m_resolution_box);

    m_frame_gen_box = new QComboBox(this);
    m_frame_gen_box->addItem(tr("Off"), -1);
    m_frame_gen_box->addItem(tr("FSR 3.1 ×2"), static_cast<int>(Settings::FrameGenType::Fsr3));
    m_frame_gen_box->addItem(tr("AMD FSR ×2"), static_cast<int>(Settings::FrameGenType::AmdFsr));
    m_frame_gen_box->addItem(tr("Apex neural ×2"), static_cast<int>(Settings::FrameGenType::Anr));
    m_frame_gen_box->addItem(tr("Lossless ×2"), static_cast<int>(Settings::FrameGenType::Lsfg));
    connect(m_frame_gen_box, &QComboBox::activated, this, [this](int index) {
        const int type = m_frame_gen_box->itemData(index).toInt();
        Settings::values.frame_gen.SetValue(type >= 0);
        if (type >= 0) {
            Settings::values.frame_gen_type.SetValue(static_cast<Settings::FrameGenType>(type));
            if (type == static_cast<int>(Settings::FrameGenType::Anr)) {
                Settings::values.scaling_filter.SetValue(Settings::ScalingFilter::Anr);
            }
        }
        RefreshValues();
    });
    add_row(tr("Frame generation"), QString{}, m_frame_gen_box);

    add_row(tr("Console mode"), QString{}, segments({tr("Docked"), tr("Handheld")}, m_mode_group));
    connect(m_mode_group, &QButtonGroup::idClicked, this, [this](int id) {
        const bool docked =
            Settings::values.use_docked_mode.GetValue() == Settings::ConsoleMode::Docked;
        if ((id == 0) != docked) {
            m_main_window->OnToggleDockedMode();
        }
        RefreshValues();
    });

    // Volume: a slider with its value.
    section(tr("Sound and play"));
    auto* const volume = new QWidget(this);
    auto* const volume_row = new QHBoxLayout(volume);
    volume_row->setContentsMargins(0, 0, 0, 0);
    volume_row->setSpacing(6);
    m_volume_slider = new QSlider(Qt::Horizontal, volume);
    m_volume_slider->setRange(0, 100);
    m_volume_slider->setFixedWidth(120);
    const auto volume_step = [&](const QString& text, int delta) {
        auto* const step = new QPushButton(text, volume);
        step->setObjectName(QStringLiteral("QuickStep"));
        step->setCursor(Qt::PointingHandCursor);
        connect(step, &QPushButton::clicked, this, [this, delta] {
            m_volume_slider->setValue(std::clamp(m_volume_slider->value() + delta, 0, 100));
        });
        return step;
    };
    volume_row->addWidget(volume_step(QStringLiteral("−"), -5));
    m_volume_slider->setCursor(Qt::PointingHandCursor);
    m_volume_label = new QLabel(volume);
    m_volume_label->setObjectName(QStringLiteral("QuickRowLabel"));
    m_volume_label->setFixedWidth(42);
    m_volume_label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    volume_row->addWidget(m_volume_slider);
    volume_row->addWidget(volume_step(QStringLiteral("+"), 5));
    volume_row->addWidget(m_volume_label);
    connect(m_volume_slider, &QSlider::valueChanged, this, [this](int value) {
        m_volume_label->setText(QStringLiteral("%1%").arg(value));
        if (m_refreshing) {
            return;
        }
        Settings::values.audio_muted = false;
        Settings::values.volume.SetValue(static_cast<u8>(value));
        m_main_window->UpdateVolumeUI();
    });
    add_row(tr("Volume"), QString{}, volume);

    // The controller player 1 holds, with its battery, and the controllers window.
    auto* const controllers = new QPushButton(tr("Controllers..."), this);
    controllers->setObjectName(QStringLiteral("QuickSmall"));
    controllers->setCursor(Qt::PointingHandCursor);
    connect(controllers, &QPushButton::clicked, this, [this] {
        hide();
        ControllersDialog dialog{m_main_window->input_subsystem.get(), [this] {
                                     m_main_window->configure_start_page = 4; // Controls
                                     m_main_window->OnConfigure();
                                 },
                                 m_main_window};
        dialog.exec();
    });
    m_controller_label = add_row(tr("Controller"), QStringLiteral(" "), controllers);

    // Amiibo: the Amiibo Library, to tap a figure in.
    auto* const amiibo = new QPushButton(tr("Choose amiibo..."), this);
    amiibo->setObjectName(QStringLiteral("QuickSmall"));
    amiibo->setCursor(Qt::PointingHandCursor);
    connect(amiibo, &QPushButton::clicked, this, [this] {
        hide();
        m_main_window->OnLoadAmiibo();
    });
    const QString amiibo_key = KeyFor("Amiibo Library");
    add_row(tr("Amiibo"), amiibo_key.isEmpty() ? tr("Tap a figure in") : tr("Tap a figure in · %1").arg(amiibo_key),
            amiibo);

    // Save slots: a copy of the game's save, kept apart and put back later.
    auto* const slots_label = new QLabel(tr("Save slots"), this);
    slots_label->setObjectName(QStringLiteral("QuickSection"));
    root->addWidget(slots_label);
    auto* const slot_row = new QHBoxLayout;
    slot_row->setSpacing(5);
    for (int i = 0; i < 5; ++i) {
        const int slot_num = i + 1;
        auto* const button = new QPushButton(QString::number(slot_num), this);
        button->setObjectName(QStringLiteral("QuickSlot"));
        button->setCheckable(true);
        button->setCursor(Qt::PointingHandCursor);
        connect(button, &QPushButton::clicked, this, [this, slot_num] { UpdateSlotInfo(slot_num); });
        m_slot_buttons[static_cast<size_t>(i)] = button;
        slot_row->addWidget(button);
    }
    root->addLayout(slot_row);
    auto* const slot_card = new QHBoxLayout;
    slot_card->setSpacing(10);
    m_slot_thumbnail = new QLabel(this);
    m_slot_thumbnail->setFixedSize(96, 54);
    m_slot_thumbnail->setAlignment(Qt::AlignCenter);
    m_slot_meta_label = new QLabel(this);
    m_slot_meta_label->setWordWrap(true);
    m_slot_save_btn = new QPushButton(tr("Save"), this);
    m_slot_load_btn = new QPushButton(tr("Load"), this);
    m_slot_restart_btn = new QPushButton(tr("Restart to load"), this);
    for (QPushButton* const button : {m_slot_save_btn, m_slot_load_btn, m_slot_restart_btn}) {
        button->setObjectName(QStringLiteral("QuickSlotAction"));
        button->setCursor(Qt::PointingHandCursor);
    }
    m_slot_restart_btn->setEnabled(false);
    connect(m_slot_save_btn, &QPushButton::clicked, this, &QuickMenuOverlay::SaveSelectedSlot);
    connect(m_slot_load_btn, &QPushButton::clicked, this, &QuickMenuOverlay::LoadSelectedSlot);
    connect(m_slot_restart_btn, &QPushButton::clicked, this, [this] {
        hide();
        m_main_window->OnRestartGame();
    });
    slot_card->addWidget(m_slot_thumbnail);
    slot_card->addWidget(m_slot_meta_label, 1);
    root->addLayout(slot_card);
    auto* const save_load = new QHBoxLayout;
    save_load->setSpacing(6);
    save_load->addWidget(m_slot_save_btn);
    save_load->addWidget(m_slot_load_btn);
    save_load->addWidget(m_slot_restart_btn, 1);
    root->addLayout(save_load);

    m_note = new QLabel(this);
    m_note->setObjectName(QStringLiteral("QuickNote"));
    m_note->setWordWrap(true);
    m_note->hide();
    root->addWidget(m_note);

    // Everything else about the game.
    auto* const all_settings = new QPushButton(tr("All game settings"), this);
    all_settings->setObjectName(QStringLiteral("QuickLink"));
    all_settings->setIcon(ActionIcon(Action::Settings));
    all_settings->setCursor(Qt::PointingHandCursor);
    if (const QString key = KeyFor("Configure Current Game"); !key.isEmpty()) {
        auto* const key_row = new QHBoxLayout(all_settings);
        key_row->setContentsMargins(0, 0, 12, 0);
        key_row->addStretch(1);
        auto* const key_label = new QLabel(key, all_settings);
        key_label->setObjectName(QStringLiteral("QuickKey"));
        key_label->setAttribute(Qt::WA_TransparentForMouseEvents);
        key_row->addWidget(key_label);
    }
    connect(all_settings, &QPushButton::clicked, this, [this] {
        hide();
        m_main_window->OnConfigurePerGame();
    });
    root->addWidget(all_settings);

    // The performance card and the status strip, fed by the numbers the status bar gets.
    m_perf_card = new HudPanel(parent, [this](QPainter& p, const QRect& r) { PaintPerfCard(p, r); });
    m_perf_card->setFixedSize(300, 196);
    auto* const strip = new HudPanel(parent, [this](QPainter& p, const QRect& r) { PaintStatusStrip(p, r); });
    strip->setFixedHeight(42);
    strip->on_click = [this, strip](const QPoint& pos) {
        // The pill at its right end closes the menu.
        if (pos.x() > strip->width() - 120) {
            hide();
        }
    };
    m_status_strip = strip;
    if (m_main_window) {
        m_main_window->installEventFilter(this);
        connect(m_main_window, &MainWindow::statsUpdated, this,
                [this](const Core::PerfStatsResults& results, const VideoCore::ShaderNotify&) {
                    UpdateStats(results);
                });
    }
}

QuickMenuOverlay::~QuickMenuOverlay() = default;

void QuickMenuOverlay::ShowNote(const QString& text) {
    m_note->setText(text);
    m_note->show();
    adjustSize();
}

void QuickMenuOverlay::ApplyPreset(int preset) {
    // Low 0.75x, Native 1x, High 1.5x, Ultra 2x, scaled up with FSR.
    static constexpr Settings::ResolutionSetup scales[] = {
        Settings::ResolutionSetup::Res3_4X, Settings::ResolutionSetup::Res1X,
        Settings::ResolutionSetup::Res3_2X, Settings::ResolutionSetup::Res2X};
    if (preset < 0 || preset >= 4) {
        return;
    }
    Settings::values.resolution_setup.SetValue(scales[preset]);
    if (Settings::values.scaling_filter.GetValue() != Settings::ScalingFilter::Anr) {
        Settings::values.scaling_filter.SetValue(Settings::ScalingFilter::Fsr);
    }
    ShowNote(tr("The preset's resolution applies the next time the game starts."));
    RefreshValues();
}

void QuickMenuOverlay::RefreshValues() {
    m_refreshing = true;
    // The game.
    const u64 pid = QtCommon::system ? QtCommon::system->GetApplicationProcessProgramID() : 0;
    QString title;
    QPixmap picture;
    if (m_main_window->game_list != nullptr) {
        const QModelIndex game = m_main_window->game_list->SelectedGame();
        if (game.isValid() && game.data(GameListItemPath::ProgramIdRole).toULongLong() == pid) {
            title = game.data(GameListItemPath::TitleRole).toString();
            picture = game.data(GameListItemPath::LargePictureRole).value<QPixmap>();
            if (picture.isNull()) {
                picture = game.data(Qt::DecorationRole).value<QPixmap>();
            }
        }
    }
    if (title.isEmpty() && QtCommon::system) {
        std::string name;
        if (QtCommon::system->GetGameName(name) == Loader::ResultStatus::Success) {
            title = QString::fromStdString(name);
        }
    }
    m_game_title->setText(title.isEmpty() ? tr("Playing") : title);
    m_game_icon->setPixmap(picture);
    m_game_icon->setVisible(!picture.isNull());
    if (m_main_window->play_time_manager && pid != 0) {
        const u64 minutes = m_main_window->play_time_manager->GetPlayTime(pid) / 60;
        m_game_played->setText(minutes < 60 ? tr("%1m played").arg(minutes)
                                            : tr("%1h %2m played").arg(minutes / 60).arg(minutes % 60));
    } else {
        m_game_played->clear();
    }
    const bool running = QtCommon::emu_thread != nullptr && QtCommon::emu_thread->IsRunning();
    m_pause_button->setIcon(ActionIcon(running ? Action::Pause : Action::Restart));
    // Chips.
    m_frame_gen_chip->setChecked(Settings::values.frame_gen.GetValue());
    m_turbo_chip->setChecked(!Settings::values.use_speed_limit.GetValue());
    m_hud_chip->setChecked(UISettings::values.show_perf_overlay.GetValue());
    const int mods = EnabledModCount(pid);
    m_mods_chip->setText(mods > 0 ? tr("Mods · %1").arg(mods) : tr("Mods"));
    if (m_preset_hint != nullptr) {
        m_preset_hint->setText(Settings::values.resolution_setup.UsingGlobal()
                                   ? tr("Global · from the next start")
                                   : tr("This game's settings · from the next start"));
    }
    // The controller: player 1's, or the handheld's when nothing is on player 1.
    if (m_controller_label != nullptr) {
        QString text = tr("None connected");
        if (QtCommon::system != nullptr) {
            auto& hid = QtCommon::system->HIDCore();
            const Core::HID::EmulatedController* pad =
                hid.GetEmulatedController(Core::HID::NpadIdType::Player1);
            if (pad == nullptr || !pad->IsConnected()) {
                pad = hid.GetEmulatedController(Core::HID::NpadIdType::Handheld);
            }
            if (pad != nullptr && pad->IsConnected()) {
                using Core::HID::NpadStyleIndex;
                const NpadStyleIndex style = pad->GetNpadStyleIndex();
                QString name;
                switch (style) {
                case NpadStyleIndex::Fullkey:
                    name = tr("Pro Controller");
                    break;
                case NpadStyleIndex::Handheld:
                    name = tr("Handheld");
                    break;
                case NpadStyleIndex::JoyconDual:
                    name = tr("Joy-Con pair");
                    break;
                case NpadStyleIndex::JoyconLeft:
                    name = tr("Left Joy-Con");
                    break;
                case NpadStyleIndex::JoyconRight:
                    name = tr("Right Joy-Con");
                    break;
                case NpadStyleIndex::GameCube:
                    name = tr("GameCube controller");
                    break;
                default:
                    name = tr("Controller");
                    break;
                }
                const auto battery = pad->GetBattery();
                const auto& power = style == NpadStyleIndex::JoyconLeft  ? battery.left
                                    : style == NpadStyleIndex::JoyconRight ? battery.right
                                                                           : battery.dual;
                auto level = power.battery_level;
                if (style == NpadStyleIndex::JoyconDual && battery.left.is_powered &&
                    battery.right.is_powered) {
                    level = std::min(battery.left.battery_level, battery.right.battery_level);
                }
                const bool reports = power.is_powered || style == NpadStyleIndex::JoyconDual;
                text = reports ? tr("%1 · battery %2%")
                                     .arg(name)
                                     .arg(static_cast<int>(level) * 25)
                               : name;
                if (power.is_charging) {
                    text += tr(" · charging");
                }
            }
        }
        m_controller_label->setText(text);
    }
    // Preset, resolution, frame generation, console mode, volume.
    const auto scale = Settings::values.resolution_setup.GetValue();
    const int preset = scale == Settings::ResolutionSetup::Res3_4X  ? 0
                       : scale == Settings::ResolutionSetup::Res1X  ? 1
                       : scale == Settings::ResolutionSetup::Res3_2X ? 2
                       : scale == Settings::ResolutionSetup::Res2X  ? 3
                                                                    : -1;
    if (preset >= 0) {
        m_preset_group->button(preset)->setChecked(true);
    } else if (QAbstractButton* const checked = m_preset_group->checkedButton()) {
        m_preset_group->setExclusive(false);
        checked->setChecked(false);
        m_preset_group->setExclusive(true);
    }
    const int scale_index = m_resolution_box->findData(static_cast<int>(scale));
    if (scale_index >= 0) {
        m_resolution_box->setCurrentIndex(scale_index);
    }
    const int frame_gen = Settings::values.frame_gen.GetValue()
                              ? static_cast<int>(Settings::values.frame_gen_type.GetValue())
                              : -1;
    const int frame_gen_index = m_frame_gen_box->findData(frame_gen);
    m_frame_gen_box->setCurrentIndex(frame_gen_index >= 0 ? frame_gen_index : 0);
    const bool docked = Settings::values.use_docked_mode.GetValue() == Settings::ConsoleMode::Docked;
    m_mode_group->button(docked ? 0 : 1)->setChecked(true);
    m_volume_slider->setValue(Settings::values.audio_muted.GetValue()
                                  ? 0
                                  : Settings::values.volume.GetValue());
    m_volume_label->setText(QStringLiteral("%1%").arg(m_volume_slider->value()));
    m_refreshing = false;
    UpdateSlotInfo(m_selected_slot);
}

void QuickMenuOverlay::Toggle() {
    if (isVisible()) {
        hide();
    } else {
        m_note->hide();
        RefreshValues();
        adjustSize();
        Reposition();
        show();
        PlacePanels();
        m_perf_card->show();
        m_status_strip->show();
        raise();
        activateWindow();
    }
}

void QuickMenuOverlay::Reposition() {
    if (!m_main_window) {
        return;
    }
    // At the game's top right, clear of the edge.
    const QWidget* const area = m_main_window->centralWidget() != nullptr
                                    ? m_main_window->centralWidget()
                                    : m_main_window;
    const QPoint top_right = area->mapToGlobal(QPoint(area->width(), 0));
    move(top_right.x() - width() - 18, top_right.y() + 18);
    PlacePanels();
}

void QuickMenuOverlay::PlacePanels() {
    if (!m_main_window || m_perf_card == nullptr) {
        return;
    }
    const QWidget* const area = m_main_window->centralWidget() != nullptr
                                    ? m_main_window->centralWidget()
                                    : m_main_window;
    const QPoint top_left = area->mapToGlobal(QPoint(0, 0));
    m_perf_card->move(top_left.x() + 18, top_left.y() + 18);
    // The strip runs along the bottom, from the card's edge to the menu's.
    const int strip_w = std::max(360, area->width() - 36);
    m_status_strip->setFixedWidth(strip_w);
    m_status_strip->move(top_left.x() + 18, top_left.y() + area->height() - 42 - 18);
}

void QuickMenuOverlay::hideEvent(QHideEvent* event) {
    if (m_perf_card != nullptr) {
        m_perf_card->hide();
        m_status_strip->hide();
    }
    QWidget::hideEvent(event);
}

QString QuickMenuOverlay::KeyFor(const char* action) const {
    if (m_main_window == nullptr) {
        return {};
    }
    return m_main_window->hotkey_registry.GetKeySequence("Main Window", action)
        .toString(QKeySequence::NativeText);
}

QString QuickMenuOverlay::RendererText() const {
    QString api;
    switch (Settings::values.renderer_backend.GetValue()) {
    case Settings::RendererBackend::Vulkan:
        api = QStringLiteral("Vulkan");
        break;
    case Settings::RendererBackend::Null:
        api = tr("No renderer");
        break;
    default:
        api = QStringLiteral("OpenGL");
        break;
    }
    const bool docked = Settings::values.use_docked_mode.GetValue() == Settings::ConsoleMode::Docked;
    return api + QStringLiteral(" · ") + (docked ? tr("Docked") : tr("Handheld"));
}

void QuickMenuOverlay::UpdateStats(const Core::PerfStatsResults& results) {
    if (!isVisible()) {
        return;
    }
    const double base = results.average_game_fps;
    if (!std::isnan(base)) {
        m_stats.base = base;
        m_stats.fps = m_main_window->DisplayedFps(base);
        // Under 3 fps is a load or a freeze, not a frame rate to chart.
        if (base > 3.0) {
            m_stats.history.push_back(base);
            while (m_stats.history.size() > 60) {
                m_stats.history.pop_front();
            }
        }
        if (m_stats.history.size() >= 4) {
            m_stats.low = *std::min_element(m_stats.history.begin(), m_stats.history.end());
        }
        m_stats.valid = true;
    }
    if (!std::isnan(results.frametime)) {
        m_stats.frame_ms = results.frametime * 1000.0;
    }
    if (!std::isnan(results.emulation_speed)) {
        m_stats.speed = results.emulation_speed * 100.0;
    }
    m_stats.upscale = m_main_window->UpscaleStatusText();
    m_stats.frame_gen = m_main_window->FrameGenStatusText();
    const auto memory = Common::GetProcessMemoryUsage();
    const u64 ram = memory.working_set != 0 ? memory.working_set : memory.commit;
    m_stats.memory = ram != 0 ? tr("RAM %1 GB").arg(static_cast<double>(ram) / (1ULL << 30), 0, 'f', 1)
                              : QString{};
    if (QtCommon::system != nullptr && QtCommon::system->IsPoweredOn()) {
        const auto video = QtCommon::system->GPU().Renderer().GetDeviceMemoryStatus();
        if (video.used != 0) {
            const QString vram =
                video.budget != 0
                    ? tr("VRAM %1 / %2 GB")
                          .arg(static_cast<double>(video.used) / (1ULL << 30), 0, 'f', 1)
                          .arg(static_cast<double>(video.budget) / (1ULL << 30), 0, 'f', 1)
                    : tr("VRAM %1 GB").arg(static_cast<double>(video.used) / (1ULL << 30), 0, 'f', 1);
            m_stats.memory = m_stats.memory.isEmpty() ? vram : m_stats.memory + QStringLiteral("  ·  ") + vram;
        }
    }
    m_perf_card->update();
    m_status_strip->update();
}

void QuickMenuOverlay::PaintPerfCard(QPainter& p, const QRect& rect) const {
    const QRectF r = QRectF(rect).adjusted(1, 1, -1, -1);
    PaintGlass(p, r, 16);
    QFont font = p.font();
    const auto set_font = [&](int px, QFont::Weight weight) {
        font.setPixelSize(px);
        font.setWeight(weight);
        p.setFont(font);
    };
    const qreal left = r.left() + 16;
    const qreal right = r.right() - 16;
    // Its name, and how to put it away.
    set_font(11, QFont::Bold);
    p.setPen(HudMuted);
    p.drawText(QRectF(left, r.top() + 12, 200, 16), Qt::AlignLeft | Qt::AlignVCenter, tr("PERFORMANCE"));
    p.drawText(QRectF(right - 140, r.top() + 12, 140, 16), Qt::AlignRight | Qt::AlignVCenter,
               tr("Esc to close"));
    // The frame rate, big, and what it is made of.
    const QString fps = m_stats.valid ? QString::number(std::lround(m_stats.fps)) : QStringLiteral("—");
    set_font(40, QFont::Black);
    p.setPen(HudText);
    const QFontMetrics big{font};
    p.drawText(QPointF(left, r.top() + 34 + big.ascent()), fps);
    const qreal after = left + big.horizontalAdvance(fps) + 8;
    set_font(13, QFont::Black);
    p.setPen(HudGold);
    p.drawText(QPointF(after, r.top() + 50), tr("FPS"));
    set_font(11, QFont::DemiBold);
    p.setPen(HudMuted);
    const bool generated = m_stats.valid && m_stats.fps > m_stats.base + 0.5;
    p.drawText(QPointF(after, r.top() + 66),
               generated ? tr("%1 rendered + frame gen").arg(std::lround(m_stats.base))
                         : tr("speed %1%").arg(std::lround(m_stats.speed)));
    // The lowest of the last half minute.
    set_font(11, QFont::Bold);
    p.setPen(HudMuted);
    p.drawText(QRectF(right - 80, r.top() + 36, 80, 14), Qt::AlignRight, tr("1% LOW"));
    set_font(18, QFont::Black);
    p.setPen(m_stats.low > 0 && m_stats.low < 28 ? HudEmber : HudText);
    p.drawText(QRectF(right - 80, r.top() + 50, 80, 24), Qt::AlignRight,
               m_stats.low > 0 ? QString::number(std::lround(m_stats.low)) : QStringLiteral("—"));

    // The frame rate over the last half minute.
    const QRectF graph{left, r.top() + 88, right - left, 40};
    p.setPen(QPen(QColor(0x3A, 0x2E, 0x25), 1, Qt::DashLine));
    p.drawLine(QPointF(graph.left(), graph.center().y()), QPointF(graph.right(), graph.center().y()));
    if (m_stats.history.size() >= 2) {
        const double top_fps = std::max(
            60.0, *std::max_element(m_stats.history.begin(), m_stats.history.end()) * 1.1);
        QPainterPath line;
        const size_t n = m_stats.history.size();
        for (size_t i = 0; i < n; ++i) {
            const qreal x = graph.left() + graph.width() * static_cast<qreal>(i) / 59.0;
            const qreal y = graph.bottom() - graph.height() * std::clamp(m_stats.history[i] / top_fps, 0.0, 1.0);
            if (i == 0) {
                line.moveTo(x, y);
            } else {
                line.lineTo(x, y);
            }
        }
        QPainterPath fill = line;
        fill.lineTo(graph.left() + graph.width() * static_cast<qreal>(n - 1) / 59.0, graph.bottom());
        fill.lineTo(graph.left(), graph.bottom());
        fill.closeSubpath();
        QLinearGradient shade{graph.topLeft(), graph.bottomLeft()};
        shade.setColorAt(0.0, QColor(0xFF, 0x5A, 0x1F, 90));
        shade.setColorAt(1.0, QColor(0xFF, 0x5A, 0x1F, 0));
        p.fillPath(fill, shade);
        p.setPen(QPen(HudEmber, 1.8));
        p.setBrush(Qt::NoBrush);
        p.drawPath(line);
    }

    // The details: frame time, renderer, upscaling, frame generation, memory.
    set_font(12, QFont::DemiBold);
    qreal y = graph.bottom() + 8;
    const auto line_of = [&](const QString& name, const QString& value) {
        if (value.isEmpty()) {
            return;
        }
        p.setPen(HudMuted);
        p.drawText(QRectF(left, y, 90, 16), Qt::AlignLeft | Qt::AlignVCenter, name);
        p.setPen(HudText);
        p.drawText(QRectF(left + 90, y, right - left - 90, 16), Qt::AlignRight | Qt::AlignVCenter,
                   QFontMetrics(font).elidedText(value, Qt::ElideRight, static_cast<int>(right - left - 90)));
        y += 17;
    };
    line_of(tr("Frame"), m_stats.frame_ms > 0 ? tr("%1 ms").arg(m_stats.frame_ms, 0, 'f', 1) : QString{});
    QString picture = RendererText();
    if (!m_stats.upscale.isEmpty()) {
        picture += QStringLiteral(" · ") + m_stats.upscale;
    }
    line_of(tr("Picture"), picture);
    line_of(tr("Frame gen"), m_stats.frame_gen);
    line_of(tr("Memory"), m_stats.memory);
}

void QuickMenuOverlay::PaintStatusStrip(QPainter& p, const QRect& rect) const {
    const QRectF r = QRectF(rect).adjusted(1, 1, -1, -1);
    PaintGlass(p, r, r.height() / 2);
    QFont font = p.font();
    font.setPixelSize(13);
    font.setWeight(QFont::Bold);
    p.setFont(font);
    // Left: the renderer, the console mode and the upscaling, with a dot that says it is running.
    const bool running = QtCommon::emu_thread != nullptr && QtCommon::emu_thread->IsRunning();
    p.setPen(Qt::NoPen);
    p.setBrush(running ? HudGood : HudGold);
    p.drawEllipse(QPointF(r.left() + 20, r.center().y()), 4, 4);
    QString left_text = RendererText();
    if (!m_stats.upscale.isEmpty()) {
        left_text += QStringLiteral(" · ") + m_stats.upscale;
    }
    if (!running) {
        left_text = tr("Paused · ") + left_text;
    }
    p.setPen(HudText);
    p.drawText(QRectF(r.left() + 34, r.top(), r.width() / 2, r.height()), Qt::AlignLeft | Qt::AlignVCenter,
               left_text);
    // Right: the frame rate (rendered → shown) and frame time, then the menu's pill.
    const QRectF pill{r.right() - 112, r.top() + 6, 106, r.height() - 12};
    QString right_text;
    if (m_stats.valid) {
        const bool generated = m_stats.fps > m_stats.base + 0.5;
        right_text = generated ? tr("%1 → %2 FPS").arg(std::lround(m_stats.base)).arg(std::lround(m_stats.fps))
                               : tr("%1 FPS").arg(std::lround(m_stats.fps));
        if (m_stats.frame_ms > 0) {
            right_text += tr(" · %1 ms").arg(m_stats.frame_ms, 0, 'f', 1);
        }
    }
    p.setPen(HudText);
    p.drawText(QRectF(r.left(), r.top(), pill.left() - 14 - r.left(), r.height()),
               Qt::AlignRight | Qt::AlignVCenter, right_text);
    QLinearGradient fill{pill.topLeft(), pill.topRight()};
    fill.setColorAt(0.0, HudEmber);
    fill.setColorAt(1.0, HudGold);
    p.setPen(Qt::NoPen);
    p.setBrush(fill);
    p.drawRoundedRect(pill, pill.height() / 2, pill.height() / 2);
    p.setPen(QColor(0x1A, 0x0C, 0x05));
    p.drawText(pill, Qt::AlignCenter, tr("Menu  ·  Esc"));
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

    painter.fillRect(r, PhoenixTheme::Recolor(QColor(0x14, 0x10, 0x0D, 240)));

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
        if (event->type() == QEvent::WindowStateChange && isVisible()) {
            Reposition(); // full screen and back
        } else if (event->type() == QEvent::Hide) {
            hide(); // the window minimised: the panels go with it
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
        const bool has_save = std::filesystem::exists(GetSlotDir(s_num) / "meta.txt", ec);
        m_slot_buttons[i]->setText(has_save ? QStringLiteral("%1 \u25CF").arg(s_num)
                                            : QString::number(s_num));
        m_slot_buttons[i]->setChecked(s_num == slot_index);
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
