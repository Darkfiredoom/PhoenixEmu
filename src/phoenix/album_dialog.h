// SPDX-FileCopyrightText: 2026 PhoenixEmu contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>
#include <QDialog>
#include <QPixmap>
#include <QSize>

#include "common/common_types.h"

class QLabel;
class QListWidget;
class QPushButton;

class AlbumDialog : public QDialog {
    Q_OBJECT

public:
    explicit AlbumDialog(QWidget* parent = nullptr);
    ~AlbumDialog() override;

private:
    void RefreshGallery();
    void SelectItem(int index);
    void CopyToClipboard();
    void OpenInExplorer();
    void DeleteCurrentScreenshot();

    struct ImageEntry {
        std::filesystem::path path;
        QString filename;
        QString date_str;
        QPixmap thumbnail;
        QSize resolution;
        u64 file_size = 0;
    };

    std::vector<ImageEntry> m_entries;
    int m_current_index = -1;

    QLabel* m_status_label = nullptr;
    QListWidget* m_list_widget = nullptr;
    QLabel* m_preview_label = nullptr;
    QLabel* m_meta_label = nullptr;

    QPushButton* m_copy_btn = nullptr;
    QPushButton* m_open_folder_btn = nullptr;
    QPushButton* m_delete_btn = nullptr;
};
