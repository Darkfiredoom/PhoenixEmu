// SPDX-FileCopyrightText: 2026 PhoenixEmu contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDialog>
#include <QTimer>

class QLabel;
class QProgressBar;
class QTabWidget;

/// Real-time interactive pipeline & engine telemetry visualizer showing JIT,
/// Vulkan pipeline status, memory allocation, and FSR/Frame Generation states.
class PipelineVisualizerDialog : public QDialog {
    Q_OBJECT

public:
    explicit PipelineVisualizerDialog(QWidget* parent = nullptr);
    ~PipelineVisualizerDialog() override;

private:
    void UpdateTelemetry();

    QTimer m_refresh_timer;

    // CPU / JIT Tab
    QLabel* m_app_title_label = nullptr;
    QLabel* m_program_id_label = nullptr;
    QLabel* m_emulation_state_label = nullptr;
    QLabel* m_jit_mode_label = nullptr;
    QLabel* m_memory_commit_label = nullptr;

    // Vulkan & Shaders Tab
    QLabel* m_vulkan_device_label = nullptr;
    QLabel* m_vulkan_driver_label = nullptr;
    QLabel* m_wave_mode_label = nullptr;
    QLabel* m_sam_heap_label = nullptr;
    QLabel* m_pipeline_count_label = nullptr;

    // FSR & Frame Gen Tab
    QLabel* m_fsr_status_label = nullptr;
    QLabel* m_frame_gen_status_label = nullptr;
    QLabel* m_async_queue_label = nullptr;
    QLabel* m_hud_mask_label = nullptr;

    // Memory Tab
    QProgressBar* m_vram_bar = nullptr;
    QLabel* m_vram_text = nullptr;
    QProgressBar* m_ram_bar = nullptr;
    QLabel* m_ram_text = nullptr;
};
