// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: 2016 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <vector>
#include <QDialog>
#include "configuration/shared_widget.h"
#include "qt_common/config/shared_translation.h"
#include "qt_common/util/vk.h"
#include "phoenix/configuration/configuration_shared.h"

namespace Core {
class System;
}

class ConfigureApplets;
class ConfigureAudio;
class ConfigureCpu;
class ConfigureDebugTab;
class ConfigureFilesystem;
class ConfigureGeneral;
class ConfigureGraphics;
class ConfigureGraphicsAdvanced;
class ConfigureGraphicsExtensions;
class ConfigureHotkeys;
class ConfigureInput;
class ConfigureProfileManager;
class ConfigureSystem;
class ConfigureNetwork;
class ConfigureUi;
class ConfigureWeb;

class HotkeyRegistry;

namespace InputCommon {
class InputSubsystem;
}

namespace Ui {
class ConfigureDialog;
}

class ConfigureDialog : public QDialog {
    Q_OBJECT

public:
    /// Opens on one of the left list's pages (0 General ... 5 Controls).
    void OpenPage(int row);

    explicit ConfigureDialog(QWidget* parent, HotkeyRegistry& registry_,
                             InputCommon::InputSubsystem* input_subsystem,
                             std::vector<VkDeviceInfo::Record>& vk_device_records,
                             Core::System& system_, bool enable_web_config = true);
    ~ConfigureDialog() override;

    void ApplyConfiguration();

    /// True when the window was closed with "Find Best Settings for This PC": the caller applies
    /// the recommended settings (common/recommended_settings.h) instead of this window's.
    [[nodiscard]] bool BestSettingsRequested() const {
        return best_settings_requested;
    }

    enum class MultiplayerAction {
        None,
        ViewLobby,
        CreateRoom,
        DirectConnect,
    };
    [[nodiscard]] MultiplayerAction RequestedMultiplayerAction() const {
        return multiplayer_action_requested;
    }

private slots:
    void OnLanguageChanged(const QString& locale);

signals:
    void LanguageChanged(const QString& locale);
    void ExternalContentDirsChanged();

private:
    void changeEvent(QEvent* event) override;
    void RetranslateUI();

    void HandleApplyButtonClicked();

    bool best_settings_requested = false;
    MultiplayerAction multiplayer_action_requested = MultiplayerAction::None;

    void SetConfiguration();
    void UpdateVisibleTabs();
    void PopulateSelectionList();
    /// Builds the sidebar, header and Everyday / Advanced switch around the pages.
    void BuildShell();
    /// Shows the pages whose settings match the search box (and the mode).
    void FilterPages();

    struct PageInfo {
        QString name;
        QString subtitle;
        QList<QWidget*> everyday;
        QList<QWidget*> advanced; ///< Shown too in Advanced mode
        int icon;
        bool advanced_only;
    };
    std::vector<PageInfo> pages;
    class QLineEdit* search_box{};
    class QLabel* page_title{};
    class QLabel* page_subtitle{};
    class QPushButton* switch2_button{};
    class QLabel* battery_label{};
    void UpdateBattery();
    InputCommon::InputSubsystem* switch2_input{};

    std::unique_ptr<Ui::ConfigureDialog> ui;
    HotkeyRegistry& registry;

    Core::System& system;
    std::unique_ptr<ConfigurationShared::Builder> builder;
    std::vector<ConfigurationShared::Tab*> tab_group;

    std::unique_ptr<ConfigureApplets> applets_tab;
    std::unique_ptr<ConfigureAudio> audio_tab;
    std::unique_ptr<ConfigureCpu> cpu_tab;
    std::unique_ptr<ConfigureDebugTab> debug_tab_tab;
    std::unique_ptr<ConfigureFilesystem> filesystem_tab;
    std::unique_ptr<ConfigureGeneral> general_tab;
    std::unique_ptr<ConfigureGraphicsAdvanced> graphics_advanced_tab;
    std::unique_ptr<ConfigureGraphicsExtensions> graphics_extensions_tab;
    std::unique_ptr<ConfigureUi> ui_tab;
    std::unique_ptr<ConfigureGraphics> graphics_tab;
    /// Graphics as most players want it (presets, picture, smoothness); applied after the rest.
    std::unique_ptr<class ConfigureGraphicsEveryday> graphics_everyday_tab;
    std::unique_ptr<ConfigureHotkeys> hotkeys_tab;
    std::unique_ptr<ConfigureInput> input_tab;
    std::unique_ptr<ConfigureNetwork> network_tab;
    std::unique_ptr<ConfigureProfileManager> profile_tab;
    std::unique_ptr<ConfigureSystem> system_tab;
    std::unique_ptr<ConfigureWeb> web_tab;
};
