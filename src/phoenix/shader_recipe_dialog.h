// SPDX-FileCopyrightText: Copyright 2026 PhoenixEmu Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <memory>
#include <vector>
#include <QDialog>
#include "common/common_types.h"

class QListWidget;
class QLabel;
class QPushButton;

class ShaderRecipeDialog : public QDialog {
    Q_OBJECT

public:
    explicit ShaderRecipeDialog(QWidget* parent = nullptr);
    ~ShaderRecipeDialog() override;

private slots:
    void OnGameSelectionChanged();
    void OnImportPackage();
    void OnExportPackage();
    void OnPrecompileSelected();
    void OnOpenShaderFolder();

private:
    void PopulateGames();
    void UpdateDetails();

    QListWidget* game_list{nullptr};
    QLabel* details_label{nullptr};
    QPushButton* import_btn{nullptr};
    QPushButton* export_btn{nullptr};
    QPushButton* precompile_btn{nullptr};
    QPushButton* open_folder_btn{nullptr};

    struct GameEntry {
        u64 program_id{};
        QString title;
        bool has_recipe{false};
        bool has_precompiled{false};
        QString recipe_path;
        QString shader_dir;
        u64 pipeline_count{0};
    };
    std::vector<GameEntry> games;
};
