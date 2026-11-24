// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>

#include "common/fs/fs.h"
#include "common/fs/fs_util.h"
#include "common/fs/path_util.h"
#include "common/settings.h"
#include "core/core.h"
#include "core/file_sys/card_image.h"
#include "core/file_sys/content_archive.h"
#include "core/file_sys/control_metadata.h"
#include "core/file_sys/fs_filesystem.h"
#include "core/file_sys/nca_metadata.h"
#include "core/file_sys/patch_manager.h"
#include "core/file_sys/program_metadata.h"
#include "core/file_sys/common_funcs.h"
#include "core/file_sys/registered_cache.h"
#include "core/file_sys/submission_package.h"
#include "core/loader/loader.h"
#include "core/loader/nup.h"
#include "core/loader/nx.h"

#include "qt_common/config/uisettings.h"
#include "qt_common/qt_common.h"

#include "qt_common/game_list/game_list_p.h"
#include "qt_common/game_list/game_pictures.h"

#include "qt_common/game_list/model.h"
#include "qt_common/game_list/discovery.h"
#include "qt_common/game_list/worker.h"
#include "qt_common/game_list/case_art.h"
#include "qt_common/game_list/platform_override.h"

namespace {

struct PendingRow {
    explicit PendingRow(QList<QStandardItem*> row) : items{std::move(row)} {}
    PendingRow(const PendingRow&) = delete;
    QList<QStandardItem*> items;
    ~PendingRow() {
        qDeleteAll(items);
    }
    QList<QStandardItem*> Release() {
        return std::exchange(items, {});
    }
};

QString GetGameListCachedObject(const std::string& filename, const std::string& ext,
                                const std::function<QString()>& generator) {
    if (!UISettings::values.cache_game_list || filename == "0000000000000000") {
        return generator();
    }

    const auto path =
        Common::FS::PathToUTF8String(Common::FS::GetEdenPath(Common::FS::EdenPath::CacheDir) /
                                     "game_list" / fmt::format("{}.{}", filename, ext));

    void(Common::FS::CreateParentDirs(path));

    if (!Common::FS::Exists(path)) {
        const auto str = generator();

        QFile file{QString::fromStdString(path)};
        if (file.open(QFile::WriteOnly)) {
            file.write(str.toUtf8());
        }

        return str;
    }

    QFile file{QString::fromStdString(path)};
    if (file.open(QFile::ReadOnly)) {
        return QString::fromUtf8(file.readAll());
    }

    return generator();
}

std::pair<std::vector<u8>, std::string> GetGameListCachedObject(
    const std::string& filename, const std::string& ext,
    const std::function<std::pair<std::vector<u8>, std::string>()>& generator) {
    if (!UISettings::values.cache_game_list || filename == "0000000000000000") {
        return generator();
    }

    const auto game_list_dir =
        Common::FS::GetEdenPath(Common::FS::EdenPath::CacheDir) / "game_list";
    const auto jpeg_name = fmt::format("{}.jpeg", filename);
    const auto app_name = fmt::format("{}.appname.txt", filename);

    const auto path1 = Common::FS::PathToUTF8String(game_list_dir / jpeg_name);
    const auto path2 = Common::FS::PathToUTF8String(game_list_dir / app_name);

    void(Common::FS::CreateParentDirs(path1));

    if (!Common::FS::Exists(path1) || !Common::FS::Exists(path2)) {
        const auto [icon, nacp] = generator();

        QFile file1{QString::fromStdString(path1)};
        if (!file1.open(QFile::WriteOnly)) {
            LOG_ERROR(Frontend, "Failed to open cache file.");
            return generator();
        }

        if (!file1.resize(icon.size())) {
            LOG_ERROR(Frontend, "Failed to resize cache file to necessary size.");
            return generator();
        }

        if (file1.write(reinterpret_cast<const char*>(icon.data()), icon.size()) !=
            s64(icon.size())) {
            LOG_ERROR(Frontend, "Failed to write data to cache file.");
            return generator();
        }

        QFile file2{QString::fromStdString(path2)};
        if (file2.open(QFile::WriteOnly)) {
            file2.write(nacp.data(), nacp.size());
        }

        return std::make_pair(icon, nacp);
    }

    QFile file1(QString::fromStdString(path1));
    QFile file2(QString::fromStdString(path2));

    if (!file1.open(QFile::ReadOnly)) {
        LOG_ERROR(Frontend, "Failed to open cache file for reading.");
        return generator();
    }

    if (!file2.open(QFile::ReadOnly)) {
        LOG_ERROR(Frontend, "Failed to open cache file for reading.");
        return generator();
    }

    std::vector<u8> vec(file1.size());
    if (file1.read(reinterpret_cast<char*>(vec.data()), vec.size()) !=
        static_cast<s64>(vec.size())) {
        return generator();
    }

    const auto data = file2.readAll();
    return std::make_pair(vec, data.toStdString());
}

void GetMetadataFromControlNCA(const FileSys::PatchManager& patch_manager, const FileSys::NCA& nca,
                               std::vector<u8>& icon, std::string& name) {
    std::tie(icon, name) = GetGameListCachedObject(
        fmt::format("{:016X}", patch_manager.GetTitleID()), {}, [&patch_manager, &nca] {
            const auto [nacp, icon_f] = patch_manager.ParseControlNCA(nca);
            return std::make_pair(icon_f->ReadAllBytes(), nacp->GetApplicationName());
        });
}

bool HasSupportedFileExtension(const std::string& file_name) {
    const QFileInfo file = QFileInfo(QString::fromStdString(file_name));
    return QtCommon::supported_file_extensions.contains(file.suffix(), Qt::CaseInsensitive);
}

bool IsExtractedNCAMain(const std::string& file_name) {
    return QFileInfo(QString::fromStdString(file_name)).fileName() == QStringLiteral("main");
}

QString FormatGameName(const std::string& physical_name) {
    const QString physical_name_as_qstring = QString::fromStdString(physical_name);
    const QFileInfo file_info(physical_name_as_qstring);

    if (IsExtractedNCAMain(physical_name)) {
        return file_info.dir().path();
    }

    return physical_name_as_qstring;
}

// Loose ExeFS dumps (<game>/exefs/main, <game>/base/exefs/main, updates in
// <game>/updates/<title id>/exefs/main) have no control data, so they are named after their
// game folder.
QString LooseExeFSName(const std::string& physical_name) {
    QDir dir = QFileInfo(QString::fromStdString(physical_name)).dir();
    if (dir.dirName().compare(QStringLiteral("exefs"), Qt::CaseInsensitive) != 0 || !dir.cdUp()) {
        return {};
    }

    // Storage levels sit between the game folder and its content directories.
    const auto is_storage_level = [](const QString& name) {
        if (name.compare(QStringLiteral("base"), Qt::CaseInsensitive) == 0 ||
            name.compare(QStringLiteral("update"), Qt::CaseInsensitive) == 0 ||
            name.compare(QStringLiteral("updates"), Qt::CaseInsensitive) == 0) {
            return true;
        }
        return !name.isEmpty() &&
               std::all_of(name.cbegin(), name.cend(), [](QChar c) {
                   return c.isDigit() || (c >= QLatin1Char('a') && c <= QLatin1Char('f')) ||
                          (c >= QLatin1Char('A') && c <= QLatin1Char('F'));
               });
    };

    bool is_update = false;
    while (is_storage_level(dir.dirName())) {
        if (dir.dirName().compare(QStringLiteral("update"), Qt::CaseInsensitive) == 0 ||
            dir.dirName().compare(QStringLiteral("updates"), Qt::CaseInsensitive) == 0) {
            is_update = true;
        }
        if (!dir.cdUp()) {
            return {};
        }
    }

    const QString name = dir.dirName();
    if (name.isEmpty()) {
        return {};
    }
    if (is_update && !name.contains(QStringLiteral("update"), Qt::CaseInsensitive)) {
        return QObject::tr("%1 (update)").arg(name);
    }
    return name;
}

QString FormatPatchNameVersions(const FileSys::PatchManager& patch_manager,
                                Loader::AppLoader& loader, bool updatable = true) {
    QString out;
    FileSys::VirtualFile update_raw;
    loader.ReadUpdateRaw(update_raw);
    for (const auto& patch : patch_manager.GetPatches(update_raw, &loader)) {
        const bool is_update = patch.type == FileSys::PatchType::Update;
        if (!updatable && is_update) {
            continue;
        }

        const QString type =
            QString::fromStdString(patch.enabled ? patch.name : "[D] " + patch.name);

        if (patch.version.empty()) {
            out.append(QStringLiteral("%1\n").arg(type));
        } else {
            auto ver = patch.version;

            // Display container name for packed updates
            if (is_update && ver == "PACKED") {
                ver = Loader::GetFileTypeString(loader.GetFileType());
            }

            out.append(QStringLiteral("%1 (%2)\n").arg(type, QString::fromStdString(ver)));
        }
    }

    out.chop(1);
    return out;
}

/// Whether the game's own program (without updates, add-ons or mods) is a Switch 2 one.
bool BaseIsSwitch2(const FileSys::VirtualFile& file, u64 program_id) {
    const QString cached = GetGameListCachedObject(
        fmt::format("{:016X}", program_id), "switch2.txt", [&file, program_id] {
            return Loader::IsSwitch2Program(file, program_id) ? QStringLiteral("1")
                                                              : QStringLiteral("0");
        });
    return cached == QStringLiteral("1");
}

bool NpdmIsSwitch2(const FileSys::VirtualFile& npdm) {
    FileSys::ProgramMetadata metadata;
    return npdm != nullptr && metadata.Load(npdm) == Loader::ResultStatus::Success &&
           metadata.IsSwitch2();
}

/// Why a Switch 1 game runs as its Nintendo Switch 2 Edition here, or empty when it doesn't:
/// - an enabled update whose program is a Switch 2 one;
/// - an enabled add-on the game's control data names as its Switch 2 upgrade pack (Switch 2
///   Editions of Switch 1 games ship the upgrade as add-on content, and the control data lists
///   which add-on switches it to Switch 2), or control data saying the game is a Switch 2 one;
/// - an enabled mod with a Switch 2 program, or "Switch 2" in its name.
/// Without one of these a game like Pokémon Legends: Z-A stays a Switch 1 game: Switch 1 badge,
/// case and pictures.
std::string Switch2EditionSource(const FileSys::VirtualFilesystem& vfs,
                                 const FileSys::ContentProvider& provider,
                                 const FileSys::PatchManager& patch, u64 program_id) {
    using namespace FileSys;
    const auto patches = patch.GetPatches();
    // Mods first: they cost no reading of game files.
    for (const Patch& p : patches) {
        if (p.type != PatchType::Mod || !p.enabled) {
            continue;
        }
        const QString name = QString::fromStdString(p.name).toLower();
        if (name.contains(QStringLiteral("switch 2")) || name.contains(QStringLiteral("switch2")) ||
            name.contains(QStringLiteral("ns2"))) {
            return fmt::format("mod \"{}\"", p.name);
        }
        if (!p.location.empty() && vfs != nullptr) {
            const auto npdm = vfs->OpenFile(p.location + "/exefs/main.npdm", OpenMode::Read);
            if (NpdmIsSwitch2(npdm)) {
                return fmt::format("mod \"{}\"", p.name);
            }
        }
    }
    const auto enabled = [&patches](PatchType type) {
        return std::ranges::find_if(patches, [type](const Patch& p) {
            return p.type == type && p.enabled;
        });
    };
    if (const auto update = enabled(PatchType::Update); update != patches.end()) {
        const auto nca = provider.GetEntry(GetUpdateTitleID(GetBaseTitleID(program_id)),
                                           ContentRecordType::Program);
        const auto exefs = nca != nullptr ? nca->GetExeFS() : nullptr;
        if (exefs != nullptr && NpdmIsSwitch2(exefs->GetFile("main.npdm"))) {
            return fmt::format("update {}", update->version);
        }
    }
    // The control data as the game runs it (its update's, when it has one).
    const auto control = patch.GetControlMetadata();
    if (control.first == nullptr) {
        return {};
    }
    const RawNACP& nacp = control.first->raw;
    if (nacp.apparent_platform == ApparentPlatform::Ounce) {
        return "control data";
    }
    const auto dlc = enabled(PatchType::DLC);
    const auto& condition = nacp.application_control_data_condition;
    if (dlc == patches.end() || condition.count == 0) {
        return {};
    }
    // The installed add-ons, as "1, 3, 4" (each one's index).
    std::vector<u32> installed;
    for (const QString& part : QString::fromStdString(dlc->version).split(QLatin1Char(','))) {
        bool ok = false;
        const u32 index = part.trimmed().toUInt(&ok);
        if (ok) {
            installed.push_back(index);
        }
    }
    const std::size_t count = (std::min)(static_cast<std::size_t>(condition.count),
                                         condition.data.size());
    for (std::size_t i = 0; i < count; ++i) {
        const u32 index = condition.data[i].aoc_index;
        if (index != 0 && std::ranges::find(installed, index) != installed.end()) {
            return fmt::format("upgrade pack (add-on {})", index);
        }
    }
    return {};
}

QList<QStandardItem*> MakeGameListEntry(const std::string& path, const std::string& name,
                                        const std::size_t size, const std::vector<u8>& icon,
                                        Loader::AppLoader& loader, u64 program_id,
                                        const PlayTime::PlayTimeManager& play_time_manager,
                                        const FileSys::PatchManager& patch, bool switch2,
                                        const std::string& edition_source) {
    auto const file_type = loader.GetFileType();
    auto const file_type_string = QString::fromStdString(Loader::GetFileTypeString(file_type));

    QString patch_versions = GetGameListCachedObject(
        fmt::format("{:016X}", patch.GetTitleID()), "pv.txt", [&patch, &loader] {
            return FormatPatchNameVersions(patch, loader, loader.IsRomFSUpdatable());
        });

    FileSys::NACP nacp{};
    // An NX tree snapshot is already the updated game. A NUP with a base lists its optional
    // update through GetPatches; an update-only NUP is itself the bootable program version.
    const bool nup_update_only =
        file_type == Loader::FileType::NUP &&
        static_cast<const Loader::AppLoader_NUP&>(loader).IsUpdateOnly();
    if (((file_type == Loader::FileType::NX && !loader.IsRomFSUpdatable()) || nup_update_only) &&
        loader.ReadControlData(nacp) == Loader::ResultStatus::Success) {
        const std::string version_string = nacp.GetVersionString();
        if (!version_string.empty()) {
            if (!patch_versions.isEmpty()) {
                patch_versions += QLatin1Char('\n');
            }
            patch_versions += QStringLiteral("Update (%1)").arg(
                QString::fromStdString(version_string));
        }
    }

    u64 play_time = play_time_manager.GetPlayTime(program_id);
    // A picture PhoenixEmu found or the player picked wins over the one inside the game.
    // One the icon pack gave the game's other edition is picked again (the game list's picture
    // search runs for it, as it has no picture).
    const std::vector<u8> saved_picture =
        QtCommon::GamePictures::PictureIsOtherEdition(program_id, switch2)
            ? std::vector<u8>{}
            : QtCommon::GamePictures::Load(program_id);
    auto* const path_item =
        new GameListItemPath(FormatGameName(path), saved_picture.empty() ? icon : saved_picture,
                             QString::fromStdString(name),
                             file_type_string, program_id, play_time, patch_versions);
    // The player's own banner (case art folder) wins over one found online.
    const QString title = QString::fromStdString(name);
    path_item->setData(switch2, GameListItemPath::Switch2Role);
    if (!edition_source.empty()) {
        path_item->setData(QString::fromStdString(edition_source),
                           GameListItemPath::Switch2EditionRole);
    }
    if (const QString shown = CaseArt::DisplayName(title, program_id); shown != title) {
        path_item->setData(shown, GameListItemPath::TitleRole);
    }
    // Switch 2 Editions get their Switch 2 case and banner, Switch 1 games the Switch 1 ones.
    std::vector<u8> banner = CaseArt::BannerBytes(title, program_id, switch2);
    if (banner.empty()) {
        banner = QtCommon::GamePictures::LoadBanner(program_id);
    }
    path_item->SetBanner(banner);
    if (const auto art = CaseArt::Load(title, program_id, switch2)) {
        QImage front = art->front;
        if (front.height() > 640) {
            front = front.scaledToHeight(640, Qt::SmoothTransformation);
        }
        path_item->setData(QPixmap::fromImage(front), GameListItemPath::CaseFrontRole);
        if (!art->spine.isNull()) {
            QImage spine = art->spine;
            if (spine.height() > 640) {
                spine = spine.scaledToHeight(640, Qt::SmoothTransformation);
            }
            path_item->setData(QPixmap::fromImage(spine), GameListItemPath::CaseSpineRole);
        }
        if (!art->back.isNull()) {
            QImage back = art->back;
            if (back.height() > 640) {
                back = back.scaledToHeight(640, Qt::SmoothTransformation);
            }
            path_item->setData(QPixmap::fromImage(back), GameListItemPath::CaseBackRole);
        }
    }
    return QList<QStandardItem*>{
        path_item,
        new GameListItem(file_type_string),
        new GameListItemSize(size),
        new GameListItemPlayTime(play_time),
        new GameListItem(patch_versions),
    };
}
} // Anonymous namespace

GameListWorker::GameListWorker(FileSys::VirtualFilesystem vfs_,
                               FileSys::ManualContentProvider* provider_,
                               QVector<UISettings::GameDir>& game_dirs_,
                               const PlayTime::PlayTimeManager& play_time_manager_,
                               Core::System& system_)
    : vfs{std::move(vfs_)}, provider{provider_}, game_dirs{game_dirs_},
      play_time_manager{play_time_manager_},
      system{system_} {
    // We want the game list to manage our lifetime.
    setAutoDelete(false);
}

GameListWorker::~GameListWorker() {
    this->disconnect();
    Stop();
}

std::pair<bool, std::string> GameListWorker::Platform(const FileSys::VirtualFile& file,
                                                     u64 program_id,
                                                     const FileSys::PatchManager& patch) const {
    switch (PlatformOverride::Get(program_id)) {
    case PlatformOverride::Choice::Switch1:
        return {false, {}};
    case PlatformOverride::Choice::Switch2:
        return {true, {}};
    case PlatformOverride::Choice::Automatic:
        break;
    }
    if (BaseIsSwitch2(file, program_id)) {
        return {true, {}};
    }
    std::string source =
        Switch2EditionSource(vfs, system.GetContentProvider(), patch, program_id);
    if (!source.empty()) {
        LOG_INFO(Frontend, "{:016X} shows as its Switch 2 Edition: {}", program_id, source);
    }
    const bool switch2 = !source.empty();
    return {switch2, std::move(source)};
}

void GameListWorker::Stop() {
    if (!stop_requested.exchange(true)) {
        processing_completed.Wait();
    }
}

void GameListWorker::ProcessEvents(GameListModel* model) {
    // An event may spin a nested event loop that replaces this worker, in which case stop.
    while (model->current_worker.get() == this) {
        std::function<void(GameListModel*)> func;
        {
            // Lock queue to protect concurrent modification.
            std::scoped_lock lk(lock);

            // If we can't pop a function, return.
            if (queued_events.empty()) {
                return;
            }

            // Pop a function.
            func = std::move(queued_events.back());
            queued_events.pop_back();
        }

        // Run the function.
        func(model);
    }
}

template <typename F>
void GameListWorker::RecordEvent(F&& func) {
    {
        // Lock queue to protect concurrent modification.
        std::scoped_lock lk(lock);

        // Add the function into the front of the queue.
        queued_events.emplace_front(std::move(func));
    }

    // Data now available.
    emit DataAvailable();
}

void GameListWorker::AddTitlesToGameList(GameListDir* parent_dir, GameListItemType parent_type) {
    using namespace FileSys;

    const auto& cache = system.GetContentProviderUnion();

    auto installed_games = cache.ListEntriesFilterOrigin(std::nullopt, TitleType::Application,
                                                         ContentRecordType::Program);

    if (parent_type == GameListItemType::SdmcDir) {
        installed_games = cache.ListEntriesFilterOrigin(
            ContentProviderUnionSlot::SDMC, TitleType::Application, ContentRecordType::Program);
    } else if (parent_type == GameListItemType::UserNandDir) {
        installed_games = cache.ListEntriesFilterOrigin(
            ContentProviderUnionSlot::UserNAND, TitleType::Application, ContentRecordType::Program);
    } else if (parent_type == GameListItemType::SysNandDir) {
        installed_games = cache.ListEntriesFilterOrigin(
            ContentProviderUnionSlot::SysNAND, TitleType::Application, ContentRecordType::Program);
    }

    for (const auto& [slot, game] : installed_games) {
        if (slot == ContentProviderUnionSlot::FrontendManual) {
            continue;
        }

        const auto file = cache.GetEntryUnparsed(game.title_id, game.type);
        std::unique_ptr<Loader::AppLoader> loader = Loader::GetLoader(system, file);
        if (!loader) {
            continue;
        }

        std::vector<u8> icon;
        std::string name;
        u64 program_id = 0;
        const auto result = loader->ReadProgramId(program_id);

        if (result != Loader::ResultStatus::Success) {
            continue;
        }

        const PatchManager patch{program_id, system.GetFileSystemController(),
                                 system.GetContentProvider()};
        LOG_INFO(Frontend, "PatchManager initiated for id {:X}", program_id);
        const auto control = cache.GetEntry(game.title_id, ContentRecordType::Control);
        if (control != nullptr) {
            GetMetadataFromControlNCA(patch, *control, icon, name);
        }

        const auto [switch2, edition] = Platform(file, program_id, patch);
        auto entry = MakeGameListEntry(file->GetFullPath(), name, file->GetSize(), icon, *loader,
                                       program_id, play_time_manager, patch, switch2, edition);
        auto pending = std::make_shared<PendingRow>(std::move(entry));
        RecordEvent([=](GameListModel* model) { model->AddEntry(pending->Release(), parent_dir); });
    }
}

void GameListWorker::ScanFileSystem(ScanTarget target, const std::vector<std::filesystem::path>& files,
                                    GameListDir* parent_dir) {
    const auto process_candidate = [this, target,
                                    parent_dir](const std::filesystem::path& path) {
        const auto physical_name = Common::FS::PathToUTF8String(path);
        const auto file = vfs->OpenFile(physical_name, FileSys::OpenMode::Read);
        if (!file) {
            return;
        }

        auto loader = Loader::GetLoader(system, file);
        if (!loader) {
            return;
        }

        const auto file_type = loader->GetFileType();
        if (file_type == Loader::FileType::Unknown || file_type == Loader::FileType::Error) {
            return;
        }

        if (target == ScanTarget::PopulateGameList &&
            (file_type == Loader::FileType::XCI || file_type == Loader::FileType::XCZ ||
             file_type == Loader::FileType::NSP || file_type == Loader::FileType::NSZ ||
             file_type == Loader::FileType::NUP || file_type == Loader::FileType::NX) &&
            !Loader::IsBootableGameContainer(file, file_type)) {
            return;
        }

        u64 program_id = 0;
        const auto res2 = loader->ReadProgramId(program_id);

        if (target == ScanTarget::FillManualContentProvider) {
            if (res2 == Loader::ResultStatus::Success && file_type == Loader::FileType::NCA) {
                provider->AddEntry(FileSys::TitleType::Application,
                                   FileSys::GetCRTypeFromNCAType(FileSys::NCA{file}.GetType()),
                                   program_id, file);
            } else if (file_type == Loader::FileType::NX) {
                // An archive brings its own updates and add-ons, whatever the setting.
                static_cast<Loader::AppLoader_NX&>(*loader).RegisterContent(*provider);
            } else if (Settings::values.ext_content_from_game_dirs.GetValue() &&
                       (file_type == Loader::FileType::XCI ||
                        file_type == Loader::FileType::XCZ ||
                        file_type == Loader::FileType::NSP ||
                        file_type == Loader::FileType::NSZ)) {
                void(provider->AddEntriesFromContainer(file));
            }
        } else {
            std::vector<u64> program_ids;
            loader->ReadProgramIds(program_ids);

            const auto addEntry = [this, physical_name, file,
                                   parent_dir](std::unique_ptr<Loader::AppLoader>& app_loader,
                                               const u64 id) {
                std::vector<u8> icon;
                [[maybe_unused]] const auto res1 = app_loader->ReadIcon(icon);

                std::string name = " ";
                const auto res3 = app_loader->ReadTitle(name);
                if ((res3 != Loader::ResultStatus::Success || name.empty() || name == " ") &&
                    IsExtractedNCAMain(physical_name)) {
                    if (const QString loose = LooseExeFSName(physical_name); !loose.isEmpty()) {
                        name = loose.toStdString();
                    }
                }
                if ((res3 != Loader::ResultStatus::Success || name.empty() || name == " ") &&
                    (app_loader->GetFileType() == Loader::FileType::NX ||
                     app_loader->GetFileType() == Loader::FileType::NUP)) {
                    // A dump without control data has no title: show the file name.
                    name = QFileInfo(QString::fromStdString(physical_name))
                               .completeBaseName()
                               .toStdString();
                }

                const FileSys::PatchManager patch{id, system.GetFileSystemController(),
                                                  system.GetContentProvider()};

                const bool loose_dump = IsExtractedNCAMain(physical_name);
                const u64 size = loose_dump ? 0 : Common::FS::GetSize(physical_name);

                const auto [switch2, edition] = Platform(file, id, patch);
                auto entry = MakeGameListEntry(physical_name, name, size, icon, *app_loader, id,
                                               play_time_manager, patch, switch2, edition);

                if (loose_dump) {
                    entry[GameListModel::COLUMN_SIZE]->setText(tr("Not scanned"));
                    entry[GameListModel::COLUMN_SIZE]->setToolTip(
                        tr("Folder size is not calculated during game discovery."));
                }
                auto pending = std::make_shared<PendingRow>(std::move(entry));
                RecordEvent([=](GameListModel* model) {
                    model->AddEntry(pending->Release(), parent_dir);
                });
            };

            if (res2 == Loader::ResultStatus::Success && program_ids.size() > 1 &&
                (file_type == Loader::FileType::XCI || file_type == Loader::FileType::XCZ ||
                 file_type == Loader::FileType::NSP || file_type == Loader::FileType::NSZ)) {
                for (const auto id : program_ids) {
                    // dravee suggested this, only viable way to
                    // not show sub-games in qlaunch for now.
                    if ((id & 0xFFF) != 0) {
                        continue;
                    }
                    loader = Loader::GetLoader(system, file, id);
                    if (!loader) {
                        continue;
                    }

                    addEntry(loader, id);
                }
            } else {
                addEntry(loader, program_id);
            }
        }
    };

    for (const auto& path : files) {
        if (stop_requested) {
            break;
        }
        // Loose executables cannot contribute NCA content to the manual provider.
        if (target == ScanTarget::FillManualContentProvider &&
            IsExtractedNCAMain(Common::FS::PathToUTF8String(path))) {
            continue;
        }
        process_candidate(path);
    }
}

void GameListWorker::run() {
    const auto started = std::chrono::steady_clock::now();
    watch_list.clear();
    provider->ClearAllEntries();

    const auto DirEntryReady = [&](GameListDir* game_list_dir) {
        auto pending = std::make_shared<std::unique_ptr<GameListDir>>(game_list_dir);
        RecordEvent([=](GameListModel* model) { model->AddDirEntry(pending->release()); });
    };

    for (UISettings::GameDir& game_dir : game_dirs) {
        if (stop_requested) {
            break;
        }

        GameListDir* game_list_dir;
        bool scan = false;

        if (game_dir.path == std::string("SDMC")) {
            game_list_dir = new GameListDir(game_dir, GameListItemType::SdmcDir);
        } else if (game_dir.path == std::string("UserNAND")) {
            game_list_dir = new GameListDir(game_dir, GameListItemType::UserNandDir);
        } else if (game_dir.path == std::string("SysNAND")) {
            game_list_dir = new GameListDir(game_dir, GameListItemType::SysNandDir);
        } else {
            const QString qpath = QString::fromStdString(game_dir.path);
            if (QDir(qpath).exists()) {
                watch_list.append(qpath);
            }

            game_list_dir = new GameListDir(game_dir);
            scan = true;
        }

        const auto parent_type = static_cast<GameListItemType>(game_list_dir->type());
        DirEntryReady(game_list_dir);
        if (scan) {
            const auto discovery = GameListDiscovery::Discover(
                std::filesystem::path(Common::FS::ToU8String(game_dir.path)), game_dir.deep_scan,
                [](const auto& path) {
                    const auto name = Common::FS::PathToUTF8String(path);
                    return HasSupportedFileExtension(name) || IsExtractedNCAMain(name);
                },
                [this] { return stop_requested.load(); });
            for (const auto& directory : discovery.directories) {
                watch_list.append(QString::fromStdString(Common::FS::PathToUTF8String(directory)));
            }
            LOG_INFO(Frontend,
                     "Discovery: candidates={} directories={} skipped_romfs={} errors={} cancelled={}",
                     discovery.files.size(), discovery.directories.size(), discovery.skipped_romfs,
                     discovery.errors, stop_requested.load());
            ScanFileSystem(ScanTarget::FillManualContentProvider, discovery.files, game_list_dir);
            ScanFileSystem(ScanTarget::PopulateGameList, discovery.files, game_list_dir);
        } else {
            AddTitlesToGameList(game_list_dir, parent_type);
        }
    }

    watch_list.removeDuplicates();
    LOG_INFO(Frontend, "Game discovery finished: elapsed_ms={} watched={} cancelled={}",
             std::chrono::duration_cast<std::chrono::milliseconds>(
                 std::chrono::steady_clock::now() - started).count(),
             watch_list.size(), stop_requested.load());
    RecordEvent([this](GameListModel* model) { model->DonePopulating(watch_list); });
    processing_completed.Set();
}
