// SPDX-License-Identifier: GPL-3.0-or-later
// Runs the game with WIICOMPILED_TEXTURE_DUMPS=1 (patch wiicompiled-texture-studio) and the launcher
// skipped, then keeps the textures it dumped as a numbered capture session.
#include "capture.h"

#include "core.h"
#include "lang.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <cstring>
extern char** environ;
#endif

#include <algorithm>
#include <cstdlib>

namespace tstudio {
namespace {
std::set<std::string> Names(const std::filesystem::path& dir) {
    std::set<std::string> out;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec)) out.insert(Utf8(e.path().filename()));
    return out;
}
} // namespace

void Capture::SeedForTest(const std::filesystem::path& dumpsDir) {
    dumps_ = dumpsDir;
    before_ = Names(dumpsDir);
}

bool Capture::Start(const std::filesystem::path& gameExe, const std::filesystem::path& dumpsDir, std::string* error) {
    SeedForTest(dumpsDir);
#if !defined(_WIN32)
    // The game reads its paths relative to its own directory (portable.txt), so start it there.
    std::vector<std::string> env{"WIICOMPILED_TEXTURE_DUMPS=1", "WIICOMPILED_SKIP_LAUNCHER=1"};
    for (char** e = environ; *e != nullptr; ++e)
        if (std::strncmp(*e, "WIICOMPILED_TEXTURE_DUMPS=", 26) != 0 && std::strncmp(*e, "WIICOMPILED_SKIP_LAUNCHER=", 26) != 0)
            env.emplace_back(*e);
    std::vector<char*> envp;
    for (auto& entry : env) envp.push_back(entry.data());
    envp.push_back(nullptr);
    std::string exe = gameExe.string();
    char* argv[] = {exe.data(), nullptr};
    std::error_code ec;
    const auto previous = std::filesystem::current_path(ec);
    std::filesystem::current_path(gameExe.parent_path(), ec);
    pid_t pid = 0;
    const int rc = posix_spawn(&pid, exe.c_str(), nullptr, nullptr, argv, envp.data());
    std::filesystem::current_path(previous, ec);
    if (rc != 0) {
        if (error) *error = Tr("No se pudo abrir el juego para capturar (", "Could not start the game to capture (") + std::string(std::strerror(rc)) + ").";
        return false;
    }
    pid_ = pid;
    return true;
#else
    std::wstring command = L"\"" + gameExe.wstring() + L"\"";
    SetEnvironmentVariableW(L"WIICOMPILED_TEXTURE_DUMPS", L"1");
    SetEnvironmentVariableW(L"WIICOMPILED_SKIP_LAUNCHER", L"1");
    STARTUPINFOW si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    const BOOL ok = CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr,
                                   gameExe.parent_path().c_str(), &si, &pi);
    SetEnvironmentVariableW(L"WIICOMPILED_TEXTURE_DUMPS", nullptr);
    SetEnvironmentVariableW(L"WIICOMPILED_SKIP_LAUNCHER", nullptr);
    if (!ok) {
        if (error) *error = Tr("No se pudo abrir el juego para capturar (", "Could not start the game to capture (") + std::to_string(GetLastError()) + ").";
        return false;
    }
    CloseHandle(pi.hThread);
    process_ = pi.hProcess;
    return true;
#endif
}

bool Capture::Running() const {
#if defined(_WIN32)
    return process_ != nullptr && WaitForSingleObject(static_cast<HANDLE>(process_), 0) == WAIT_TIMEOUT;
#else
    if (pid_ <= 0) return false;
    int status = 0;
    if (waitpid(static_cast<pid_t>(pid_), &status, WNOHANG) == 0) return true;
    pid_ = 0;  // reaped
    return false;
#endif
}

std::pair<int, std::vector<std::filesystem::path>> Capture::Collect(const std::filesystem::path& capturesDir) {
#if defined(_WIN32)
    if (process_) {
        CloseHandle(static_cast<HANDLE>(process_));
        process_ = nullptr;
    }
#else
    if (pid_ > 0) {
        int status = 0;
        waitpid(static_cast<pid_t>(pid_), &status, WNOHANG);
        pid_ = 0;
    }
#endif
    std::vector<std::filesystem::path> fresh;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(dumps_, ec))
        if (e.is_regular_file(ec) && !before_.count(Utf8(e.path().filename()))) fresh.push_back(e.path());
    if (fresh.empty()) return {0, {}};
    int n = 1;
    for (const auto& [session, _] : LoadSessions(capturesDir)) n = std::max(n, session + 1);
    const auto dir = capturesDir / std::to_string(n);
    std::filesystem::create_directories(dir, ec);
    std::vector<std::filesystem::path> moved;
    for (const auto& f : fresh) {
        const auto target = dir / f.filename();
        std::filesystem::rename(f, target, ec);
        if (ec) {
            ec.clear();
            std::filesystem::copy_file(f, target, ec);
            if (ec) continue;  // left in the dump folder; not part of this session
            std::filesystem::remove(f, ec);
        }
        moved.push_back(target);
    }
    return {n, moved};
}

std::vector<std::pair<int, std::vector<std::filesystem::path>>> LoadSessions(const std::filesystem::path& capturesDir) {
    std::vector<std::pair<int, std::vector<std::filesystem::path>>> out;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(capturesDir, ec)) {
        const int n = std::atoi(Utf8(e.path().filename()).c_str());
        if (!e.is_directory(ec) || n <= 0) continue;
        std::vector<std::filesystem::path> files;
        for (const auto& f : std::filesystem::directory_iterator(e.path(), ec)) files.push_back(f.path());
        out.emplace_back(n, std::move(files));
    }
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace tstudio
