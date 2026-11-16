// SPDX-FileCopyrightText: 2026 PhoenixEmu contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "phoenix/album_dialog.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>

#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QUrl>
#include <QVBoxLayout>

#include "common/fs/fs_paths.h"
#include "common/fs/path_util.h"
#include "phoenix/util/phoenix_theme.h"

namespace {

QString FormatBytes(u64 bytes) {
    if (bytes < 1024) {
        return QStringLiteral("%1 B").arg(bytes);
    }
    if (bytes < 1024 * 1024) {
        return QStringLiteral("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    }
    return QStringLiteral("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 2);
}

} // namespace

AlbumDialog::AlbumDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Switch Album - Media Gallery"));
    resize(960, 620);
    setStyleSheet(QStringLiteral(
        "QDialog { background-color: #171513; color: #F5EFEB; } "
        "QLabel { color: #E8D9B5; } "
        "QListWidget { background-color: #201C18; border: 1px solid rgba(255,255,255,30); "
        "border-radius: 8px; color: #FFF; padding: 6px; } "
        "QListWidget::item { padding: 6px; border-radius: 6px; } "
        "QListWidget::item:selected { background-color: rgba(255,82,48,160); } "
        "QPushButton { background-color: rgba(255,255,255,25); border: 1px solid rgba(255,255,255,40); "
        "border-radius: 6px; color: #FFF; padding: 6px 14px; font-weight: bold; } "
        "QPushButton:hover { background-color: rgba(255,82,48,150); } "
        "QPushButton:disabled { color: #666; background-color: rgba(255,255,255,10); }"));

    auto* const root_layout = new QVBoxLayout(this);
    root_layout->setContentsMargins(16, 16, 16, 16);
    root_layout->setSpacing(12);

    // Top Header
    auto* const header_layout = new QHBoxLayout();
    auto* const title_label = new QLabel(tr("📸 PHOENIX MEDIA ALBUM"), this);
    QFont f = title_label->font();
    f.setBold(true);
    f.setPixelSize(15);
    title_label->setFont(f);
    title_label->setStyleSheet(QStringLiteral("color: #FF5230; font-weight: bold; letter-spacing: 1px;"));

    m_status_label = new QLabel(this);
    m_status_label->setStyleSheet(QStringLiteral("color: #AAA; font-size: 12px;"));

    header_layout->addWidget(title_label);
    header_layout->addSpacing(16);
    header_layout->addWidget(m_status_label);
    header_layout->addStretch();
    root_layout->addLayout(header_layout);

    // Main Splitter: Left Thumbnail List, Right High-Res Preview
    auto* const splitter = new QSplitter(Qt::Horizontal, this);

    m_list_widget = new QListWidget(splitter);
    m_list_widget->setIconSize(QSize(128, 72));
    m_list_widget->setGridSize(QSize(140, 100));
    m_list_widget->setViewMode(QListView::IconMode);
    m_list_widget->setResizeMode(QListView::Adjust);
    m_list_widget->setMovement(QListView::Static);
    m_list_widget->setWordWrap(true);
    connect(m_list_widget, &QListWidget::currentRowChanged, this, &AlbumDialog::SelectItem);

    auto* const right_pane = new QWidget(splitter);
    auto* const right_layout = new QVBoxLayout(right_pane);
    right_layout->setContentsMargins(8, 0, 0, 0);

    m_preview_label = new QLabel(right_pane);
    m_preview_label->setAlignment(Qt::AlignCenter);
    m_preview_label->setStyleSheet(
        QStringLiteral("background-color: #12100E; border: 1px solid rgba(255,255,255,25); border-radius: 8px;"));
    m_preview_label->setMinimumSize(480, 320);

    m_meta_label = new QLabel(right_pane);
    m_meta_label->setStyleSheet(QStringLiteral("color: #FFC83D; font-size: 12px; padding: 4px;"));
    m_meta_label->setWordWrap(true);

    right_layout->addWidget(m_preview_label, 1);
    right_layout->addWidget(m_meta_label);

    splitter->addWidget(m_list_widget);
    splitter->addWidget(right_pane);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 2);
    root_layout->addWidget(splitter, 1);

    // Bottom Action Bar
    auto* const action_layout = new QHBoxLayout();
    m_copy_btn = new QPushButton(tr("📋 Copy to Clipboard"), this);
    m_open_folder_btn = new QPushButton(tr("📂 Open Screenshots Folder"), this);
    m_delete_btn = new QPushButton(tr("🗑 Delete"), this);
    auto* const refresh_btn = new QPushButton(tr("🔄 Refresh"), this);
    auto* const close_btn = new QPushButton(tr("Close"), this);

    connect(m_copy_btn, &QPushButton::clicked, this, &AlbumDialog::CopyToClipboard);
    connect(m_open_folder_btn, &QPushButton::clicked, this, &AlbumDialog::OpenInExplorer);
    connect(m_delete_btn, &QPushButton::clicked, this, &AlbumDialog::DeleteCurrentScreenshot);
    connect(refresh_btn, &QPushButton::clicked, this, &AlbumDialog::RefreshGallery);
    connect(close_btn, &QPushButton::clicked, this, &QDialog::accept);

    action_layout->addWidget(m_copy_btn);
    action_layout->addWidget(m_open_folder_btn);
    action_layout->addWidget(m_delete_btn);
    action_layout->addWidget(refresh_btn);
    action_layout->addStretch();
    action_layout->addWidget(close_btn);
    root_layout->addLayout(action_layout);

    RefreshGallery();
}

AlbumDialog::~AlbumDialog() = default;

void AlbumDialog::RefreshGallery() {
    m_entries.clear();
    m_list_widget->clear();
    m_current_index = -1;
    m_preview_label->clear();
    m_meta_label->setText(tr("No screenshot selected."));

    const auto dir_path = Common::FS::GetEdenPath(Common::FS::EdenPath::ScreenshotsDir);
    std::error_code ec;
    if (!std::filesystem::exists(dir_path, ec)) {
        m_status_label->setText(tr("Screenshots folder not found."));
        m_copy_btn->setEnabled(false);
        m_delete_btn->setEnabled(false);
        return;
    }

    uint64_t total_bytes = 0;
    std::vector<std::filesystem::path> paths;
    for (const auto& entry : std::filesystem::directory_iterator(dir_path, ec)) {
        if (!entry.is_regular_file(ec)) {
            continue;
        }
        const std::string ext = entry.path().extension().string();
        if (ext == ".png" || ext == ".PNG" || ext == ".jpg" || ext == ".JPG" || ext == ".jpeg") {
            paths.push_back(entry.path());
        }
    }

    std::sort(paths.begin(), paths.end(), [](const auto& a, const auto& b) {
        std::error_code err;
        return std::filesystem::last_write_time(a, err) > std::filesystem::last_write_time(b, err);
    });

    for (const auto& p : paths) {
        ImageEntry entry;
        entry.path = p;
        entry.filename = QString::fromStdWString(p.filename().wstring());
        entry.file_size = std::filesystem::file_size(p, ec);
        total_bytes += entry.file_size;

        QImageReader reader(QString::fromStdWString(p.wstring()));
        entry.resolution = reader.size();

        // Fast thumbnail read
        QPixmap full_pix(QString::fromStdWString(p.wstring()));
        if (!full_pix.isNull()) {
            entry.thumbnail = full_pix.scaled(128, 72, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }

        m_entries.push_back(std::move(entry));
    }

    for (const auto& item : m_entries) {
        auto* list_item = new QListWidgetItem(QIcon(item.thumbnail), item.filename, m_list_widget);
        list_item->setToolTip(QStringLiteral("%1 (%2x%3, %4)")
                                  .arg(item.filename)
                                  .arg(item.resolution.width())
                                  .arg(item.resolution.height())
                                  .arg(FormatBytes(item.file_size)));
    }

    m_status_label->setText(tr("%1 screenshots (%2 total)")
                                .arg(m_entries.size())
                                .arg(FormatBytes(total_bytes)));

    if (!m_entries.empty()) {
        m_list_widget->setCurrentRow(0);
    } else {
        m_copy_btn->setEnabled(false);
        m_delete_btn->setEnabled(false);
    }
}

void AlbumDialog::SelectItem(int index) {
    if (index < 0 || index >= static_cast<int>(m_entries.size())) {
        m_current_index = -1;
        m_copy_btn->setEnabled(false);
        m_delete_btn->setEnabled(false);
        return;
    }

    m_current_index = index;
    const auto& entry = m_entries[index];
    QPixmap full_pix(QString::fromStdWString(entry.path.wstring()));
    if (!full_pix.isNull()) {
        m_preview_label->setPixmap(full_pix.scaled(m_preview_label->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }

    m_meta_label->setText(QStringLiteral("<b>File:</b> %1 &nbsp;|&nbsp; <b>Resolution:</b> %2x%3 &nbsp;|&nbsp; <b>Size:</b> %4")
                              .arg(entry.filename)
                              .arg(entry.resolution.width())
                              .arg(entry.resolution.height())
                              .arg(FormatBytes(entry.file_size)));

    m_copy_btn->setEnabled(true);
    m_delete_btn->setEnabled(true);
}

void AlbumDialog::CopyToClipboard() {
    if (m_current_index < 0 || m_current_index >= static_cast<int>(m_entries.size())) {
        return;
    }
    const auto& entry = m_entries[m_current_index];
    QImage img(QString::fromStdWString(entry.path.wstring()));
    if (!img.isNull()) {
        QGuiApplication::clipboard()->setImage(img);
        QMessageBox::information(this, tr("Copied"), tr("Screenshot copied to clipboard!"));
    }
}

void AlbumDialog::OpenInExplorer() {
    const auto dir_path = Common::FS::GetEdenPath(Common::FS::EdenPath::ScreenshotsDir);
    QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdWString(dir_path.wstring())));
}

void AlbumDialog::DeleteCurrentScreenshot() {
    if (m_current_index < 0 || m_current_index >= static_cast<int>(m_entries.size())) {
        return;
    }
    const auto& entry = m_entries[m_current_index];
    const auto res = QMessageBox::question(
        this, tr("Confirm Delete"),
        tr("Are you sure you want to delete screenshot '%1'?").arg(entry.filename),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (res == QMessageBox::Yes) {
        std::error_code ec;
        std::filesystem::remove(entry.path, ec);
        RefreshGallery();
    }
}
