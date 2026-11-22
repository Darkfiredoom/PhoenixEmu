// SPDX-FileCopyrightText: 2026 PhoenixEmu contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <filesystem>

#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>

#include <fmt/format.h>

#include "common/fs/fs.h"
#include "common/fs/path_util.h"
#include "common/settings.h"
#include "core/core.h"
#include "core/file_sys/control_metadata.h"
#include "core/file_sys/patch_manager.h"
#include "core/loader/loader.h"
#include "frontend_common/play_time_manager.h"
#include "phoenix/game_details_panel.h"
#include "phoenix/util/phoenix_theme.h"
#include "qt_common/game_list/game_list_p.h"
#include "qt_common/game_list/model.h"
#include "qt_common/qt_common.h"
#include "video_core/shader_warm/shader_warm_builder.h"

namespace {

constexpr int PanelWidth = 340;
constexpr int BannerHeight = 150;
constexpr int IconSize = 84;

QLabel* Heading(const QString& text) {
    auto* const label = new QLabel(text);
    label->setObjectName(QStringLiteral("CustomizeSectionTitle")); // the Customize panel's look
    return label;
}

QLabel* Chip(QWidget* parent) {
    auto* const label = new QLabel(parent);
    label->setObjectName(QStringLiteral("GameDetailsChip"));
    return label;
}

/// Bytes in the game's shader folder (its driver pipeline caches).
u64 FolderBytes(const std::filesystem::path& folder) {
    std::error_code error;
    u64 total = 0;
    for (auto it = std::filesystem::recursive_directory_iterator(folder, error);
         !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error)) {
        if (it->is_regular_file(error)) {
            total += it->file_size(error);
        }
    }
    return total;
}

QString Bytes(u64 bytes) {
    return QLocale().formattedDataSize(static_cast<qint64>(bytes), 1, QLocale::DataSizeTraditionalFormat);
}

} // namespace

GameDetailsPanel::GameDetailsPanel(Core::System& system_, const PlayTime::PlayTimeManager& play_time_,
                                   Actions actions_, QWidget* parent)
    : QFrame(parent), system{system_}, play_time{play_time_}, actions{std::move(actions_)} {
    setObjectName(QStringLiteral("CustomizePanel")); // the same side panel look
    setFixedWidth(PanelWidth);

    auto* const outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // The banner, with the close button on it.
    banner = new QLabel;
    banner->setFixedHeight(BannerHeight);
    banner->setScaledContents(false);
    outer->addWidget(banner);
    auto* const close = new QToolButton(banner);
    close->setText(QStringLiteral("✕"));
    close->setToolTip(tr("Hide the game details"));
    close->setAutoRaise(true);
    close->move(PanelWidth - 36, 8);
    connect(close, &QToolButton::clicked, this, &GameDetailsPanel::CloseRequested);

    auto* const scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* const body = new QWidget;
    body->setObjectName(QStringLiteral("CustomizePanelBody"));
    // The panel's width, whatever a long add-on name or path asks for (see CustomizePanel).
    body->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto* const layout = new QVBoxLayout(body);
    layout->setSizeConstraint(QLayout::SetNoConstraint);
    layout->setContentsMargins(16, 0, 16, 12);
    layout->setSpacing(8);
    scroll->setWidget(body);
    outer->addWidget(scroll, 1);

    // The icon overlapping the banner's bottom edge, the name and developer beside it.
    auto* const identity = new QHBoxLayout;
    icon = new QLabel;
    icon->setFixedSize(IconSize, IconSize);
    identity->addWidget(icon, 0, Qt::AlignTop);
    auto* const names = new QVBoxLayout;
    title = new QLabel;
    title->setObjectName(QStringLiteral("CustomizePanelTitle"));
    title->setWordWrap(true);
    developer = new QLabel;
    developer->setWordWrap(true);
    names->addWidget(title);
    names->addWidget(developer);
    names->addStretch(1);
    identity->addLayout(names, 1);
    layout->addLayout(identity);

    auto* const chips = new QHBoxLayout;
    played = Chip(body);
    size = Chip(body);
    chips->addWidget(played);
    chips->addWidget(size);
    chips->addStretch(1);
    layout->addLayout(chips);

    addons_box = new QWidget;
    addons_list = new QVBoxLayout(addons_box);
    addons_list->setContentsMargins(0, 0, 0, 0);
    addons_list->setSpacing(2);
    no_addons = new QLabel(tr("No updates or DLC"));
    no_addons->setEnabled(false);
    addons_list->addWidget(no_addons);
    layout->addWidget(Heading(tr("Updates & DLC")));
    layout->addWidget(addons_box);

    layout->addWidget(Heading(tr("Shaders")));
    pipeline_cache = new QLabel;
    precompiled = new QLabel;
    layout->addWidget(pipeline_cache);
    layout->addWidget(precompiled);
    precompile = new QPushButton(tr("Pre-compile Shaders"));
    connect(precompile, &QPushButton::clicked, this, [this] {
        if (game.isValid() && actions.precompile_shaders) {
            actions.precompile_shaders(game.data(GameListItemPath::ProgramIdRole).toULongLong(),
                                       game.data(GameListItemPath::FullPathRole).toString());
            Refresh();
        }
    });
    layout->addWidget(precompile, 0, Qt::AlignLeft);

    details_toggle = new QToolButton;
    details_toggle->setText(tr("Technical Details"));
    details_toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    details_toggle->setArrowType(Qt::RightArrow);
    details_toggle->setCheckable(true);
    details_toggle->setAutoRaise(true);
    layout->addWidget(details_toggle);
    details = new QWidget;
    auto* const details_layout = new QVBoxLayout(details);
    details_layout->setContentsMargins(4, 0, 0, 0);
    details_text = new QLabel;
    details_text->setWordWrap(true);
    details_text->setTextInteractionFlags(Qt::TextSelectableByMouse);
    details_layout->addWidget(details_text);
    details->setVisible(false);
    layout->addWidget(details);
    connect(details_toggle, &QToolButton::toggled, this, [this](bool open) {
        details->setVisible(open);
        details_toggle->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
    });
    layout->addStretch(1);

    // Play, Properties, Favorite and the game's menu.
    auto* const bar = new QHBoxLayout;
    bar->setContentsMargins(16, 8, 16, 14);
    play_button = new QPushButton(tr("▶  Play"));
    play_button->setObjectName(QStringLiteral("HomeHeroPlayButton"));
    play_button->setMinimumHeight(38);
    connect(play_button, &QPushButton::clicked, this, [this] {
        if (game.isValid() && actions.play) {
            actions.play(game.data(GameListItemPath::FullPathRole).toString(),
                         game.data(GameListItemPath::ProgramIdRole).toULongLong());
        }
    });
    bar->addWidget(play_button, 1);
    const auto tool = [bar](const QString& text, const QString& tip) {
        auto* const button = new QToolButton;
        button->setText(text);
        button->setToolTip(tip);
        button->setMinimumSize(38, 38);
        bar->addWidget(button);
        return button;
    };
    QToolButton* const properties = tool(QStringLiteral("⚙"), tr("Properties"));
    connect(properties, &QToolButton::clicked, this, [this] {
        if (game.isValid() && actions.properties) {
            actions.properties(game.data(GameListItemPath::FullPathRole).toString());
        }
    });
    favorite = tool(QStringLiteral("☆"), tr("Favorite"));
    connect(favorite, &QToolButton::clicked, this, [this] {
        if (game.isValid() && actions.toggle_favorite) {
            actions.toggle_favorite(game.data(GameListItemPath::ProgramIdRole).toULongLong());
            UpdateFavorite();
        }
    });
    QToolButton* const more = tool(QStringLiteral("⋯"), tr("More"));
    connect(more, &QToolButton::clicked, this, [this, more] {
        if (game.isValid() && actions.more) {
            actions.more(game, more->mapToGlobal(QPoint(0, more->height())));
        }
    });
    outer->addLayout(bar);

    settle.setSingleShot(true);
    settle.setInterval(200);
    connect(&settle, &QTimer::timeout, this, &GameDetailsPanel::Load);
}

void GameDetailsPanel::SetGame(const QModelIndex& index) {
    const QModelIndex first = index.isValid() ? index.sibling(index.row(), 0) : QModelIndex{};
    if (first.isValid() &&
        first.data(GameListItem::TypeRole).value<GameListItemType>() != GameListItemType::Game) {
        return; // a folder row in the list view: keep the last game
    }
    if (QPersistentModelIndex{first} == game && game.isValid()) {
        return;
    }
    game = first;
    // The parts already in the list show at once; the files are read once the selection settles.
    const bool valid = game.isValid();
    title->setText(valid ? game.data(GameListItemPath::TitleRole).toString() : tr("No game picked"));
    developer->clear();
    ShowPictures();
    // The game's picture is often found after the list is filled: show it when it arrives.
    if (valid && game.model() != watched_model) {
        disconnect(picture_watch);
        watched_model = game.model();
        picture_watch = connect(watched_model, &QAbstractItemModel::dataChanged, this,
                                [this](const QModelIndex& top_left, const QModelIndex& bottom_right) {
                                    if (game.isValid() && game.parent() == top_left.parent() &&
                                        game.row() >= top_left.row() &&
                                        game.row() <= bottom_right.row()) {
                                        ShowPictures();
                                    }
                                });
    }
    play_button->setEnabled(valid);
    UpdateFavorite();
    settle.start();
}

void GameDetailsPanel::ShowPictures() {
    const bool valid = game.isValid();
    // The game's banner, else its picture.
    banner_source = valid ? game.data(GameListItemPath::BannerRole).value<QPixmap>() : QPixmap{};
    if (banner_source.isNull() && valid) {
        banner_source = game.data(GameListItemPath::LargePictureRole).value<QPixmap>();
    }
    if (banner_source.isNull() && valid) {
        banner_source = game.data(Qt::DecorationRole).value<QPixmap>();
    }
    PaintBanner();
    const QPixmap small = valid ? game.data(Qt::DecorationRole).value<QPixmap>() : QPixmap{};
    icon->setPixmap(small.isNull() ? QPixmap{}
                                   : small.scaled(IconSize, IconSize, Qt::KeepAspectRatio,
                                                  Qt::SmoothTransformation));
}

void GameDetailsPanel::Refresh() {
    settle.start();
}

void GameDetailsPanel::showEvent(QShowEvent* event) {
    QFrame::showEvent(event);
    settle.start();
}

void GameDetailsPanel::resizeEvent(QResizeEvent* event) {
    QFrame::resizeEvent(event);
    PaintBanner();
}

void GameDetailsPanel::PaintBanner() {
    QPixmap canvas(PanelWidth, BannerHeight);
    canvas.fill(PhoenixTheme::Recolor(QColor(0x1C, 0x18, 0x11)));
    QPainter p(&canvas);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    if (!banner_source.isNull()) {
        const QPixmap scaled = banner_source.scaled(canvas.size(), Qt::KeepAspectRatioByExpanding,
                                                    Qt::SmoothTransformation);
        p.drawPixmap(0, 0, scaled, (scaled.width() - canvas.width()) / 2,
                     (scaled.height() - canvas.height()) / 2, canvas.width(), canvas.height());
    }
    // Faded into the panel at the bottom, so the name below reads cleanly.
    QLinearGradient fade{0, BannerHeight * 0.4, 0, BannerHeight};
    fade.setColorAt(0, QColor(0, 0, 0, 0));
    fade.setColorAt(1, PhoenixTheme::Recolor(QColor(0x1C, 0x18, 0x11)));
    p.fillRect(canvas.rect(), fade);
    p.end();
    banner->setPixmap(canvas);
}

void GameDetailsPanel::UpdateFavorite() {
    const bool on = game.isValid() && actions.is_favorite &&
                    actions.is_favorite(game.data(GameListItemPath::ProgramIdRole).toULongLong());
    favorite->setText(on ? QStringLiteral("★") : QStringLiteral("☆"));
    favorite->setToolTip(on ? tr("Remove from Favorites") : tr("Add to Favorites"));
}

void GameDetailsPanel::Load() {
    // The list of add-ons is made again from the game's files.
    for (auto& [box, addon] : addons) {
        delete box;
    }
    addons.clear();
    ShowPictures();
    if (!game.isValid() || !isVisible()) {
        played->clear();
        size->clear();
        return;
    }
    const u64 program_id = game.data(GameListItemPath::ProgramIdRole).toULongLong();
    const QString path = game.data(GameListItemPath::FullPathRole).toString();

    const u64 seconds = play_time.GetPlayTime(program_id);
    played->setText(seconds == 0 ? tr("Never played")
                                 : QString::fromStdString(
                                       PlayTime::PlayTimeManager::GetReadablePlayTime(seconds)));
    const u64 bytes = game.sibling(game.row(), GameListModel::COLUMN_SIZE)
                          .data(GameListItemSize::SizeRole)
                          .toULongLong();
    size->setText(Bytes(bytes));
    size->setVisible(bytes != 0);

    // Developer and add-ons from the game's own files.
    const auto file = QtCommon::vfs->OpenFile(path.toStdString(), FileSys::OpenMode::Read);
    const auto loader = file ? Loader::GetLoader(system, file, program_id) : nullptr;
    QString version;
    if (loader) {
        FileSys::NACP control;
        if (loader->ReadControlData(control) == Loader::ResultStatus::Success) {
            developer->setText(QString::fromStdString(control.GetDeveloperName()));
            version = QString::fromStdString(control.GetVersionString());
        }
        FileSys::VirtualFile update_raw;
        loader->ReadUpdateRaw(update_raw);
        const FileSys::PatchManager patches{program_id, system.GetFileSystemController(),
                                            system.GetContentProvider()};
        const auto& disabled = Settings::values.disabled_addons[program_id];
        bool update_on = false;
        for (const FileSys::Patch& patch : patches.GetPatches(update_raw, loader.get())) {
            if (patch.type == FileSys::PatchType::Mod) {
                continue; // Properties > Add-Ons has the mods
            }
            Addon addon;
            addon.name = QString::fromStdString(patch.name);
            addon.version = QString::fromStdString(patch.version);
            addon.update = patch.type == FileSys::PatchType::Update;
            // The same keys as Properties > Add-Ons.
            const bool external_update = addon.update &&
                                         patch.source == FileSys::PatchSource::External &&
                                         patch.numeric_version != 0;
            addon.key = external_update ? fmt::format("Update@{}", patch.numeric_version)
                                        : patch.name;
            addon.enabled = std::find(disabled.begin(), disabled.end(), addon.key) == disabled.end();
            if (addon.update && addon.enabled) {
                // One update at a time, the first enabled one, as the Add-Ons page does.
                addon.enabled = !update_on;
                update_on = true;
            }
            auto* const box = new QCheckBox(addon.version.isEmpty()
                                                ? addon.name
                                                : tr("%1   %2").arg(addon.name, addon.version));
            box->setChecked(addon.enabled);
            connect(box, &QCheckBox::toggled, this, [this, box](bool on) {
                const auto it = std::find_if(addons.begin(), addons.end(),
                                             [box](const auto& entry) { return entry.first == box; });
                if (it == addons.end()) {
                    return;
                }
                if (on && it->second.update) {
                    for (auto& [other_box, other] : addons) {
                        if (other_box != box && other.update && other_box->isChecked()) {
                            const QSignalBlocker blocker{other_box};
                            other_box->setChecked(false);
                        }
                    }
                }
                ApplyAddons();
            });
            addons_list->addWidget(box);
            addons.emplace_back(box, std::move(addon));
        }
    }
    no_addons->setVisible(addons.empty());

    // Shader caches.
    const auto shader_dir = Common::FS::GetEdenPath(Common::FS::EdenPath::ShaderDir) /
                            fmt::format("{:016x}", program_id);
    const u64 cache_bytes = FolderBytes(shader_dir);
    pipeline_cache->setText(tr("Pipeline cache:  %1")
                                .arg(cache_bytes == 0 ? tr("None yet") : Bytes(cache_bytes)));
    std::error_code error;
    const bool warm = program_id != 0 &&
                      std::filesystem::exists(VideoCommon::ShaderWarm::ComputeWarmPath(program_id), error);
    precompiled->setText(tr("Pre-compiled:  %1").arg(warm ? tr("Yes") : tr("No")));
    precompile->setText(warm ? tr("Pre-compile Again") : tr("Pre-compile Shaders"));

    const bool switch2 = game.data(GameListItemPath::Switch2Role).toBool();
    details_text->setText(
        tr("Program ID: %1\nPlatform: %2\nFormat: %3\nVersion: %4\nFile: %5")
            .arg(QStringLiteral("%1").arg(program_id, 16, 16, QLatin1Char('0')).toUpper(),
                 switch2 ? tr("Nintendo Switch 2") : tr("Nintendo Switch"),
                 game.data(GameListItemPath::FileTypeRole).toString(),
                 version.isEmpty() ? tr("unknown") : version, path));
}

void GameDetailsPanel::ApplyAddons() {
    if (!game.isValid()) {
        return;
    }
    const u64 program_id = game.data(GameListItemPath::ProgramIdRole).toULongLong();
    // Keep what this panel doesn't show (mods), replace the updates' and DLC's entries.
    std::vector<std::string> disabled = Settings::values.disabled_addons[program_id];
    for (const auto& [box, addon] : addons) {
        if (addon.key.empty()) {
            continue;
        }
        std::erase(disabled, addon.key);
        if (!box->isChecked()) {
            disabled.push_back(addon.key);
        }
    }
    Settings::values.disabled_addons[program_id] = disabled;
    // The list's add-on column is cached per game: drop it so it shows the change.
    Common::FS::RemoveFile(Common::FS::GetEdenPath(Common::FS::EdenPath::CacheDir) / "game_list" /
                           fmt::format("{:016X}.pv.txt", program_id));
    if (actions.addons_changed) {
        actions.addons_changed();
    }
}
