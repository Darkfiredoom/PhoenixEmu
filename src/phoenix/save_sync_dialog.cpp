// SPDX-FileCopyrightText: Copyright 2026 PhoenixEmu Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "save_sync_dialog.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <QCheckBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

#include "common/fs/path_util.h"
#include "common/logging.h"
#include "qt_common/config/uisettings.h"

namespace {

std::filesystem::path GetLocalSaveDir() {
    return Common::FS::GetEdenPath(Common::FS::EdenPath::NANDDir) / "user" / "save";
}

u64 CalculateDirSize(const std::filesystem::path& path) {
    u64 total_size = 0;
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return 0;
    }
    for (const auto& entry : std::filesystem::recursive_directory_iterator(path, ec)) {
        if (entry.is_regular_file(ec)) {
            total_size += entry.file_size(ec);
        }
    }
    return total_size;
}

QString FormatSize(u64 bytes) {
    if (bytes < 1024) {
        return QStringLiteral("%1 B").arg(bytes);
    }
    if (bytes < 1024 * 1024) {
        return QStringLiteral("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    }
    return QStringLiteral("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 2);
}

} // namespace

SaveSyncDialog::SaveSyncDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Cloud & LAN Save Sync"));
    setMinimumWidth(540);

    auto* main_layout = new QVBoxLayout(this);

    // Group 1: Local Saves Status
    auto* local_group = new QGroupBox(tr("Local PhoenixEmu Saves"), this);
    auto* local_layout = new QVBoxLayout(local_group);

    const auto local_path = GetLocalSaveDir();
    auto* local_path_label = new QLabel(QString::fromStdString(Common::FS::PathToUTF8String(local_path)), this);
    local_path_label->setStyleSheet(QStringLiteral("color: #aaa; font-size: 11px;"));
    local_path_label->setWordWrap(true);
    local_layout->addWidget(local_path_label);

    local_status_label = new QLabel(this);
    local_layout->addWidget(local_status_label);

    auto* open_local_btn = new QPushButton(tr("Open Local Saves Folder"), this);
    connect(open_local_btn, &QPushButton::clicked, this, &SaveSyncDialog::OnOpenLocal);
    local_layout->addWidget(open_local_btn);

    main_layout->addWidget(local_group);

    // Group 2: Remote / Cloud Sync Target
    auto* target_group = new QGroupBox(tr("Sync / Cloud Destination (NAS, OneDrive, Google Drive, etc.)"), this);
    auto* target_layout = new QVBoxLayout(target_group);

    auto* path_select_layout = new QHBoxLayout();
    target_path_edit = new QLineEdit(this);
    target_path_edit->setPlaceholderText(tr("Select folder (e.g. D:/OneDrive/PhoenixEmuSaves or \\\\NAS\\Saves)"));
    target_path_edit->setText(QString::fromStdString(UISettings::values.save_sync_folder.GetValue()));
    path_select_layout->addWidget(target_path_edit);

    auto* browse_btn = new QPushButton(tr("Browse..."), this);
    connect(browse_btn, &QPushButton::clicked, this, &SaveSyncDialog::OnBrowseTarget);
    path_select_layout->addWidget(browse_btn);
    target_layout->addLayout(path_select_layout);

    target_status_label = new QLabel(this);
    target_layout->addWidget(target_status_label);

    auto* open_target_btn = new QPushButton(tr("Open Sync Destination Folder"), this);
    connect(open_target_btn, &QPushButton::clicked, this, &SaveSyncDialog::OnOpenTarget);
    target_layout->addWidget(open_target_btn);

    auto_sync_check = new QCheckBox(tr("Automatically backup saves to sync folder whenever a game exits"), this);
    auto_sync_check->setChecked(UISettings::values.auto_save_sync.GetValue());
    connect(auto_sync_check, &QCheckBox::toggled, this, &SaveSyncDialog::OnAutoSyncToggled);
    target_layout->addWidget(auto_sync_check);

    main_layout->addWidget(target_group);

    // Actions
    auto* actions_layout = new QHBoxLayout();
    backup_button = new QPushButton(tr("Backup Now to Sync Folder"), this);
    backup_button->setStyleSheet(QStringLiteral("font-weight: bold; padding: 8px;"));
    connect(backup_button, &QPushButton::clicked, this, &SaveSyncDialog::OnBackupNow);
    actions_layout->addWidget(backup_button);

    restore_button = new QPushButton(tr("Restore from Sync Folder..."), this);
    restore_button->setStyleSheet(QStringLiteral("padding: 8px;"));
    connect(restore_button, &QPushButton::clicked, this, &SaveSyncDialog::OnRestore);
    actions_layout->addWidget(restore_button);

    main_layout->addLayout(actions_layout);

    auto* close_btn = new QPushButton(tr("Close"), this);
    connect(close_btn, &QPushButton::clicked, this, &QDialog::accept);
    main_layout->addWidget(close_btn);

    RefreshStatus();
}

SaveSyncDialog::~SaveSyncDialog() = default;

void SaveSyncDialog::RefreshStatus() {
    const auto local_dir = GetLocalSaveDir();
    std::error_code ec;
    if (std::filesystem::exists(local_dir, ec)) {
        u64 size = CalculateDirSize(local_dir);
        local_status_label->setText(tr("Status: Ready • Total Save Data: %1").arg(FormatSize(size)));
    } else {
        local_status_label->setText(tr("Status: No local save directory found yet."));
    }

    const auto target_str = target_path_edit->text().toStdString();
    if (!target_str.empty()) {
        const std::filesystem::path target_path(target_str);
        if (std::filesystem::exists(target_path, ec)) {
            const auto target_save_dir = target_path / "save";
            if (std::filesystem::exists(target_save_dir, ec)) {
                u64 size = CalculateDirSize(target_save_dir);
                target_status_label->setText(tr("Destination: Connected • Remote Save Data: %1").arg(FormatSize(size)));
            } else {
                target_status_label->setText(tr("Destination: Connected • No existing backups in this folder yet."));
            }
        } else {
            target_status_label->setText(tr("Destination folder does not exist or is currently unreachable."));
        }
    } else {
        target_status_label->setText(tr("No destination folder selected."));
    }
}

void SaveSyncDialog::OnBrowseTarget() {
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Select Sync Destination Folder"),
        target_path_edit->text().isEmpty() ? QStringLiteral("C:/") : target_path_edit->text());
    if (!dir.isEmpty()) {
        target_path_edit->setText(dir);
        UISettings::values.save_sync_folder = dir.toStdString();
        RefreshStatus();
    }
}

void SaveSyncDialog::OnAutoSyncToggled(bool checked) {
    UISettings::values.auto_save_sync = checked;
}

void SaveSyncDialog::OnOpenLocal() {
    const auto local_path = GetLocalSaveDir();
    std::error_code ec;
    std::filesystem::create_directories(local_path, ec);
    QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdString(Common::FS::PathToUTF8String(local_path))));
}

void SaveSyncDialog::OnOpenTarget() {
    const QString target = target_path_edit->text();
    if (!target.isEmpty()) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(target));
    }
}

void SaveSyncDialog::OnBackupNow() {
    const auto target_str = target_path_edit->text().toStdString();
    if (target_str.empty()) {
        QMessageBox::warning(this, tr("Save Sync"), tr("Please select a target sync directory first."));
        return;
    }

    const auto local_dir = GetLocalSaveDir();
    std::error_code ec;
    if (!std::filesystem::exists(local_dir, ec)) {
        QMessageBox::information(this, tr("Save Sync"), tr("No local save directory to backup."));
        return;
    }

    const std::filesystem::path target_dir(target_str);
    const auto dest_dir = target_dir / "save";
    std::filesystem::create_directories(dest_dir, ec);

    std::filesystem::copy(local_dir, dest_dir,
                          std::filesystem::copy_options::recursive |
                              std::filesystem::copy_options::overwrite_existing,
                          ec);

    if (ec) {
        QMessageBox::critical(this, tr("Save Sync"),
                              tr("Failed to copy save data: %1").arg(QString::fromStdString(ec.message())));
        return;
    }

    // Write metadata note
    const auto note_file = target_dir / "last_synced.txt";
    std::ofstream out(note_file);
    if (out.is_open()) {
        out << "PhoenixEmu Save Sync: "
            << QDateTime::currentDateTime().toString(Qt::ISODate).toStdString() << "\n";
    }

    QMessageBox::information(this, tr("Save Sync"),
                             tr("Successfully backed up all save files to %1").arg(QString::fromStdString(Common::FS::PathToUTF8String(dest_dir))));
    RefreshStatus();
}

void SaveSyncDialog::OnRestore() {
    const auto target_str = target_path_edit->text().toStdString();
    if (target_str.empty()) {
        QMessageBox::warning(this, tr("Save Restore"), tr("Please select a target sync directory first."));
        return;
    }

    const std::filesystem::path target_dir(target_str);
    const auto source_dir = target_dir / "save";
    std::error_code ec;
    if (!std::filesystem::exists(source_dir, ec)) {
        QMessageBox::warning(this, tr("Save Restore"), tr("No 'save' folder found in the selected destination directory."));
        return;
    }

    if (QMessageBox::question(
            this, tr("Restore Saves"),
            tr("This will replace current local saves with the backup from %1.\n\n"
               "A safety backup of your current local saves will be saved as 'save.pre_restore'.\n\n"
               "Do you want to proceed?")
                .arg(QString::fromStdString(Common::FS::PathToUTF8String(source_dir))),
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) {
        return;
    }

    const auto local_dir = GetLocalSaveDir();
    if (std::filesystem::exists(local_dir, ec)) {
        const auto safety_dir = local_dir.parent_path() / "save.pre_restore";
        std::filesystem::remove_all(safety_dir, ec);
        std::filesystem::copy(local_dir, safety_dir,
                              std::filesystem::copy_options::recursive |
                                  std::filesystem::copy_options::overwrite_existing,
                              ec);
    }

    std::filesystem::create_directories(local_dir, ec);
    std::filesystem::copy(source_dir, local_dir,
                          std::filesystem::copy_options::recursive |
                              std::filesystem::copy_options::overwrite_existing,
                          ec);

    if (ec) {
        QMessageBox::critical(this, tr("Save Restore"),
                              tr("Failed to restore save data: %1").arg(QString::fromStdString(ec.message())));
        return;
    }

    QMessageBox::information(this, tr("Save Restore"), tr("Saves successfully restored!"));
    RefreshStatus();
}

void SaveSyncDialog::PerformBackgroundSync() {
    const auto target_str = UISettings::values.save_sync_folder.GetValue();
    if (target_str.empty() || !UISettings::values.auto_save_sync.GetValue()) {
        return;
    }

    const auto local_dir = GetLocalSaveDir();
    std::error_code ec;
    if (!std::filesystem::exists(local_dir, ec)) {
        return;
    }

    const std::filesystem::path target_dir(target_str);
    const auto dest_dir = target_dir / "save";
    std::filesystem::create_directories(dest_dir, ec);
    if (ec) {
        LOG_WARNING(Frontend, "Auto Save Sync failed to create destination dir: {}", ec.message());
        return;
    }

    std::filesystem::copy(local_dir, dest_dir,
                          std::filesystem::copy_options::recursive |
                              std::filesystem::copy_options::overwrite_existing,
                          ec);
    if (!ec) {
        LOG_INFO(Frontend, "Auto Save Sync successfully backed up saves to {}", target_str);
    } else {
        LOG_WARNING(Frontend, "Auto Save Sync failed: {}", ec.message());
    }
}
