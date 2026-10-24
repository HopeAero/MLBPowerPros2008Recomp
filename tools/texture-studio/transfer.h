// SPDX-License-Identifier: GPL-3.0-or-later
// Export originals as PNG; import edited PNG/DDS files into a mod under their tex1_ names.
#pragma once
#include "catalog.h"

#include <atomic>
#include <unordered_map>

namespace tstudio {
struct ImportResult { std::string file; std::string texture; bool ok = false; std::string message; };
// Work counter for long batches (export, folder import), read by the UI thread.
struct Progress { std::atomic<size_t> done{0}, total{0}; };
// Every replacement file of a mod that applies to this texture (exact palette, $, captured variants).
std::vector<std::filesystem::path> ReplacementsOf(const CatalogTexture& t,
                                                  const std::unordered_map<std::string, std::filesystem::path>& found);
// Deletes them; returns how many files were removed.
size_t RemoveReplacements(const CatalogTexture& t, const std::unordered_map<std::string, std::filesystem::path>& found);
// One image onto one texture (the selected one). allPalettes: write the $ name instead of the exact one.
ImportResult ImportOne(Catalog& c, int texture, const std::filesystem::path& image, const std::filesystem::path& modRoot,
                       bool allPalettes);
// A folder (recursive): files whose name is a known tex1_ name; others reported.
std::vector<ImportResult> ImportFolder(Catalog& c, const std::filesystem::path& folder, const std::filesystem::path& modRoot,
                                       Progress* progress = nullptr);
// A Dolphin texture pack (a folder, or a .zip already unpacked into one): every tex1_ PNG/DDS file, mipmaps
// (_mipN), arbitrary mipmaps (_arb) and "_m_" names included, copied as it is into
// <mod>/textures/<pack>/ keeping its subfolders, since the game reads Dolphin's names itself. The report
// says which ones are textures of the game's packs (the rest may be drawn while playing, or not exist).
std::vector<ImportResult> ImportDolphinPack(Catalog& c, const std::filesystem::path& folder, const std::filesystem::path& modRoot,
                                            const std::string& packName, Progress* progress = nullptr);
// The plain tex1_ name of a Dolphin file name: without _mipN, _arb and the "_m" part ("" if not a tex1_ name).
std::string DolphinBaseName(std::string_view stem);
// Original images as PNG under <out>/<group>/<subgroup>/<name>.png. Returns how many were written.
// grey: how many came out grey because their palette lives in another pack.
size_t Export(Catalog& c, const std::vector<int>& textures, const std::filesystem::path& out, std::vector<std::string>* problems,
              Progress* progress = nullptr, size_t* grey = nullptr);
} // namespace tstudio
