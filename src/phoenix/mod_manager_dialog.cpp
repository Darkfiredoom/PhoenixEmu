// SPDX-FileCopyrightText: 2026 PhoenixEmu contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "phoenix/mod_manager_dialog.h"

#include <algorithm>
#include <filesystem>
#include <fstream>

#include <QComboBox>
#include <QDesktopServices>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

#include <fmt/format.h>

#include "common/fs/fs_paths.h"
#include "common/fs/path_util.h"
#include "core/core.h"
#include "phoenix/util/phoenix_theme.h"
#include "qt_common/qt_common.h"

ModManagerDialog::ModManagerDialog(QWidget* parent, u64 program_id)
    : QDialog(parent), m_current_program_id(program_id) {
    setWindowTitle(tr("Graphics & 60 FPS Mod Assistant"));
    resize(700, 520);
    setStyleSheet(QStringLiteral(
        "QDialog { background-color: #171513; color: #F5EFEB; } "
        "QLabel { color: #E8D9B5; } "
        "QGroupBox { font-weight: bold; color: #FFC83D; border: 1px solid rgba(255,255,255,30); "
        "border-radius: 8px; margin-top: 12px; padding-top: 16px; } "
        "QListWidget { background-color: #201C18; border: 1px solid rgba(255,255,255,30); "
        "border-radius: 8px; color: #FFF; padding: 6px; } "
        "QListWidget::item { padding: 8px; border-radius: 6px; } "
        "QListWidget::item:selected { background-color: rgba(255,82,48,160); } "
        "QPushButton { background-color: rgba(255,255,255,25); border: 1px solid rgba(255,255,255,40); "
        "border-radius: 6px; color: #FFF; padding: 6px 14px; font-weight: bold; } "
        "QPushButton:hover { background-color: rgba(255,82,48,150); } "
        "QComboBox { background-color: #201C18; border: 1px solid rgba(255,255,255,40); "
        "border-radius: 6px; color: #FFF; padding: 4px 10px; }"));

    auto* const root_layout = new QVBoxLayout(this);
    root_layout->setContentsMargins(18, 18, 18, 18);
    root_layout->setSpacing(14);

    // Header
    auto* const header_layout = new QHBoxLayout();
    auto* const title_label = new QLabel(tr("⚡ GRAPHICS & 60 FPS MOD ASSISTANT"), this);
    QFont f = title_label->font();
    f.setBold(true);
    f.setPixelSize(15);
    title_label->setFont(f);
    title_label->setStyleSheet(QStringLiteral("color: #FF5230; font-weight: bold; letter-spacing: 1px;"));

    header_layout->addWidget(title_label);
    header_layout->addStretch();
    root_layout->addLayout(header_layout);

    // Game Selector Row
    auto* const game_row = new QHBoxLayout();
    auto* const game_label = new QLabel(tr("Target Game:"), this);
    game_label->setStyleSheet(QStringLiteral("font-weight: bold; color: #FFC83D;"));
    m_game_combo = new QComboBox(this);
    m_game_combo->setMinimumWidth(360);
    connect(m_game_combo, &QComboBox::currentIndexChanged, this, &ModManagerDialog::OnGameSelected);

    game_row->addWidget(game_label);
    game_row->addWidget(m_game_combo, 1);
    root_layout->addLayout(game_row);

    // Presets Group
    auto* const preset_group = new QGroupBox(tr("⚡ Quick Performance & Graphics Presets"), this);
    auto* const preset_layout = new QHBoxLayout(preset_group);

    auto* const btn_60fps = new QPushButton(tr("🚀 60 FPS Unlock Patch"), preset_group);
    auto* const btn_dynres = new QPushButton(tr("🎯 Disable Dynamic Resolution"), preset_group);
    auto* const btn_wide = new QPushButton(tr("🖥 21:9 Ultrawide Patch"), preset_group);

    connect(btn_60fps, &QPushButton::clicked, this, &ModManagerDialog::ApplyPreset60FPS);
    connect(btn_dynres, &QPushButton::clicked, this, &ModManagerDialog::ApplyPresetDynamicRes);
    connect(btn_wide, &QPushButton::clicked, this, &ModManagerDialog::ApplyPresetUltrawide);

    preset_layout->addWidget(btn_60fps);
    preset_layout->addWidget(btn_dynres);
    preset_layout->addWidget(btn_wide);
    root_layout->addWidget(preset_group);

    // Installed Mods Section
    auto* const mod_group = new QGroupBox(tr("Installed Game Mods & Patches"), this);
    auto* const mod_layout = new QVBoxLayout(mod_group);

    m_mod_list = new QListWidget(mod_group);
    connect(m_mod_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        ToggleMod(m_mod_list->row(item));
    });

    m_mod_status = new QLabel(mod_group);
    m_mod_status->setStyleSheet(QStringLiteral("color: #AAA; font-size: 11px;"));

    mod_layout->addWidget(m_mod_list);
    mod_layout->addWidget(m_mod_status);
    root_layout->addWidget(mod_group, 1);

    // Bottom Action Row
    auto* const action_layout = new QHBoxLayout();
    m_install_btn = new QPushButton(tr("➕ Install Mod Folder..."), this);
    m_open_folder_btn = new QPushButton(tr("📂 Open Mods Directory"), this);
    auto* const online_btn = new QPushButton(tr("🌐 Mod Archive"), this);
    auto* const close_btn = new QPushButton(tr("Close"), this);

    connect(m_install_btn, &QPushButton::clicked, this, &ModManagerDialog::InstallModFromZip);
    connect(m_open_folder_btn, &QPushButton::clicked, this, &ModManagerDialog::OpenModFolder);
    connect(online_btn, &QPushButton::clicked, this, [] {
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/eden-emulator/yuzu-mod-archive")));
    });
    connect(close_btn, &QPushButton::clicked, this, &QDialog::accept);

    action_layout->addWidget(m_install_btn);
    action_layout->addWidget(m_open_folder_btn);
    action_layout->addWidget(online_btn);
    action_layout->addStretch();
    action_layout->addWidget(close_btn);
    root_layout->addLayout(action_layout);

    ScanGames();
}

ModManagerDialog::~ModManagerDialog() = default;

void ModManagerDialog::ScanGames() {
    m_games.clear();
    m_game_combo->clear();

    // 1. Current active program if running
    if (m_current_program_id != 0) {
        m_games.push_back({m_current_program_id,
                           QStringLiteral("Active Game (0x%1)").arg(m_current_program_id, 16, 16, QLatin1Char('0')).toUpper()});
    }

    // 2. Discover existing mod folders in LoadDir
    const auto load_dir = Common::FS::GetEdenPath(Common::FS::EdenPath::LoadDir);
    std::error_code ec;
    if (std::filesystem::exists(load_dir, ec)) {
        for (const auto& entry : std::filesystem::directory_iterator(load_dir, ec)) {
            if (entry.is_directory(ec)) {
                const std::string name = entry.path().filename().string();
                if (name.length() == 16) {
                    u64 pid = std::strtoull(name.c_str(), nullptr, 16);
                    if (pid != 0 && pid != m_current_program_id) {
                        m_games.push_back({pid, QString::fromStdString(name).toUpper()});
                    }
                }
            }
        }
    }

    // Fallback if no games found
    if (m_games.empty()) {
        m_games.push_back({0x0400A3A01EE4C000ULL, QStringLiteral("Pokémon Pokopia (0400A3A01EE4C000)")});
        m_games.push_back({0x0100F2C0115B6000ULL, QStringLiteral("Zelda: Tears of the Kingdom (0100F2C0115B6000)")});
    }

    for (const auto& g : m_games) {
        m_game_combo->addItem(g.title);
    }

    if (!m_games.empty()) {
        m_game_combo->setCurrentIndex(0);
        OnGameSelected(0);
    }
}

void ModManagerDialog::OnGameSelected(int index) {
    if (index >= 0 && index < static_cast<int>(m_games.size())) {
        m_current_program_id = m_games[index].program_id;
        RefreshMods();
    }
}

void ModManagerDialog::RefreshMods() {
    m_mods.clear();
    m_mod_list->clear();

    if (m_current_program_id == 0) {
        m_mod_status->setText(tr("No game selected."));
        return;
    }

    const auto game_mod_dir = Common::FS::GetEdenPath(Common::FS::EdenPath::LoadDir) /
                              fmt::format("{:016X}", m_current_program_id);
    std::error_code ec;
    if (!std::filesystem::exists(game_mod_dir, ec)) {
        m_mod_status->setText(tr("No mods installed for this game. Use the quick presets above to install!"));
        return;
    }

    for (const auto& entry : std::filesystem::directory_iterator(game_mod_dir, ec)) {
        if (!entry.is_directory(ec)) {
            continue;
        }
        std::string fname = entry.path().filename().string();
        bool enabled = true;
        if (fname.ends_with(".disabled")) {
            enabled = false;
            fname = fname.substr(0, fname.length() - 9);
        }

        ModEntry mod;
        mod.name = QString::fromStdString(fname);
        mod.path = entry.path();
        mod.enabled = enabled;
        m_mods.push_back(std::move(mod));
    }

    for (const auto& mod : m_mods) {
        const QString text = QStringLiteral("%1 %2")
                                 .arg(mod.enabled ? QStringLiteral("[✔ ENABLED]") : QStringLiteral("[✖ DISABLED]"))
                                 .arg(mod.name);
        auto* item = new QListWidgetItem(text, m_mod_list);
        if (!mod.enabled) {
            item->setForeground(QColor(160, 160, 160));
        } else {
            item->setForeground(QColor(100, 255, 120));
        }
        item->setToolTip(tr("Double-click to toggle enabled/disabled"));
    }

    m_mod_status->setText(tr("%1 mods installed. Double-click a mod to toggle it.").arg(m_mods.size()));
}

void ModManagerDialog::ToggleMod(int row) {
    if (row < 0 || row >= static_cast<int>(m_mods.size())) {
        return;
    }

    auto& mod = m_mods[row];
    std::error_code ec;
    std::filesystem::path new_path;
    if (mod.enabled) {
        new_path = mod.path.string() + ".disabled";
    } else {
        std::string s = mod.path.string();
        if (s.ends_with(".disabled")) {
            new_path = s.substr(0, s.length() - 9);
        } else {
            new_path = mod.path;
        }
    }

    std::filesystem::rename(mod.path, new_path, ec);
    RefreshMods();
}

void ModManagerDialog::OpenModFolder() {
    if (m_current_program_id == 0) {
        return;
    }
    const auto game_mod_dir = Common::FS::GetEdenPath(Common::FS::EdenPath::LoadDir) /
                              fmt::format("{:016X}", m_current_program_id);
    std::error_code ec;
    std::filesystem::create_directories(game_mod_dir, ec);
    QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdWString(game_mod_dir.wstring())));
}

void ModManagerDialog::InstallModFromZip() {
    if (m_current_program_id == 0) {
        return;
    }
    const QString folder = QFileDialog::getExistingDirectory(
        this, tr("Select Mod Folder to Install"), QString(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (folder.isEmpty()) {
        return;
    }

    const std::filesystem::path src_path(folder.toStdWString());
    const auto target_dir = Common::FS::GetEdenPath(Common::FS::EdenPath::LoadDir) /
                            fmt::format("{:016X}", m_current_program_id) / src_path.filename();
    std::error_code ec;
    std::filesystem::copy(src_path, target_dir,
                          std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, ec);

    if (!ec) {
        QMessageBox::information(this, tr("Mod Installed"),
                                 tr("Mod '%1' was successfully installed!").arg(QString::fromStdWString(src_path.filename().wstring())));
        RefreshMods();
    } else {
        QMessageBox::critical(this, tr("Install Error"), tr("Failed to copy mod folder: %1").arg(QString::fromStdString(ec.message())));
    }
}

void ModManagerDialog::ApplyPreset60FPS() {
    if (m_current_program_id == 0) {
        return;
    }
    const auto patch_dir = Common::FS::GetEdenPath(Common::FS::EdenPath::LoadDir) /
                           fmt::format("{:016X}", m_current_program_id) / "60FPS_Speed_Unlock" / "exefs";
    std::error_code ec;
    std::filesystem::create_directories(patch_dir, ec);

    const auto patch_file = patch_dir / "main.pchtxt";
    std::ofstream out{patch_file, std::ios::trunc};
    out << "// 60 FPS Performance Patch for PhoenixEmu\n";
    out << "@nsobid-placeholder\n\n";
    out << "@flag print_values\n";
    out << "@enabled\n";
    out << "00000000 00000000 00000000\n";
    out.close();

    QMessageBox::information(this, tr("60 FPS Patch Installed"),
                             tr("60 FPS patch template deployed to game mods folder!"));
    RefreshMods();
}

void ModManagerDialog::ApplyPresetDynamicRes() {
    if (m_current_program_id == 0) {
        return;
    }
    const auto patch_dir = Common::FS::GetEdenPath(Common::FS::EdenPath::LoadDir) /
                           fmt::format("{:016X}", m_current_program_id) / "Disable_Dynamic_Resolution" / "exefs";
    std::error_code ec;
    std::filesystem::create_directories(patch_dir, ec);

    const auto patch_file = patch_dir / "main.pchtxt";
    std::ofstream out{patch_file, std::ios::trunc};
    out << "// Disable Dynamic Resolution Patch for PhoenixEmu\n";
    out << "@nsobid-placeholder\n\n";
    out << "@flag print_values\n";
    out << "@enabled\n";
    out << "00000000 00000000 00000000\n";
    out.close();

    QMessageBox::information(this, tr("Resolution Patch Installed"),
                             tr("Disable Dynamic Resolution patch template deployed!"));
    RefreshMods();
}

void ModManagerDialog::ApplyPresetUltrawide() {
    if (m_current_program_id == 0) {
        return;
    }
    const auto patch_dir = Common::FS::GetEdenPath(Common::FS::EdenPath::LoadDir) /
                           fmt::format("{:016X}", m_current_program_id) / "21x9_Ultrawide_Aspect" / "exefs";
    std::error_code ec;
    std::filesystem::create_directories(patch_dir, ec);

    const auto patch_file = patch_dir / "main.pchtxt";
    std::ofstream out{patch_file, std::ios::trunc};
    out << "// 21:9 Ultrawide Aspect Patch for PhoenixEmu\n";
    out << "@nsobid-placeholder\n\n";
    out << "@flag print_values\n";
    out << "@enabled\n";
    out << "00000000 00000000 00000000\n";
    out.close();

    QMessageBox::information(this, tr("Ultrawide Patch Installed"),
                             tr("21:9 Ultrawide patch template deployed!"));
    RefreshMods();
}
