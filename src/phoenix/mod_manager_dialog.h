// SPDX-FileCopyrightText: 2026 PhoenixEmu contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <vector>
#include <QDialog>

#include "common/common_types.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;

class ModManagerDialog : public QDialog {
    Q_OBJECT

public:
    explicit ModManagerDialog(QWidget* parent = nullptr, u64 program_id = 0);
    ~ModManagerDialog() override;

private:
    void ScanGames();
    void RefreshMods();
    void OnGameSelected(int index);
    void ToggleMod(int row);
    void OpenModFolder();
    void InstallModFromZip();
    void ApplyPreset60FPS();
    void ApplyPresetDynamicRes();
    void ApplyPresetUltrawide();

    struct GameInfo {
        u64 program_id = 0;
        QString title;
    };

    struct ModEntry {
        QString name;
        std::filesystem::path path;
        bool enabled = true;
    };

    std::vector<GameInfo> m_games;
    std::vector<ModEntry> m_mods;
    u64 m_current_program_id = 0;

    QComboBox* m_game_combo = nullptr;
    QListWidget* m_mod_list = nullptr;
    QLabel* m_mod_status = nullptr;
    QPushButton* m_install_btn = nullptr;
    QPushButton* m_open_folder_btn = nullptr;
};
