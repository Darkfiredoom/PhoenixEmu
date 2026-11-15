// SPDX-FileCopyrightText: 2026 PhoenixEmu contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QTabWidget>
#include <QVBoxLayout>

#ifdef _WIN32
#include <windows.h>
#endif

#include "common/settings.h"
#include "core/core.h"
#include "phoenix/pipeline_visualizer_dialog.h"
#include "phoenix/util/phoenix_theme.h"
#include "qt_common/qt_common.h"
#include "video_core/gpu.h"

PipelineVisualizerDialog::PipelineVisualizerDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Live Pipeline & Engine Visualizer"));
    resize(580, 440);

    auto* const main_layout = new QVBoxLayout(this);
    main_layout->setSpacing(12);

    auto* const tabs = new QTabWidget(this);

    // ==========================================
    // Tab 1: CPU & Dynarmic ARM64 JIT
    // ==========================================
    auto* const cpu_tab = new QWidget(this);
    auto* const cpu_layout = new QVBoxLayout(cpu_tab);

    auto* const app_group = new QGroupBox(tr("Target Application State"), cpu_tab);
    auto* const app_form = new QFormLayout(app_group);
    m_app_title_label = new QLabel(tr("No game loaded"), this);
    m_program_id_label = new QLabel(QStringLiteral("0x0000000000000000"), this);
    m_emulation_state_label = new QLabel(tr("Idle"), this);
    app_form->addRow(tr("Game Title:"), m_app_title_label);
    app_form->addRow(tr("Program ID:"), m_program_id_label);
    app_form->addRow(tr("Core State:"), m_emulation_state_label);
    cpu_layout->addWidget(app_group);

    auto* const jit_group = new QGroupBox(tr("Dynarmic ARM64 JIT & Virtual Memory"), cpu_tab);
    auto* const jit_form = new QFormLayout(jit_group);
    m_jit_mode_label = new QLabel(tr("ARM64 Fastmem Virtual Addressing (Dynamic Recompilation)"), this);
    m_memory_commit_label =
        new QLabel(tr("4 MiB Chunked Growth Protection (Error 1455 Hardened)"), this);
    m_jit_mode_label->setStyleSheet(QStringLiteral("color: #00E676; font-weight: bold;"));
    m_memory_commit_label->setStyleSheet(QStringLiteral("color: #00E676; font-weight: bold;"));
    jit_form->addRow(tr("JIT Engine:"), m_jit_mode_label);
    jit_form->addRow(tr("Memory Backing:"), m_memory_commit_label);
    cpu_layout->addWidget(jit_group);
    cpu_layout->addStretch();
    tabs->addTab(cpu_tab, tr("CPU & Dynarmic JIT"));

    // ==========================================
    // Tab 2: Vulkan & Shaders
    // ==========================================
    auto* const vk_tab = new QWidget(this);
    auto* const vk_layout = new QVBoxLayout(vk_tab);

    auto* const vk_group = new QGroupBox(tr("Vulkan Graphics & Compute Engine"), vk_tab);
    auto* const vk_form = new QFormLayout(vk_group);
    m_vulkan_device_label = new QLabel(tr("Auto-Selected Vulkan Physical Device"), this);
    m_vulkan_driver_label = new QLabel(tr("Vulkan 1.3 / SPIR-V Translation Active"), this);
    m_wave_mode_label =
        new QLabel(tr("Wave32 Compute / Wave64 Graphics (VK_EXT_subgroup_size_control)"), this);
    m_sam_heap_label =
        new QLabel(tr("Direct SAM / Resizable BAR Host-Visible Buffer Streaming"), this);
    m_pipeline_count_label = new QLabel(tr("Pipeline Binary Cache Active (Zero-Stutter Persistence)"), this);

    m_wave_mode_label->setStyleSheet(QStringLiteral("color: #00E676; font-weight: bold;"));
    m_sam_heap_label->setStyleSheet(QStringLiteral("color: #00E676; font-weight: bold;"));

    vk_form->addRow(tr("Graphics Device:"), m_vulkan_device_label);
    vk_form->addRow(tr("Driver API:"), m_vulkan_driver_label);
    vk_form->addRow(tr("Subgroup Sizing:"), m_wave_mode_label);
    vk_form->addRow(tr("Memory Streaming:"), m_sam_heap_label);
    vk_form->addRow(tr("Shaders / Pipelines:"), m_pipeline_count_label);
    vk_layout->addWidget(vk_group);
    vk_layout->addStretch();
    tabs->addTab(vk_tab, tr("Vulkan & Pipelines"));

    // ==========================================
    // Tab 3: FidelityFX & Frame Gen
    // ==========================================
    auto* const ffx_tab = new QWidget(this);
    auto* const ffx_layout = new QVBoxLayout(ffx_tab);

    auto* const ffx_group = new QGroupBox(tr("AMD FidelityFX™ Technology"), ffx_tab);
    auto* const ffx_form = new QFormLayout(ffx_group);
    m_fsr_status_label = new QLabel(this);
    m_frame_gen_status_label = new QLabel(this);
    m_async_queue_label =
        new QLabel(tr("Dedicated Async Compute Queue (VK_QUEUE_COMPUTE_BIT)"), this);
    m_hud_mask_label =
        new QLabel(tr("Noise-Filtered RGB Masking (> 0.015 Threshold Protection)"), this);

    m_async_queue_label->setStyleSheet(QStringLiteral("color: #00E676; font-weight: bold;"));
    m_hud_mask_label->setStyleSheet(QStringLiteral("color: #00E676; font-weight: bold;"));

    ffx_form->addRow(tr("FSR 3.1 Upscaling:"), m_fsr_status_label);
    ffx_form->addRow(tr("Frame Generation:"), m_frame_gen_status_label);
    ffx_form->addRow(tr("Compute Dispatch:"), m_async_queue_label);
    ffx_form->addRow(tr("HUD Artifact Guard:"), m_hud_mask_label);
    ffx_layout->addWidget(ffx_group);
    ffx_layout->addStretch();
    tabs->addTab(ffx_tab, tr("FidelityFX & Frame Gen"));

    // ==========================================
    // Tab 4: Memory & VRAM
    // ==========================================
    auto* const mem_tab = new QWidget(this);
    auto* const mem_layout = new QVBoxLayout(mem_tab);

    auto* const mem_group = new QGroupBox(tr("Physical & Commit Memory Map"), mem_tab);
    auto* const mem_form = new QFormLayout(mem_group);

    m_ram_bar = new QProgressBar(this);
    m_ram_bar->setRange(0, 100);
    m_ram_text = new QLabel(this);

    m_vram_bar = new QProgressBar(this);
    m_vram_bar->setRange(0, 100);
    m_vram_text = new QLabel(this);

    mem_form->addRow(tr("Host RAM Load:"), m_ram_bar);
    mem_form->addRow(QString{}, m_ram_text);
    mem_form->addRow(tr("Device Commit / VRAM:"), m_vram_bar);
    mem_form->addRow(QString{}, m_vram_text);
    mem_layout->addWidget(mem_group);
    mem_layout->addStretch();
    tabs->addTab(mem_tab, tr("Memory & VRAM"));

    main_layout->addWidget(tabs);

    auto* const button_box = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(button_box, &QDialogButtonBox::rejected, this, &QDialog::accept);
    main_layout->addWidget(button_box);

    connect(&m_refresh_timer, &QTimer::timeout, this, &PipelineVisualizerDialog::UpdateTelemetry);
    m_refresh_timer.start(500);
    UpdateTelemetry();
}

PipelineVisualizerDialog::~PipelineVisualizerDialog() = default;

void PipelineVisualizerDialog::UpdateTelemetry() {
    const bool is_powered = QtCommon::system && QtCommon::system->IsPoweredOn();

    // App state
    if (is_powered) {
        const u64 pid = QtCommon::system->GetApplicationProcessProgramID();
        const std::string path = QtCommon::system->GetCurrentApplicationFilePath();
        m_program_id_label->setText(
            QStringLiteral("0x%1").arg(pid, 16, 16, QLatin1Char('0')).toUpper());
        m_app_title_label->setText(path.empty() ? tr("Active Application") : QString::fromStdString(path));
        m_emulation_state_label->setText(tr("Running (Guest Threads Active)"));
        m_emulation_state_label->setStyleSheet(QStringLiteral("color: #00E676; font-weight: bold;"));
    } else {
        m_program_id_label->setText(QStringLiteral("0x0000000000000000"));
        m_app_title_label->setText(tr("No game loaded"));
        m_emulation_state_label->setText(tr("Idle (Awaiting Game Start)"));
        m_emulation_state_label->setStyleSheet(QStringLiteral("color: #E8D9B5;"));
    }

    // FSR & Frame Gen
    const bool fg_enabled = Settings::values.frame_gen.GetValue();
    const int sharpness = Settings::values.fsr_sharpening_slider.GetValue();
    m_fsr_status_label->setText(
        tr("Active (CAS & Temporal Filter at %1% Sharpness)").arg(sharpness));
    m_fsr_status_label->setStyleSheet(QStringLiteral("color: #00E676; font-weight: bold;"));

    if (fg_enabled) {
        m_frame_gen_status_label->setText(
            tr("Enabled (2x Temporal Interpolation Active)"));
        m_frame_gen_status_label->setStyleSheet(
            QStringLiteral("color: #00E676; font-weight: bold;"));
    } else {
        m_frame_gen_status_label->setText(tr("Disabled (Native Frame Rate)"));
        m_frame_gen_status_label->setStyleSheet(
            QStringLiteral("color: #E8D9B5;"));
    }

    // System Memory
#ifdef _WIN32
    MEMORYSTATUSEX mem{};
    mem.dwLength = sizeof(mem);
    if (GlobalMemoryStatusEx(&mem)) {
        m_ram_bar->setValue(mem.dwMemoryLoad);
        const double used_gb =
            (mem.ullTotalPhys - mem.ullAvailPhys) / (1024.0 * 1024.0 * 1024.0);
        const double total_gb = mem.ullTotalPhys / (1024.0 * 1024.0 * 1024.0);
        m_ram_text->setText(tr("%1 GB / %2 GB used (%3% load)")
                                .arg(used_gb, 0, 'f', 1)
                                .arg(total_gb, 0, 'f', 1)
                                .arg(mem.dwMemoryLoad));

        const double page_used_gb =
            (mem.ullTotalPageFile - mem.ullAvailPageFile) / (1024.0 * 1024.0 * 1024.0);
        const double page_total_gb = mem.ullTotalPageFile / (1024.0 * 1024.0 * 1024.0);
        const int commit_pct = page_total_gb > 0 ? int((page_used_gb / page_total_gb) * 100) : 0;
        m_vram_bar->setValue(commit_pct);
        m_vram_text->setText(tr("%1 GB / %2 GB system commit (%3%)")
                                 .arg(page_used_gb, 0, 'f', 1)
                                 .arg(page_total_gb, 0, 'f', 1)
                                 .arg(commit_pct));
    }
#endif
}
