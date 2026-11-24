// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <vector>

#include <QDialog>
#include <QHash>
#include <QIcon>
#include <QSet>
#include <QString>
#include <QTimer>

class ControllerNavigation;
class QButtonGroup;
class QComboBox;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QNetworkAccessManager;
class QPushButton;

namespace Core::HID {
class HIDCore;
}

namespace InputCommon {
class InputSubsystem;
}

// Amiibo files the player added once, tapped onto the virtual reader from a list instead of a file
// dialog each time. A tap made while the game isn't scanning waits until it is. The figures sit on
// cards in a grid (pills above it pick all, favorites, a group or a series); the picked one is
// shown large beside them with what is known about it and what can be done with it.
class AmiiboLibraryDialog final : public QDialog {
    Q_OBJECT

public:
    explicit AmiiboLibraryDialog(QWidget* parent, InputCommon::InputSubsystem* input_subsystem,
                                 Core::HID::HIDCore& hid_core);
    ~AmiiboLibraryDialog() override;

    // Removes the amiibo on the reader and forgets a waiting tap.
    void RemoveFromReader();
    // Called when emulation stops: a waiting tap would land in the next game.
    void CancelPendingTap();

signals:
    void LibraryChanged();

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    // What the list shows for one library entry, worked out once per change to the library
    // rather than on every keystroke in the search box.
    struct Row {
        QString path;
        QString name; ///< For sorting and searching: the label without marks.
        QString label;
        QString file_name;
        QString series;
        QString image;
        QString tooltip;
        int recent; ///< Position in the library: 0 is the most recently tapped.
        bool exists;
    };

    void Rebuild();
    void RefreshFilters();
    void ApplyView();
    void SaveLibrary();
    void ShowContextMenu(const QPoint& pos);
    QStringList SelectedPaths() const;
    void SetFavorite(const QStringList& paths, bool favorite);
    void SetGroup(const QStringList& paths);
    void Rename(const QString& path);
    QIcon IconFor(const QString& image);
    // Downloads an amiipedia picture into the cache and shows it on the items that use it.
    void RequestImage(const QString& image);
    void AddFiles();
    void AddFolder();
    void AddPaths(const QStringList& paths);
    void RemoveSelected();
    void TapSelected();
    void TapNow(const QString& path);
    void MoveToFront(const QString& path);
    QString OnReaderPath();
    QString LabelFor(const QString& path) const;
    void UpdateState();
    /// The panel beside the grid: the picked figure (or the one on the reader), large.
    void UpdateDetails(const QString& on_reader, int state);
    /// The pills above the grid, one per entry of `view`.
    void RebuildPills();
    void BackUp(const QString& path);

    InputCommon::InputSubsystem* input_subsystem;
    ControllerNavigation* controller_navigation = nullptr;

    QLineEdit* search = nullptr;
    QComboBox* view = nullptr;
    QComboBox* sort = nullptr;
    QListWidget* list = nullptr;
    QLabel* status = nullptr;
    QPushButton* tap_button = nullptr;
    QPushButton* remove_tag_button = nullptr;
    QPushButton* forget_button = nullptr;
    QPushButton* rename_button = nullptr;
    QPushButton* favorite_button = nullptr;
    QPushButton* group_button = nullptr;
    QPushButton* backup_button = nullptr;
    QLabel* count_label = nullptr;
    QLabel* listening_label = nullptr;
    QLabel* detail_heading = nullptr;
    QLabel* detail_picture = nullptr;
    QLabel* detail_name = nullptr;
    QLabel* detail_series = nullptr;
    QLabel* detail_group = nullptr;
    QLabel* detail_file = nullptr;
    QLabel* detail_id = nullptr;
    QHBoxLayout* pill_row = nullptr;
    QButtonGroup* pill_group = nullptr;
    QString details_path;
    int details_state = -1;

    std::vector<Row> rows;
    QHash<QString, QIcon> icons;
    QNetworkAccessManager* network = nullptr;
    QSet<QString> downloading;
    QIcon placeholder;
    QString pending_path;
    QString reader_raw_path;
    QString reader_path;
    QString bold_path;
    int last_state = -1;
    QTimer state_timer;
};
