// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <atomic>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <QDialog>
#include <QFutureWatcher>
#include <QList>
#include <QPixmap>
#include "configuration/shared_widget.h"
#include "core/file_sys/vfs/vfs_types.h"
#include "frontend_common/config.h"
#include "qt_common/config/qt_config.h"
#include "qt_common/config/shared_translation.h"
#include "qt_common/util/vk.h"
#include "phoenix/configuration/configuration_shared.h"

namespace Core {
class System;
}

namespace InputCommon {
class InputSubsystem;
}

class ConfigurePerGameAddons;
class ConfigureAudio;
class ConfigureCpu;
class ConfigureGraphics;
class ConfigureGraphicsAdvanced;
class ConfigureGraphicsExtensions;
class ConfigureInputPerGame;
class ConfigureSystem;
class ConfigureNetwork;
class ConfigureApplets;

class QGraphicsScene;
class QStandardItem;
class QStandardItemModel;
class QTreeView;
class QVBoxLayout;

namespace Ui {
class ConfigurePerGame;
}

class ConfigurePerGame : public QDialog {
    Q_OBJECT

public:
    // Cannot use std::filesystem::path due to https://bugreports.qt.io/browse/QTBUG-73263
    explicit ConfigurePerGame(QWidget* parent, u64 title_id_, const std::string& file_name,
                              std::vector<VkDeviceInfo::Record>& vk_device_records,
                              Core::System& system_);
    ~ConfigurePerGame() override;

    /// Save all button configurations to settings file
    void ApplyConfiguration();

    /// The name and picture the game list shows, used when the game file has none of its own.
    void SetListInfo(const QString& title, const QPixmap& picture);
    void LoadFromFile(FileSys::VirtualFile file_);

private:
    void done(int result) override;
    void changeEvent(QEvent* event) override;
    void RetranslateUI();

    void HandleApplyButtonClicked();

    void LoadConfiguration();
    void UpdateIconPreview(const QPixmap& map);
    void OnUploadNupIcon();
    void OnDetectNupIcon();
    bool SaveNupMetadata(bool show_error = false);

    struct DetectIconResult {
        std::vector<u8> icon_bytes;
        std::string resolved_title;
        std::string error;
    };

    std::unique_ptr<Ui::ConfigurePerGame> ui;
    FileSys::VirtualFile file;
    std::filesystem::path file_path;
    u64 title_id;
    bool is_nup_file{false};
    std::string initial_nup_title;
    std::vector<u8> pending_nup_icon;
    bool nup_icon_modified{false};
    QFutureWatcher<DetectIconResult> detect_watcher;
    std::shared_ptr<std::atomic_bool> detect_cancel;

    QGraphicsScene* scene;

    /// The settings shell around the pages (settings_shell.h): a sidebar with the game and its
    /// pages, a header with its picture, name, badge and how many settings are its own.
    void BuildShell();
    void FillPages();
    void FilterPages();
    void UpdateHeader(const QPixmap& icon);
    QString list_title;   ///< The game list's name and picture, for dumps without their own
    QPixmap list_picture;
    QPixmap header_picture;
    struct PageInfo {
        QWidget* page;
        QString name;
        int icon;
        bool advanced;
    };
    std::vector<PageInfo> page_infos;
    class QListWidget* page_list{};
    class QLineEdit* page_search{};
    class QLabel* side_icon{};
    class QLabel* side_name{};
    class QLabel* header_icon{};
    class QLabel* header_name{};
    class QLabel* header_changes{};
    class QLabel* header_badge{};
    class QLabel* header_meta{};

    std::unique_ptr<QtConfig> game_config;

    Core::System& system;
    std::unique_ptr<ConfigurationShared::Builder> builder;
    std::shared_ptr<std::vector<ConfigurationShared::Tab*>> tab_group;

    std::unique_ptr<ConfigurePerGameAddons> addons_tab;
    std::unique_ptr<ConfigureAudio> audio_tab;
    std::unique_ptr<ConfigureCpu> cpu_tab;
    std::unique_ptr<ConfigureGraphicsAdvanced> graphics_advanced_tab;
    std::unique_ptr<ConfigureGraphicsExtensions> graphics_extensions_tab;
    std::unique_ptr<ConfigureGraphics> graphics_tab;
    std::unique_ptr<ConfigureInputPerGame> input_tab;
    std::unique_ptr<ConfigureSystem> system_tab;
    std::unique_ptr<ConfigureNetwork> network_tab;
    std::unique_ptr<ConfigureApplets> applets_tab;
};
