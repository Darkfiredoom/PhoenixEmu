// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: 2016 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <memory>
#include "common/logging.h"
#include "common/settings.h"
#include "common/settings_enums.h"
#include "core/core.h"
#include "qt_common/config/uisettings.h"
#include "qt_common/util/vk.h"
#include "ui_configure.h"
#include "phoenix/util/fit_window.h"
#include "phoenix/configuration/configure_applets.h"
#include "phoenix/configuration/configure_audio.h"
#include "phoenix/configuration/configure_cpu.h"
#include "phoenix/configuration/configure_debug_tab.h"
#include "phoenix/configuration/configure_dialog.h"
#include "phoenix/configuration/configure_filesystem.h"
#include "phoenix/configuration/configure_general.h"
#include "phoenix/configuration/configure_graphics.h"
#include "phoenix/configuration/configure_graphics_advanced.h"
#include "phoenix/configuration/configure_graphics_everyday.h"
#include "phoenix/configuration/configure_graphics_extensions.h"
#include "phoenix/configuration/configure_hotkeys.h"
#include "phoenix/configuration/configure_input.h"
#include "phoenix/configuration/configure_input_player.h"
#include "phoenix/configuration/configure_network.h"
#include "phoenix/configuration/configure_profile_manager.h"
#include "phoenix/configuration/configure_system.h"
#include "phoenix/configuration/configure_ui.h"
#include "phoenix/configuration/configure_web.h"
#include "phoenix/configuration/settings_shell.h"
#include "phoenix/switch2_controllers_dialog.h"
#include "phoenix/hotkeys.h"

#include <QButtonGroup>
#include <QPushButton>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QTabBar>
#include <QTimer>
#include "hid_core/frontend/emulated_controller.h"
#include "hid_core/hid_core.h"
#include "input_common/drivers/switch2.h"
#include "input_common/main.h"
#include <QVBoxLayout>

ConfigureDialog::ConfigureDialog(QWidget* parent, HotkeyRegistry& registry_,
                                 InputCommon::InputSubsystem* input_subsystem,
                                 std::vector<VkDeviceInfo::Record>& vk_device_records,
                                 Core::System& system_, bool enable_web_config)
    : QDialog(parent), ui{std::make_unique<Ui::ConfigureDialog>()}, registry(registry_),
      system{system_},
      builder{std::make_unique<ConfigurationShared::Builder>(this, !system_.IsPoweredOn())},
      applets_tab{std::make_unique<ConfigureApplets>(system_, nullptr, *builder, this)},
      audio_tab{std::make_unique<ConfigureAudio>(system_, nullptr, *builder, this)},
      cpu_tab{std::make_unique<ConfigureCpu>(system_, nullptr, *builder, this)},
      debug_tab_tab{std::make_unique<ConfigureDebugTab>(system_, this)},
      filesystem_tab{std::make_unique<ConfigureFilesystem>(this)},
      general_tab{std::make_unique<ConfigureGeneral>(system_, nullptr, *builder, this)},
      graphics_advanced_tab{
          std::make_unique<ConfigureGraphicsAdvanced>(system_, nullptr, *builder, this)},
      graphics_extensions_tab{
          std::make_unique<ConfigureGraphicsExtensions>(system_, nullptr, *builder, this)},
      ui_tab{std::make_unique<ConfigureUi>(system_, this)},
      graphics_tab{std::make_unique<ConfigureGraphics>(
          system_, vk_device_records, [&]() { graphics_advanced_tab->ExposeComputeOption(); },
          [this](Settings::AspectRatio ratio, Settings::ResolutionSetup setup) {
              ui_tab->UpdateScreenshotInfo(ratio, setup);
          },
          nullptr, *builder, this)},
      graphics_everyday_tab{std::make_unique<ConfigureGraphicsEveryday>(this)},
      hotkeys_tab{std::make_unique<ConfigureHotkeys>(system_.HIDCore(), this)},
      input_tab{std::make_unique<ConfigureInput>(system_, this)},
      network_tab{std::make_unique<ConfigureNetwork>(system_, this)},
      profile_tab{std::make_unique<ConfigureProfileManager>(system_, this)},
      system_tab{std::make_unique<ConfigureSystem>(system_, nullptr, *builder, this)},
      web_tab{std::make_unique<ConfigureWeb>(this)} {
    Settings::SetConfiguringGlobal(true);

    ui->setupUi(this);

    ui->tabWidget->addTab(applets_tab.get(), tr("Applets"));
    ui->tabWidget->addTab(audio_tab.get(), tr("Audio"));
    ui->tabWidget->addTab(cpu_tab.get(), tr("CPU"));
    ui->tabWidget->addTab(debug_tab_tab.get(), tr("Debug"));
    ui->tabWidget->addTab(filesystem_tab.get(), tr("Filesystem"));
    ui->tabWidget->addTab(general_tab.get(), tr("General"));
    ui->tabWidget->addTab(graphics_everyday_tab.get(), tr("Everyday"));
    ui->tabWidget->addTab(graphics_tab.get(), tr("Graphics"));
    ui->tabWidget->addTab(graphics_advanced_tab.get(), tr("GraphicsAdvanced"));
    ui->tabWidget->addTab(graphics_extensions_tab.get(), tr("GraphicsExtra"));
    ui->tabWidget->addTab(hotkeys_tab.get(), tr("Hotkeys"));
    ui->tabWidget->addTab(input_tab.get(), tr("Controls"));
    ui->tabWidget->addTab(profile_tab.get(), tr("Profiles"));
    ui->tabWidget->addTab(network_tab.get(), tr("Network"));
    ui->tabWidget->addTab(system_tab.get(), tr("System"));
    ui->tabWidget->addTab(ui_tab.get(), tr("Game List"));
    ui->tabWidget->addTab(web_tab.get(), tr("Web"));

    web_tab->SetWebServiceConfigEnabled(enable_web_config);
    hotkeys_tab->Populate(registry);

    input_tab->Initialize(input_subsystem);
    switch2_input = input_subsystem;

    general_tab->SetResetCallback([&] { this->close(); });

    SetConfiguration();
    BuildShell();
    PopulateSelectionList();

    connect(ui->tabWidget, &QTabWidget::currentChanged, this, [this](int index) {
        if (index != -1) {
            debug_tab_tab->SetCurrentIndex(0);
        }
    });
    connect(ui_tab.get(), &ConfigureUi::LanguageChanged, this, &ConfigureDialog::OnLanguageChanged);
    connect(general_tab.get(), &ConfigureGeneral::ExternalContentDirsChanged, this,
            &ConfigureDialog::ExternalContentDirsChanged);
    connect(network_tab.get(), &ConfigureNetwork::BrowsePublicLobbyRequested, this, [this] {
        multiplayer_action_requested = MultiplayerAction::ViewLobby;
        accept();
    });
    connect(network_tab.get(), &ConfigureNetwork::CreateRoomRequested, this, [this] {
        multiplayer_action_requested = MultiplayerAction::CreateRoom;
        accept();
    });
    connect(network_tab.get(), &ConfigureNetwork::DirectConnectRequested, this, [this] {
        multiplayer_action_requested = MultiplayerAction::DirectConnect;
        accept();
    });
    connect(web_tab.get(), &ConfigureWeb::BrowsePublicLobbyRequested, this, [this] {
        multiplayer_action_requested = MultiplayerAction::ViewLobby;
        accept();
    });
    connect(ui->selectorList, &QListWidget::itemSelectionChanged, this,
            &ConfigureDialog::UpdateVisibleTabs);

    // One button for players who don't want to go through the tabs: picks the settings that fit
    // this PC's graphics card and memory (with a way back), instead of what is in this window.
    {
        QPushButton* const best = ui->buttonBox->addButton(
            tr("Find Best Settings for This PC..."), QDialogButtonBox::ResetRole);
        best->setToolTip(
            tr("Closes this window and offers the settings PhoenixEmu recommends for this "
               "graphics card and memory. Your own settings are kept: Help > Restore My "
               "Previous Settings brings them back."));
        best->setEnabled(!system.IsPoweredOn());
        if (system.IsPoweredOn()) {
            best->setToolTip(tr("Stop the game first."));
        }
        connect(best, &QAbstractButton::clicked, this, [this] {
            best_settings_requested = true;
            reject();
        });
    }

    if (system.IsPoweredOn()) {
        QPushButton* apply_button = ui->buttonBox->addButton(QDialogButtonBox::Apply);
        connect(apply_button, &QAbstractButton::clicked, this,
                &ConfigureDialog::HandleApplyButtonClicked);
    }

    // The pages scroll, so the window can be smaller than its longest page and its buttons stay
    // reachable on a small screen: it opens at the size it always had, or at what the screen has.
    adjustSize();
    // Room for the sidebar and a player's controls page side by side (FitWindow keeps it to
    // the screen).
    const QSize natural = size().expandedTo(QSize(1500, 880));
    FitWindow::ScrollWrap(ui->tabWidget);
    FitWindow::OpenAt(this, natural);
    ui->selectorList->setCurrentRow(0);

    // Selects the leftmost button on the bottom bar (Cancel as of writing)
    ui->buttonBox->setFocus();
}

ConfigureDialog::~ConfigureDialog() = default;

void ConfigureDialog::SetConfiguration() {}

void ConfigureDialog::ApplyConfiguration() {
    general_tab->ApplyConfiguration();
    ui_tab->ApplyConfiguration();
    system_tab->ApplyConfiguration();
    profile_tab->ApplyConfiguration();
    filesystem_tab->ApplyConfiguration();
    input_tab->ApplyConfiguration();
    hotkeys_tab->ApplyConfiguration(registry);
    cpu_tab->ApplyConfiguration();
    graphics_tab->ApplyConfiguration();
    graphics_advanced_tab->ApplyConfiguration();
    graphics_extensions_tab->ApplyConfiguration();
    // After the full Graphics tabs and System: it saves only what was changed on it.
    graphics_everyday_tab->ApplyConfiguration();
    audio_tab->ApplyConfiguration();
    debug_tab_tab->ApplyConfiguration();
    web_tab->ApplyConfiguration();
    network_tab->ApplyConfiguration();
    applets_tab->ApplyConfiguration();
    system.ApplySettings();
    Settings::LogSettings();
}

void ConfigureDialog::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        RetranslateUI();
    }

    QDialog::changeEvent(event);
}

void ConfigureDialog::RetranslateUI() {
    const int old_row = ui->selectorList->currentRow();
    const int old_index = ui->tabWidget->currentIndex();

    ui->retranslateUi(this);

    PopulateSelectionList();
    ui->selectorList->setCurrentRow(old_row);

    UpdateVisibleTabs();
    ui->tabWidget->setCurrentIndex(old_index);
}

void ConfigureDialog::HandleApplyButtonClicked() {
    UISettings::values.configuration_applied = true;
    ApplyConfiguration();
}

Q_DECLARE_METATYPE(QList<QWidget*>);

void ConfigureDialog::OpenPage(int page) {
    // `page` is a PageInfo index (4: Controls); its row depends on the mode and the search.
    for (int row = 0; row < ui->selectorList->count(); ++row) {
        if (ui->selectorList->item(row)->data(Qt::UserRole + 1).toInt() == page &&
            !ui->selectorList->item(row)->isHidden()) {
            ui->selectorList->setCurrentRow(row);
            return;
        }
    }
}

void ConfigureDialog::BuildShell() {
    // The page list moves into a sidebar with a title and a search box; a header names the page
    // above its tabs, with the Everyday / Advanced switch at its right.
    ui->horizontalLayout->removeWidget(ui->selectorList);
    ui->horizontalLayout->removeWidget(ui->tabWidget);
    const auto sidebar = SettingsShell::BuildSidebar(this, ui->selectorList, tr("Settings"),
                                                     tr("Search settings"));
    search_box = sidebar.search;
    ui->horizontalLayout->addWidget(sidebar.frame);
    auto* const content = new QWidget(this);
    content->setObjectName(QStringLiteral("SettingsContent"));
    auto* const column = new QVBoxLayout(content);
    column->setContentsMargins(18, 8, 8, 0);
    const auto header = SettingsShell::BuildHeader(content);
    page_title = header.title;
    page_subtitle = header.subtitle;
    // On the Controls page: pairing Switch 2 controllers over Bluetooth.
    switch2_button = new QPushButton(tr("Switch 2 controllers…"), content);
    switch2_button->setObjectName(QStringLiteral("SettingsHeaderButton"));
    switch2_button->setCursor(Qt::PointingHandCursor);
    switch2_button->hide();
    header.right->addWidget(switch2_button, 0, Qt::AlignTop);
    // On the Controls page: the battery of the controllers connected now.
    battery_label = new QLabel(content);
    battery_label->setObjectName(QStringLiteral("SettingsBattery"));
    battery_label->hide();
    header.right->insertWidget(0, battery_label, 0, Qt::AlignTop);
    auto* const battery_timer = new QTimer(this);
    battery_timer->setInterval(1000);
    connect(battery_timer, &QTimer::timeout, this, &ConfigureDialog::UpdateBattery);
    battery_timer->start();
    connect(switch2_button, &QPushButton::clicked, this, [this] {
        Switch2ControllersDialog dialog(switch2_input, this);
        dialog.exec();
    });
    QButtonGroup* const mode =
        SettingsShell::BuildModeSwitch(content, header.right,
                                       UISettings::values.settings_advanced.GetValue());
    column->addWidget(header.widget);
    column->addWidget(ui->tabWidget, 1);
    ui->horizontalLayout->addWidget(content, 1);
    ui->horizontalLayout->setSpacing(0);
    ui->tabWidget->setObjectName(QStringLiteral("SettingsTabs"));
    ui->tabWidget->tabBar()->setDrawBase(false);
    setObjectName(QStringLiteral("ConfigureDialog"));
    setMinimumSize(980, 680);
    connect(mode, &QButtonGroup::idClicked, this, [this](int id) {
        UISettings::values.settings_advanced = id == 1;
        const int page = ui->selectorList->currentItem() != nullptr
                             ? ui->selectorList->currentItem()->data(Qt::UserRole + 1).toInt()
                             : 0;
        PopulateSelectionList();
        FilterPages();
        OpenPage(page);
        if (ui->selectorList->currentRow() < 0) {
            ui->selectorList->setCurrentRow(0);
        }
    });
    connect(search_box, &QLineEdit::textChanged, this, [this] { FilterPages(); });
}

void ConfigureDialog::UpdateBattery() {
    if (battery_label == nullptr) {
        return;
    }
    const QListWidgetItem* const item = ui->selectorList->currentItem();
    const bool controls = item != nullptr && item->data(Qt::UserRole + 1).toInt() == 4;
    QStringList parts;
    // Switch 2 controllers report a percentage.
    if (switch2_input != nullptr) {
        if (const auto* const switch2 = switch2_input->GetSwitch2Controllers()) {
            for (const auto& status : switch2->GetStatus()) {
                if (status.battery_percent >= 0) {
                    parts << QStringLiteral("%1 %2%")
                                 .arg(QString::fromStdString(
                                     InputCommon::Switch2Controllers::KindName(status.kind)))
                                 .arg(status.battery_percent);
                }
            }
        }
    }
    // Other controllers: Player 1's, as a level.
    if (parts.isEmpty()) {
        const auto* const p1 =
            system.HIDCore().GetEmulatedController(Core::HID::NpadIdType::Player1);
        const auto battery = p1->GetBatteryValues();
        static const QStringList levels{QString{}, tr("empty"), tr("critical"), tr("low"),
                                        tr("medium"), tr("full"), tr("charging")};
        for (const auto& value : battery) {
            const auto level = static_cast<qsizetype>(value);
            if (level > 0 && level < levels.size()) {
                parts << tr("Player 1 %1").arg(levels[level]);
                break;
            }
        }
    }
    battery_label->setText(QStringLiteral("🔋  ") + parts.join(QStringLiteral("  ·  ")));
    battery_label->setVisible(controls && !parts.isEmpty());
}

void ConfigureDialog::PopulateSelectionList() {
    using SettingsShell::Icon;
    pages = {
        {tr("General"), tr("Language, updates and how PhoenixEmu starts"),
         {general_tab.get()}, {}, static_cast<int>(Icon::General), false},
        {tr("Library"), tr("Game folders, the game list and its pictures"),
         {ui_tab.get(), filesystem_tab.get()}, {}, static_cast<int>(Icon::Library), false},
        {tr("Graphics"), tr("How games look and how fast they run"),
         {graphics_everyday_tab.get()},
         {graphics_tab.get(), graphics_advanced_tab.get(), graphics_extensions_tab.get()},
         static_cast<int>(Icon::Graphics), false},
        {tr("Audio"), tr("Where the sound goes and how loud it is"),
         {audio_tab.get()}, {}, static_cast<int>(Icon::Audio), false},
        {tr("Controls"), tr("One controller per player; hotkeys work with keyboard and pad"),
         input_tab->GetSubTabs(), {}, static_cast<int>(Icon::Controls), false},
        {tr("System"), tr("The console your games think they run on"),
         {system_tab.get(), profile_tab.get()}, {applets_tab.get()},
         static_cast<int>(Icon::System), false},
        {tr("Multiplayer"), tr("Local wireless, LAN and online play"),
         {network_tab.get(), web_tab.get()}, {}, static_cast<int>(Icon::Multiplayer), false},
        {tr("Shortcuts"), tr("Keys and buttons for pausing, saving, full screen and more"),
         {hotkeys_tab.get()}, {}, static_cast<int>(Icon::Shortcuts), false},
        {tr("CPU"), tr("How exactly the console's processor is emulated"),
         {cpu_tab.get()}, {}, static_cast<int>(Icon::Cpu), true},
        {tr("Debug"), tr("Logging and tools for finding problems"),
         {debug_tab_tab.get()}, {}, static_cast<int>(Icon::Debug), true},
    };
    const bool advanced = UISettings::values.settings_advanced.GetValue();

    [[maybe_unused]] const QSignalBlocker blocker(ui->selectorList);

    ui->selectorList->clear();
    for (size_t i = 0; i < pages.size(); ++i) {
        const PageInfo& page = pages[i];
        if (page.advanced_only && !advanced) {
            continue;
        }
        QList<QWidget*> tabs = page.everyday;
        if (advanced) {
            tabs += page.advanced;
        }
        auto* const item = new QListWidgetItem(
            SettingsShell::MakeIcon(static_cast<SettingsShell::Icon>(page.icon)), page.name);
        item->setData(Qt::UserRole, QVariant::fromValue(tabs));
        item->setData(Qt::UserRole + 1, static_cast<int>(i));
        item->setSizeHint(QSize(0, 40));
        ui->selectorList->addItem(item);
    }
}

void ConfigureDialog::FilterPages() {
    const QString query = search_box != nullptr ? search_box->text().trimmed() : QString{};
    int first_shown = -1;
    for (int row = 0; row < ui->selectorList->count(); ++row) {
        QListWidgetItem* const item = ui->selectorList->item(row);
        const auto tabs = qvariant_cast<QList<QWidget*>>(item->data(Qt::UserRole));
        bool match = query.isEmpty() || item->text().contains(query, Qt::CaseInsensitive);
        for (const QWidget* tab : tabs) {
            match = match || SettingsShell::ContainsText(tab, query);
        }
        item->setHidden(!match);
        if (match && first_shown < 0) {
            first_shown = row;
        }
    }
    QListWidgetItem* const current = ui->selectorList->currentItem();
    if ((current == nullptr || current->isHidden()) && first_shown >= 0) {
        ui->selectorList->setCurrentRow(first_shown);
    }
    // On the page shown, open the first tab with a match.
    if (!query.isEmpty()) {
        for (int i = 0; i < ui->tabWidget->count(); ++i) {
            if (SettingsShell::ContainsText(ui->tabWidget->widget(i), query)) {
                ui->tabWidget->setCurrentIndex(i);
                break;
            }
        }
    }
}

void ConfigureDialog::OnLanguageChanged(const QString& locale) {
    emit LanguageChanged(locale);
    //  Reloading the game list is needed to force retranslation.
    UISettings::values.is_game_list_reload_pending = true;
    // first apply the configuration, and then restore the display
    ApplyConfiguration();
    RetranslateUI();
    SetConfiguration();
}

void ConfigureDialog::UpdateVisibleTabs() {
    const auto items = ui->selectorList->selectedItems();
    if (items.isEmpty()) {
        return;
    }

    [[maybe_unused]] const QSignalBlocker blocker(ui->tabWidget);

    ui->tabWidget->clear();

    const auto tabs = qvariant_cast<QList<QWidget*>>(items[0]->data(Qt::UserRole));

    for (auto* const tab : tabs) {
        LOG_DEBUG(Frontend, "{}", tab->accessibleName().toStdString());
        ui->tabWidget->addTab(tab, tab->accessibleName());
    }
    // One tab needs no tab bar.
    ui->tabWidget->tabBar()->setVisible(tabs.size() > 1);
    const int page = items[0]->data(Qt::UserRole + 1).toInt();
    if (page_title != nullptr && page >= 0 && page < static_cast<int>(pages.size())) {
        page_title->setText(pages[static_cast<size_t>(page)].name);
        if (switch2_button != nullptr) {
            switch2_button->setVisible(page == 4); // Controls
            UpdateBattery();
        }
        page_subtitle->setText(pages[static_cast<size_t>(page)].subtitle);
    }
    if (search_box != nullptr && !search_box->text().trimmed().isEmpty()) {
        for (int i = 0; i < ui->tabWidget->count(); ++i) {
            if (SettingsShell::ContainsText(ui->tabWidget->widget(i), search_box->text().trimmed())) {
                ui->tabWidget->setCurrentIndex(i);
                break;
            }
        }
    }
}
