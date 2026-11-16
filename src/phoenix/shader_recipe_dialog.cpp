// SPDX-FileCopyrightText: Copyright 2026 PhoenixEmu Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "shader_recipe_dialog.h"

#include <filesystem>
#include <fstream>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

#include "common/fs/path_util.h"
#include "common/logging.h"
#include "fmt/format.h"

namespace {

std::filesystem::path GetShaderBaseDir() {
    return Common::FS::GetEdenPath(Common::FS::EdenPath::ShaderDir);
}

std::filesystem::path GetAppRecipesDir() {
    return std::filesystem::path(QCoreApplication::applicationDirPath().toStdWString()) / "recipes";
}

} // namespace

ShaderRecipeDialog::ShaderRecipeDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Shader Recipe & Precache Manager"));
    setMinimumSize(680, 440);

    auto* main_layout = new QVBoxLayout(this);

    auto* header_label = new QLabel(
        tr("<b>Precached Shader Tooling & Distribution</b><br>"
           "Manage, export, and import precompiled shader recipes to eliminate in-game shader stutter."),
        this);
    header_label->setWordWrap(true);
    main_layout->addWidget(header_label);

    auto* body_layout = new QHBoxLayout();

    // Game selection list
    game_list = new QListWidget(this);
    game_list->setMinimumWidth(280);
    connect(game_list, &QListWidget::currentRowChanged, this, &ShaderRecipeDialog::OnGameSelectionChanged);
    body_layout->addWidget(game_list, 1);

    // Details panel
    auto* right_layout = new QVBoxLayout();
    details_label = new QLabel(this);
    details_label->setWordWrap(true);
    details_label->setStyleSheet(
        QStringLiteral("background: rgba(255, 255, 255, 0.04); border: 1px solid rgba(255, 255, 255, 0.1); "
                       "border-radius: 6px; padding: 12px; font-family: monospace;"));
    right_layout->addWidget(details_label, 1);

    auto* btn_layout = new QVBoxLayout();

    export_btn = new QPushButton(tr("Export Precache Recipe..."), this);
    connect(export_btn, &QPushButton::clicked, this, &ShaderRecipeDialog::OnExportPackage);
    btn_layout->addWidget(export_btn);

    import_btn = new QPushButton(tr("Import Recipe / Precache File..."), this);
    connect(import_btn, &QPushButton::clicked, this, &ShaderRecipeDialog::OnImportPackage);
    btn_layout->addWidget(import_btn);

    open_folder_btn = new QPushButton(tr("Open Shader Cache Folder"), this);
    connect(open_folder_btn, &QPushButton::clicked, this, &ShaderRecipeDialog::OnOpenShaderFolder);
    btn_layout->addWidget(open_folder_btn);

    right_layout->addLayout(btn_layout);
    body_layout->addLayout(right_layout, 2);

    main_layout->addLayout(body_layout);

    auto* close_btn = new QPushButton(tr("Close"), this);
    connect(close_btn, &QPushButton::clicked, this, &QDialog::accept);
    main_layout->addWidget(close_btn);

    PopulateGames();
}

ShaderRecipeDialog::~ShaderRecipeDialog() = default;

void ShaderRecipeDialog::PopulateGames() {
    games.clear();
    game_list->clear();

    const auto shader_base = GetShaderBaseDir();
    const auto recipes_dir = GetAppRecipesDir();

    std::error_code ec;
    if (std::filesystem::exists(shader_base, ec)) {
        for (const auto& entry : std::filesystem::directory_iterator(shader_base, ec)) {
            if (!entry.is_directory(ec)) {
                continue;
            }
            const auto dir_name = entry.path().filename().string();
            if (dir_name.length() != 16) {
                continue;
            }

            try {
                u64 id = std::stoull(dir_name, nullptr, 16);
                GameEntry game;
                game.program_id = id;
                game.title = QStringLiteral("%1").arg(id, 16, 16, QLatin1Char('0')).toUpper();
                game.shader_dir = QString::fromStdString(Common::FS::PathToUTF8String(entry.path()));

                // Check for recipe
                const auto recipe_in_dir = entry.path() / fmt::format("{:016x}.nx2recipe", id);
                const auto recipe_in_recipes = recipes_dir / fmt::format("{:016X}.nx2recipe", id);
                if (std::filesystem::exists(recipe_in_dir, ec)) {
                    game.has_recipe = true;
                    game.recipe_path = QString::fromStdString(Common::FS::PathToUTF8String(recipe_in_dir));
                } else if (std::filesystem::exists(recipe_in_recipes, ec)) {
                    game.has_recipe = true;
                    game.recipe_path = QString::fromStdString(Common::FS::PathToUTF8String(recipe_in_recipes));
                }

                // Check for precompiled pipelines
                for (const auto& sub : std::filesystem::directory_iterator(entry.path(), ec)) {
                    if (sub.path().extension() == ".nx2prep" || sub.path().filename() == "vulkan.bin") {
                        game.has_precompiled = true;
                        game.pipeline_count++;
                    }
                }

                games.push_back(game);
                game_list->addItem(QStringLiteral("0x%1 %2")
                                       .arg(id, 16, 16, QLatin1Char('0')).toUpper()
                                       .arg(game.has_recipe ? tr("[Recipe Available]") : QString()));
            } catch (...) {
            }
        }
    }

    if (games.empty()) {
        details_label->setText(tr("No games found in the shader cache yet.\n"
                                  "Play a game or import a recipe to get started!"));
        export_btn->setEnabled(false);
    } else {
        game_list->setCurrentRow(0);
    }
}

void ShaderRecipeDialog::OnGameSelectionChanged() {
    UpdateDetails();
}

void ShaderRecipeDialog::UpdateDetails() {
    int row = game_list->currentRow();
    if (row < 0 || row >= static_cast<int>(games.size())) {
        details_label->setText(tr("Select a game from the list to view its shader status."));
        export_btn->setEnabled(false);
        return;
    }

    const auto& game = games[row];
    export_btn->setEnabled(game.has_recipe || game.has_precompiled);

    QString text;
    text += tr("<b>Title ID:</b> 0x%1<br>").arg(game.program_id, 16, 16, QLatin1Char('0')).toUpper();
    text += tr("<b>Recipe:</b> %1<br>")
                .arg(game.has_recipe ? tr("<font color='#4caf50'>Present (%1)</font>").arg(game.recipe_path)
                                     : tr("<font color='#e53935'>None</font>"));
    text += tr("<b>Precompiled Cache:</b> %1<br>")
                .arg(game.has_precompiled ? tr("<font color='#4caf50'>Available (%1 cache files)</font>").arg(game.pipeline_count)
                                          : tr("<font color='#ffb74d'>Not yet generated</font>"));
    text += tr("<b>Location:</b> %1<br>").arg(game.shader_dir);

    details_label->setText(text);
}

void ShaderRecipeDialog::OnOpenShaderFolder() {
    int row = game_list->currentRow();
    if (row >= 0 && row < static_cast<int>(games.size())) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(games[row].shader_dir));
    } else {
        const auto base = GetShaderBaseDir();
        std::error_code ec;
        std::filesystem::create_directories(base, ec);
        QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdString(Common::FS::PathToUTF8String(base))));
    }
}

void ShaderRecipeDialog::OnExportPackage() {
    int row = game_list->currentRow();
    if (row < 0 || row >= static_cast<int>(games.size())) {
        return;
    }

    const auto& game = games[row];
    const QString default_name = QStringLiteral("Recipe_%1.nx2recipe")
                                     .arg(game.program_id, 16, 16, QLatin1Char('0')).toUpper();

    const QString dest = QFileDialog::getSaveFileName(
        this, tr("Export Shader Recipe"), default_name, tr("Shader Recipe (*.nx2recipe);;All Files (*.*)"));
    if (dest.isEmpty()) {
        return;
    }

    std::error_code ec;
    if (game.has_recipe && !game.recipe_path.isEmpty()) {
        std::filesystem::copy_file(std::filesystem::path(game.recipe_path.toStdWString()),
                                   std::filesystem::path(dest.toStdWString()),
                                   std::filesystem::copy_options::overwrite_existing, ec);
    } else {
        // Create recipe manifest note
        std::ofstream out(std::filesystem::path(dest.toStdWString()));
        out << "NX2RECIPE " << fmt::format("{:016X}", game.program_id) << "\n";
    }

    if (!ec) {
        QMessageBox::information(this, tr("Export Successful"),
                                 tr("Shader recipe exported successfully to:\n%1").arg(dest));
    } else {
        QMessageBox::critical(this, tr("Export Failed"),
                              tr("Could not write recipe file: %1").arg(QString::fromStdString(ec.message())));
    }
}

void ShaderRecipeDialog::OnImportPackage() {
    const QString file = QFileDialog::getOpenFileName(
        this, tr("Import Shader Recipe or Precache"), {},
        tr("Shader Recipes & Precompiled (*.nx2recipe *.nx2prep *.bin);;All Files (*.*)"));
    if (file.isEmpty()) {
        return;
    }

    const std::filesystem::path src_path(file.toStdWString());
    const auto filename = src_path.filename().string();

    // Check if filename contains title ID
    u64 target_id = 0;
    try {
        if (filename.length() >= 16) {
            target_id = std::stoull(filename.substr(0, 16), nullptr, 16);
        }
    } catch (...) {
    }

    if (target_id == 0) {
        int row = game_list->currentRow();
        if (row >= 0 && row < static_cast<int>(games.size())) {
            target_id = games[row].program_id;
        }
    }

    if (target_id == 0) {
        QMessageBox::warning(this, tr("Import Shader Recipe"),
                             tr("Could not determine which game this recipe belongs to.\n"
                                "Please select a game from the list first."));
        return;
    }

    const auto target_dir = GetShaderBaseDir() / fmt::format("{:016x}", target_id);
    std::error_code ec;
    std::filesystem::create_directories(target_dir, ec);

    const auto dest_path = target_dir / src_path.filename();
    std::filesystem::copy_file(src_path, dest_path,
                               std::filesystem::copy_options::overwrite_existing, ec);

    if (!ec) {
        QMessageBox::information(
            this, tr("Import Successful"),
            tr("Shader recipe installed successfully for Title 0x%1.\nIt will be utilized automatically at next boot.")
                .arg(target_id, 16, 16, QLatin1Char('0')).toUpper());
        PopulateGames();
    } else {
        QMessageBox::critical(this, tr("Import Failed"),
                              tr("Failed to install recipe: %1").arg(QString::fromStdString(ec.message())));
    }
}

void ShaderRecipeDialog::OnPrecompileSelected() {
    // Refresh display
    PopulateGames();
}
