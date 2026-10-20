// SPDX-License-Identifier: GPL-3.0-or-later
// In-game capture: run the game with texture dumps on, then keep what it dumped as a numbered session.
#pragma once
#include <filesystem>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace tstudio {

class Capture {
public:
    bool Start(const std::filesystem::path& gameExe, const std::filesystem::path& dumpsDir, std::string* error);
    bool Running() const;
    // Moves the dumps that appeared since Start into <capturesDir>/<n>/; returns n and the files (n = 0 if none).
    std::pair<int, std::vector<std::filesystem::path>> Collect(const std::filesystem::path& capturesDir);
    void SeedForTest(const std::filesystem::path& dumpsDir);  // what Start records, without a process

private:
    void* process_ = nullptr;  // HANDLE (Windows)
    mutable long long pid_ = 0;  // child pid (POSIX); Running() reaps it
    std::filesystem::path dumps_;
    std::set<std::string> before_;
};

std::vector<std::pair<int, std::vector<std::filesystem::path>>> LoadSessions(const std::filesystem::path& capturesDir);

} // namespace tstudio
