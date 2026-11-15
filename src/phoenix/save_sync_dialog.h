// SPDX-FileCopyrightText: Copyright 2026 PhoenixEmu Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <memory>
#include <QDialog>

class QLineEdit;
class QLabel;
class QCheckBox;
class QPushButton;

class SaveSyncDialog : public QDialog {
    Q_OBJECT

public:
    explicit SaveSyncDialog(QWidget* parent = nullptr);
    ~SaveSyncDialog() override;

    static void PerformBackgroundSync();

private slots:
    void OnBrowseTarget();
    void OnBackupNow();
    void OnRestore();
    void OnOpenLocal();
    void OnOpenTarget();
    void OnAutoSyncToggled(bool checked);

private:
    void RefreshStatus();

    QLabel* local_status_label{nullptr};
    QLabel* target_status_label{nullptr};
    QLineEdit* target_path_edit{nullptr};
    QCheckBox* auto_sync_check{nullptr};
    QPushButton* backup_button{nullptr};
    QPushButton* restore_button{nullptr};
};
