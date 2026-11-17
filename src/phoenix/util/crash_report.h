// SPDX-FileCopyrightText: Copyright 2026 PhoenixEmu contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Windows crash report: when PhoenixEmu crashes, phoenixemu_log.txt gets the exception and the module and
// offset it happened at (match the offset with the release's phoenixemu-*-map.zip), and a minidump
// goes to <data>/crash_dumps. Without it a crash just ended the log mid-line.
//
// Linux: a fatal signal writes the signal, fault address, memory use and a backtrace (module and
// offset per frame) to <log dir>/crash_report.txt and flushes the log. The next start copies the
// report into phoenixemu_log.txt. A run that ended with neither was killed from outside (out of memory).

#pragma once

#ifdef _WIN32

#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
// dbghelp.h needs windows.h first.
#include <dbghelp.h>

#include <fmt/format.h>

#include "common/fs/path_util.h"
#include "common/logging.h"

namespace CrashReport {

namespace detail {

inline std::wstring& DumpDir() {
    static std::wstring dir;
    return dir;
}

inline std::string ModuleAt(const void* address, uintptr_t& offset) {
    HMODULE module{};
    offset = reinterpret_cast<uintptr_t>(address);
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            static_cast<LPCWSTR>(address), &module) ||
        module == nullptr) {
        return "?";
    }
    offset -= reinterpret_cast<uintptr_t>(module);
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(module, path, MAX_PATH);
    return std::filesystem::path(path).filename().string();
}

inline std::wstring WriteDump(EXCEPTION_POINTERS* info) {
    using WriteFn = BOOL(WINAPI*)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE,
                                  PMINIDUMP_EXCEPTION_INFORMATION, PMINIDUMP_USER_STREAM_INFORMATION,
                                  PMINIDUMP_CALLBACK_INFORMATION);
    const HMODULE dbghelp = LoadLibraryW(L"dbghelp.dll");
    const auto write =
        dbghelp ? reinterpret_cast<WriteFn>(GetProcAddress(dbghelp, "MiniDumpWriteDump")) : nullptr;
    if (!write || DumpDir().empty()) {
        return {};
    }
    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t name[96];
    swprintf_s(name, L"\\phoenixemu_%04u%02u%02u_%02u%02u%02u.dmp", now.wYear, now.wMonth, now.wDay,
               now.wHour, now.wMinute, now.wSecond);
    const std::wstring path = DumpDir() + name;
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return {};
    }
    MINIDUMP_EXCEPTION_INFORMATION exception{GetCurrentThreadId(), info, FALSE};
    const BOOL ok = write(GetCurrentProcess(), GetCurrentProcessId(), file,
                          static_cast<MINIDUMP_TYPE>(MiniDumpWithThreadInfo |
                                                     MiniDumpWithIndirectlyReferencedMemory |
                                                     MiniDumpWithUnloadedModules),
                          &exception, nullptr, nullptr);
    CloseHandle(file);
    return ok ? path : std::wstring{};
}

inline LONG WINAPI Filter(EXCEPTION_POINTERS* info) {
    static std::atomic_bool handled{};
    if (handled.exchange(true) || info == nullptr || info->ExceptionRecord == nullptr) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    const EXCEPTION_RECORD& record = *info->ExceptionRecord;
    // The dump first: logging can't be trusted if the crash was inside the logger.
    const std::wstring dump = WriteDump(info);
    uintptr_t offset{};
    const std::string module = ModuleAt(record.ExceptionAddress, offset);
    std::string access;
    if (record.ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record.NumberParameters >= 2) {
        const auto kind = record.ExceptionInformation[0];
        access = fmt::format(" ({} of {:#x})",
                             kind == 0 ? "read" : kind == 1 ? "write" : "execute",
                             record.ExceptionInformation[1]);
    }
    LOG_CRITICAL(Frontend,
                 "PhoenixEmu crashed: exception {:#010x}{} at {}+{:#x} (thread {}). Crash dump: {}",
                 static_cast<u32>(record.ExceptionCode), access, module, offset,
                 GetCurrentThreadId(),
                 dump.empty() ? std::string{"not written"}
                              : std::filesystem::path(dump).string());
    Common::Log::Stop(); // flush
    return EXCEPTION_CONTINUE_SEARCH;
}

} // namespace detail

/// Call once at startup.
inline void Install() {
    std::error_code ec;
    const auto dir = Common::FS::GetEdenPath(Common::FS::EdenPath::CrashDumpsDir);
    std::filesystem::create_directories(dir, ec);
    detail::DumpDir() = dir.wstring();
    SetUnhandledExceptionFilter(detail::Filter);
}

inline void LogPrevious() {}

} // namespace CrashReport

#elif defined(__linux__) && defined(__GLIBC__) && !defined(__ANDROID__)

#include <atomic>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <dlfcn.h>
#include <execinfo.h>
#include <fcntl.h>
#include <sys/prctl.h>
#include <ucontext.h>
#include <unistd.h>

#include "common/fs/path_util.h"
#include "common/logging.h"

namespace CrashReport {

namespace detail {

inline char report_path[1024];

// The handler only uses calls that are safe after a crash: no allocation, no stdio.
inline void Put(int fd, const char* text) {
    [[maybe_unused]] const auto written = write(fd, text, std::strlen(text));
}

inline void PutNumber(int fd, std::uintptr_t value, unsigned base) {
    char digits[24];
    int at = sizeof(digits);
    digits[--at] = 0;
    do {
        digits[--at] = "0123456789abcdef"[value % base];
        value /= base;
    } while (value != 0 && at > 0);
    Put(fd, base == 16 ? "0x" : "");
    Put(fd, digits + at);
}

inline void PutFile(int fd, const char* path) {
    const int in = open(path, O_RDONLY);
    if (in < 0) {
        return;
    }
    char block[4096];
    for (ssize_t got; (got = read(in, block, sizeof(block))) > 0;) {
        [[maybe_unused]] const auto written = write(fd, block, static_cast<size_t>(got));
    }
    close(in);
}

inline void Handler(int sig, siginfo_t* info, void* raw_context) {
    static std::atomic_flag handled;
    if (handled.test_and_set()) {
        // Another thread is writing the report.
        sleep(3);
    } else {
        // A crash inside the logger or the allocator must not hang the report.
        alarm(5);
        const int fd = open(report_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) {
            std::uintptr_t pc{};
#if defined(__x86_64__)
            pc = static_cast<std::uintptr_t>(
                static_cast<ucontext_t*>(raw_context)->uc_mcontext.gregs[REG_RIP]);
#elif defined(__aarch64__)
            pc = static_cast<std::uintptr_t>(static_cast<ucontext_t*>(raw_context)->uc_mcontext.pc);
#endif
            char thread[32]{};
            prctl(PR_GET_NAME, thread);
            Put(fd, "signal ");
            PutNumber(fd, static_cast<std::uintptr_t>(sig), 10);
            Put(fd, sig == SIGSEGV   ? " (SIGSEGV)"
                    : sig == SIGBUS  ? " (SIGBUS)"
                    : sig == SIGABRT ? " (SIGABRT)"
                    : sig == SIGILL  ? " (SIGILL)"
                    : sig == SIGFPE  ? " (SIGFPE)"
                                     : "");
            Put(fd, " code ");
            PutNumber(fd, static_cast<std::uintptr_t>(info ? info->si_code : 0), 10);
            Put(fd, " address ");
            PutNumber(fd, info ? reinterpret_cast<std::uintptr_t>(info->si_addr) : 0, 16);
            Put(fd, " thread ");
            Put(fd, thread);
            Put(fd, "\npc ");
            PutNumber(fd, pc, 16);
            Dl_info module{};
            if (pc != 0 && dladdr(reinterpret_cast<void*>(pc), &module) != 0 && module.dli_fname) {
                Put(fd, " = ");
                Put(fd, module.dli_fname);
                Put(fd, "+");
                PutNumber(fd, pc - reinterpret_cast<std::uintptr_t>(module.dli_fbase), 16);
            }
            Put(fd, "\nbacktrace:\n");
            void* frames[64];
            backtrace_symbols_fd(frames, backtrace(frames, 64), fd);
            Put(fd, "memory:\n");
            PutFile(fd, "/proc/self/status");
            Put(fd, "system memory:\n");
            PutFile(fd, "/proc/meminfo");
            close(fd);
        }
        Common::Log::Stop(); // flush
        alarm(0);
    }
    // The default action ends the process and leaves the usual core dump.
    signal(sig, SIG_DFL);
    raise(sig);
}

} // namespace detail

/// Call once at startup, before the CPU backends install their own fault handlers: those pass
/// the faults they do not handle on to this one.
inline void Install() {
    const auto path =
        (Common::FS::GetEdenPath(Common::FS::EdenPath::LogDir) / "crash_report.txt").string();
    if (path.size() >= sizeof(detail::report_path)) {
        return;
    }
    std::memcpy(detail::report_path, path.c_str(), path.size() + 1);
    // backtrace() loads libgcc on first use, which must not happen inside the handler.
    void* frame[1];
    backtrace(frame, 1);
    struct sigaction action {};
    action.sa_sigaction = detail::Handler;
    action.sa_flags = SA_SIGINFO;
    sigemptyset(&action.sa_mask);
    for (const int sig : {SIGSEGV, SIGBUS, SIGABRT, SIGILL, SIGFPE}) {
        sigaction(sig, &action, nullptr);
    }
}

/// Call once the log is open: copies the report of the last run's crash into the log.
inline void LogPrevious() {
    std::error_code ec;
    const std::filesystem::path path{detail::report_path};
    if (path.empty() || !std::filesystem::exists(path, ec)) {
        return;
    }
    std::stringstream text;
    text << std::ifstream{path}.rdbuf();
    std::string line;
    LOG_CRITICAL(Frontend, "The previous run crashed. Its crash report:");
    while (std::getline(text, line)) {
        LOG_CRITICAL(Frontend, "  {}", line);
    }
    auto kept = path;
    kept.replace_extension(".old.txt");
    std::filesystem::rename(path, kept, ec);
}

} // namespace CrashReport

#else

namespace CrashReport {
inline void Install() {}
inline void LogPrevious() {}
} // namespace CrashReport

#endif
