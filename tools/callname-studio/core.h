// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "config.h"
#include "callnames.h"
#include "toml.hpp"
#include <set>

namespace cstudio {
namespace fs = std::filesystem;
using Value = toml::value;
struct Preview { std::string text, upper, dropped; size_t count = 0; };
Preview PreviewText(const std::string& text);
Value Load(const fs::path& mod);
void Save(const fs::path& mod, const Value& document);
std::set<int> UsedVoices(const fs::path& mods, const fs::path& editing = {}, int skip = -1);
int NextVoice(const std::set<int>& used);
bool SafeRelative(const fs::path& folder, const std::string& name);
std::string ImportAudio(const fs::path& mod, const fs::path& source);
void Validate(const fs::path& mod, const Value& entry);
void CreateMod(const fs::path& mods, const std::string& name);
int Selftest();
}
