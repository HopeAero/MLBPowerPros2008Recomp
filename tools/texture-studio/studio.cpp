// SPDX-License-Identifier: GPL-3.0-or-later
// Power Pros Wii 2008 Texture Studio: browse every texture of the player's own copy of MLB Power Pros
// 2008, export it as PNG, and import edited (or upscaled) PNGs into a texture mod. The game files are
// only read; replacements go to mods/<mod>/textures/ as Dolphin-style tex1_ names the runtime loads.
#include "capture.h"
#include "catalog.h"
#include "config.h"
#include "gx.h"
#include "images.h"
#include "lang.h"
#include "modfolder.h"
#include "names.h"
#include "pack.h"
#include "projectbin.h"
#include "transfer.h"
#include "ui.h"

#include <SDL3/SDL.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlrenderer3.h>

#if defined(_WIN32)
#include <windows.h>
#include <shellapi.h>
#undef LoadImage  // windows.h macro; tstudio::LoadImage is ours
#else
#include <spawn.h>
extern char** environ;
#endif

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <iterator>
#include <list>
#include <memory>
#include <map>
#include <mutex>
#include <set>
#include <thread>

namespace tstudio { int RunSelftest(); }

namespace {
using namespace tstudio;
namespace fs = std::filesystem;

// ---- Developer checks against local game data (nothing of it is stored) ---------------------------
int DevMode(const std::vector<std::string>& args) {
    if (args.size() == 2 && args[0] == "--list-pack") {
        std::ifstream f(PathFromUtf8(args[1]), std::ios::binary);
        const std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), {});
        for (const auto& t : FindTextures(d))
            std::printf("%#x %u %u %u %#x %016llx\n", t.offset, t.width, t.height, t.format, t.paletteEntries ? t.paletteData : 0,
                        static_cast<unsigned long long>(
                            TextureHash(std::span<const uint8_t>(d.data() + t.data, d.size() - t.data), t.width, t.height, t.format)));
        return 0;
    }
    if (args.size() == 2 && args[0] == "--catalog") {
        Catalog c;
        std::string error;
        const auto start = std::chrono::steady_clock::now();
        if (!c.Build(PathFromUtf8(args[1]), nullptr, &error)) { std::fprintf(stderr, "%s\n", error.c_str()); return 1; }
        size_t wildcard = 0;
        for (const auto& t : c.Textures()) wildcard += t.key.paletteWildcard;
        std::printf("%zu textures, %zu with unknown palette, %.1f s\n", c.Textures().size(), wildcard,
                    std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
        return 0;
    }
    // Scriptable versions of the window's actions (automation, and tests without clicking).
    if (args.size() == 3 && args[0] == "--new-mod") {
        std::string error;
        if (!CreateMod(PathFromUtf8(args[1]), args[2], &error)) { std::fprintf(stderr, "%s\n", error.c_str()); return 1; }
        return 0;
    }
    if (args.size() == 4 && (args[0] == "--export" || args[0] == "--import-folder")) {
        Catalog c;
        std::string error;
        if (!c.Build(PathFromUtf8(args[1]), nullptr, &error)) { std::fprintf(stderr, "%s\n", error.c_str()); return 1; }
        if (args[0] == "--export") {  // --export <project.bin> <group[/subgroup]> <out dir>
            const size_t slash = args[2].find('/');
            const std::string group = args[2].substr(0, slash), sub = slash == std::string::npos ? "" : args[2].substr(slash + 1);
            std::vector<int> picked;
            for (int i = 0; i < static_cast<int>(c.Textures().size()); ++i)
                if (c.Textures()[i].group == group && (sub.empty() || c.Textures()[i].subgroup == sub)) picked.push_back(i);
            std::vector<std::string> problems;
            const size_t n = Export(c, picked, PathFromUtf8(args[3]), &problems);
            std::printf("%zu exported, %zu problems\n", n, problems.size());
            return 0;
        }
        int ok = 0;  // --import-folder <project.bin> <mod folder> <folder>
        for (const auto& r : ImportFolder(c, PathFromUtf8(args[3]), PathFromUtf8(args[2]))) {
            ok += r.ok;
            std::printf("%s %s: %s\n", r.ok ? "OK" : "NO", r.file.c_str(), r.message.c_str());
        }
        std::printf("%d imported\n", ok);
        return 0;
    }
    if (args.size() == 2 && args[0] == "--list-entries") {
        std::string error;
        const auto index = ReadProjectIndex(PathFromUtf8(args[1]), &error);
        if (!index) { std::fprintf(stderr, "%s\n", error.c_str()); return 1; }
        for (const auto& e : *index) std::printf("%llu %u %s\n", static_cast<unsigned long long>(e.offset), e.size, e.name.c_str());
        return 0;
    }
    if (args.size() == 3 && args[0] == "--whereis") {  // --whereis <project.bin> <dumps dir>: category and pack of each dump
        Catalog c;
        std::string error;
        if (!c.Build(PathFromUtf8(args[1]), nullptr, &error)) { std::fprintf(stderr, "%s\n", error.c_str()); return 1; }
        for (const auto& f : fs::directory_iterator(PathFromUtf8(args[2]))) {
            const std::string stem = Utf8(f.path().stem());
            const auto n = ParseName(stem);
            if (!n) continue;
            int i = c.Find(stem);
            if (i < 0) i = c.FindTexture(*n);
            if (i < 0) { std::printf("%s -\n", stem.c_str()); continue; }
            const auto& t = c.Textures()[i];
            std::printf("%s %s/%s %s\n", stem.c_str(), t.group.c_str(), t.subgroup.c_str(), c.Entries()[t.locations[0].entry].name.c_str());
        }
        return 0;
    }
    if (args.size() == 3 && args[0] == "--check-dumps") {  // names computed here vs the game's dumps
        Catalog c;
        std::string error;
        if (!c.Build(PathFromUtf8(args[1]), nullptr, &error)) { std::fprintf(stderr, "%s\n", error.c_str()); return 1; }
        int total = 0, exact = 0, texture = 0;
        for (const auto& f : fs::directory_iterator(PathFromUtf8(args[2]))) {
            const std::string stem = Utf8(f.path().stem());
            const auto n = ParseName(stem);
            if (!n) continue;
            ++total;
            exact += c.Find(stem) >= 0;
            texture += c.FindTexture(*n) >= 0;
        }
        std::printf("textures %zu, dumps %d, same texture %d, same full name %d\n", c.Textures().size(), total, texture, exact);
        return 0;
    }
    return -1;
}

// ---- Application state ------------------------------------------------------------------------------
struct Paths { fs::path config, userData, mods, game, projectBin; };

struct DialogResult {
    std::mutex mutex;
    std::string path;
    bool ready = false;
};
void SDLCALL OnDialog(void* userdata, const char* const* files, int) {
    auto* result = static_cast<DialogResult*>(userdata);
    std::lock_guard lock(result->mutex);
    result->path = files != nullptr && files[0] != nullptr ? files[0] : "";
    result->ready = true;
}

enum class Pending { None, ExportOne, ImportOne, ExportCategory, ImportFolder, Zip };

// GPU copies of decoded textures, least recently used dropped first; a few created per frame so
// scrolling a large category never stalls.
struct Thumbs {
    struct Entry { SDL_Texture* tex; int w, h; std::list<std::string>::iterator lru; };
    std::unordered_map<std::string, Entry> map;
    std::list<std::string> order;
    int budget = 0;
    // maxSide: thumbnails are scaled down to it (0: full size, for the zoom view); *w/*h are the source size.
    SDL_Texture* Get(SDL_Renderer* r, const std::string& key, const std::function<Image()>& load, int* w, int* h,
                     uint32_t maxSide = 256) {
        if (auto it = map.find(key); it != map.end()) {
            order.splice(order.begin(), order, it->second.lru);
            *w = it->second.w;
            *h = it->second.h;
            return it->second.tex;
        }
        if (budget <= 0) return nullptr;
        --budget;
        const Image source = load();
        if (source.Empty()) return nullptr;
        const Image img = maxSide ? Downscale(source, maxSide) : source;
        SDL_Texture* tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, static_cast<int>(img.width),
                                             static_cast<int>(img.height));
        if (!tex) return nullptr;
        SDL_UpdateTexture(tex, nullptr, img.rgba.data(), static_cast<int>(img.width * 4));
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        order.push_front(key);
        map[key] = {tex, static_cast<int>(source.width), static_cast<int>(source.height), order.begin()};
        while (map.size() > 600) {
            SDL_DestroyTexture(map[order.back()].tex);
            map.erase(order.back());
            order.pop_back();
        }
        *w = static_cast<int>(source.width);
        *h = static_cast<int>(source.height);
        return tex;
    }
    void Clear() {
        for (auto& [_, e] : map) SDL_DestroyTexture(e.tex);
        map.clear();
        order.clear();
    }
    void Forget(const std::string& key) {
        if (auto it = map.find(key); it != map.end()) {
            SDL_DestroyTexture(it->second.tex);
            order.erase(it->second.lru);
            map.erase(it);
        }
    }
};

struct App {
    Paths paths;
    bool gameMissing = false;  // game not found: only its message is shown
    std::string fatal;         // project.bin unreadable: only this message is shown
    Catalog catalog;
    std::thread builder;
    std::atomic<float> progress{0};
    std::atomic<bool> built{false};
    bool ready = false;
    std::string buildError;
    std::map<std::string, std::map<std::string, int>> counts;  // group -> subgroup -> textures
    std::vector<ModInfo> mods;
    int activeMod = -1;
    std::unordered_map<std::string, fs::path> replacements;  // of the active mod
    std::string group = "Menús", subgroup;
    int session = 0;  // capture session filter (0: none)
    char search[128] = "";
    bool onlyReplaced = false, onlySeen = false;
    std::vector<int> visible;
    int selected = -1;
    bool allPalettes = false;
    std::string toast;
    bool toastError = false;
    std::vector<ImportResult> report;
    bool openReport = false;
    Capture capture;
    bool capturing = false;
    std::vector<std::pair<int, std::vector<fs::path>>> sessions;
    char newModName[64] = "";
    std::string newModError;
    bool openNewMod = false, openZoom = false;
    bool zoomReplacement = false;
    DialogResult dialog;
    Pending pending = Pending::None;
    bool dialogOpen = false;
    // Long batches (export, folder import, zip) run here so the window keeps responding.
    struct Job {
        std::thread thread;
        std::atomic<bool> done{false};
        bool running = false;
        Progress progress;
        std::string label;
        std::function<void()> finish;  // runs on the UI thread once the work is done
    } job;
    Thumbs thumbs;
};

void Toast(App& a, std::string text, bool error = false) {
    a.toast = std::move(text);
    a.toastError = error;
}

// The replacement files of the active mod for this texture (exact palette, "$", captured variants).
std::vector<fs::path> Replacements(const App& a, const CatalogTexture& t) { return ReplacementsOf(t, a.replacements); }
bool Replaced(const App& a, const CatalogTexture& t) { return !Replacements(a, t).empty(); }

void StartJob(App& a, std::string label, std::function<void(Progress&)> work, std::function<void()> finish) {
    a.job.progress.done = 0;
    a.job.progress.total = 0;
    a.job.done = false;
    a.job.running = true;
    a.job.label = std::move(label);
    a.job.finish = std::move(finish);
    a.job.thread = std::thread([&a, work = std::move(work)] {
        work(a.job.progress);
        a.job.done = true;
    });
}

fs::path ModRoot(const App& a) { return a.activeMod >= 0 ? a.mods[a.activeMod].root : fs::path(); }

void RefreshMods(App& a, const std::string& keep = {}) {
    const std::string current = !keep.empty() ? keep : a.activeMod >= 0 ? a.mods[a.activeMod].id : std::string();
    a.mods = ListMods(a.paths.mods, ReadEnabledMods(a.paths.config));
    a.activeMod = -1;
    for (int i = 0; i < static_cast<int>(a.mods.size()); ++i)
        if (a.mods[i].id == current) a.activeMod = i;
    if (a.activeMod < 0)
        for (int i = 0; i < static_cast<int>(a.mods.size()); ++i)
            if (a.mods[i].enabled) { a.activeMod = i; break; }
    if (a.activeMod < 0 && !a.mods.empty()) a.activeMod = 0;
    a.replacements = a.activeMod >= 0 ? FindReplacements(ModRoot(a)) : decltype(a.replacements){};
}

void RebuildCounts(App& a) {
    a.counts.clear();
    for (const auto& t : a.catalog.Textures()) ++a.counts[t.group][t.subgroup];
}

void Refilter(App& a) {
    a.visible.clear();
    std::string needle = a.search;
    for (char& ch : needle) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    const auto& textures = a.catalog.Textures();
    for (int i = 0; i < static_cast<int>(textures.size()); ++i) {
        const auto& t = textures[i];
        if (a.session > 0) {
            if (std::find(t.seenIn.begin(), t.seenIn.end(), a.session) == t.seenIn.end()) continue;
        } else {
            if (a.group != "Todo" && t.group != a.group) continue;
            if (!a.subgroup.empty() && t.subgroup != a.subgroup) continue;
        }
        if (a.onlyReplaced && !Replaced(a, t)) continue;
        if (a.onlySeen && t.seenIn.empty()) continue;
        if (!needle.empty()) {
            std::string hay = t.name + " " + t.group + " " + t.subgroup + " " + GroupLabel(t.group) + " " + SubgroupLabel(t.subgroup);
            for (const auto& l : t.locations) hay += " " + a.catalog.Entries()[l.entry].name;
            for (char& ch : hay) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            if (hay.find(needle) == std::string::npos) continue;
        }
        a.visible.push_back(i);
    }
    if (std::find(a.visible.begin(), a.visible.end(), a.selected) == a.visible.end())
        a.selected = a.visible.empty() ? -1 : a.visible[0];
}

std::vector<std::string> WideArgs([[maybe_unused]] int argc, [[maybe_unused]] char** argv) {
    std::vector<std::string> out;
#if defined(_WIN32)
    int count = 0;
    if (LPWSTR* args = CommandLineToArgvW(GetCommandLineW(), &count)) {
        for (int i = 1; i < count; ++i) out.push_back(Utf8(fs::path(args[i])));
        LocalFree(args);
    }
#else
    for (int i = 1; i < argc; ++i) out.emplace_back(argv[i]);  // already UTF-8
#endif
    return out;
}

Paths ResolvePaths(const std::vector<std::string>& args, bool* gameMissing) {
    const fs::path base = PathFromUtf8(SDL_GetBasePath());
    Paths p{base / "UserData" / "Config.toml", base / "UserData", base / "mods",
#if defined(_WIN32)
            base / "MLBPowerPros2008.exe",
#else
            base / "MLBPowerPros2008",
#endif
            {}};
    for (size_t i = 0; i + 1 < args.size(); ++i) {
        if (args[i] == "--config") p.config = PathFromUtf8(args[++i]);
        else if (args[i] == "--userdata") p.userData = PathFromUtf8(args[++i]);
        else if (args[i] == "--mods") p.mods = PathFromUtf8(args[++i]);
        else if (args[i] == "--game") p.game = PathFromUtf8(args[++i]);
    }
    const auto dvd = fs::exists(p.config) ? ReadSetting(p.config, "paths", "dvd_root") : std::nullopt;
    if (dvd) {
        fs::path root = PathFromUtf8(*dvd);
        if (root.is_relative()) root = p.game.parent_path() / root;
        p.projectBin = root / "files" / "project.bin";
    }
    *gameMissing = !dvd || !fs::exists(p.projectBin);
    return p;
}

const char* GameMissingText() {
    return Tr("No se encontró el juego.\n\nInstálalo con el Setup de MLB Power Pros 2008 y abre el Studio desde el "
              "launcher (HERRAMIENTAS > EDITOR DE TEXTURAS) o desde su acceso directo.",
              "The game was not found.\n\nInstall it with the MLB Power Pros 2008 Setup and open the Studio from the "
              "launcher (TOOLS > TEXTURE EDITOR) or from its shortcut.");
}

// Config.toml [ui] language: "es", "en", or "auto" / missing for the system's language.
void LoadLanguage(const fs::path& config) {
    const auto value = fs::exists(config) ? ReadSetting(config, "ui", "language") : std::nullopt;
    power_pros_ui::g_lang = power_pros_ui::LangFromSetting(value.value_or("auto"));
}

// Applies at once; kept in Config.toml when the game's config exists (otherwise for this session only).
void SetLanguage(App& a, power_pros_ui::Lang lang) {
    power_pros_ui::g_lang = lang;
    if (fs::exists(a.paths.config) &&
        !WriteSetting(a.paths.config, "ui", "language", lang == power_pros_ui::Lang::En ? "\"en\"" : "\"es\""))
        Toast(a, Tr("No se pudo guardar el idioma en Config.toml.", "Could not save the language in Config.toml."), true);
}

void OpenInExplorer(const fs::path& target) {
#if defined(_WIN32)
    ShellExecuteW(nullptr, L"open", target.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
    const std::string path = target.string();
    char* argv[] = {const_cast<char*>("xdg-open"), const_cast<char*>(path.c_str()), nullptr};
    pid_t pid = 0;
    posix_spawnp(&pid, "xdg-open", nullptr, nullptr, argv, environ);
#endif
}

// ---- Drawing ----------------------------------------------------------------------------------------
void DrawBand(App& a, float w) {
    using namespace power_pros_ui;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float h = 64;
    const int start = dl->VtxBuffer.Size;
    dl->AddRectFilled(ImVec2(0, 0), ImVec2(w, h), IM_COL32_WHITE);
    Shade(dl, start, 0, h, ui::kBandTop, ui::kBandBottom);
    dl->AddLine(ImVec2(0, h), ImVec2(w, h), Rgba(255, 255, 255, 140), 3);
    ShadowedText(dl, g_title, 30, ImVec2(18, 6), IM_COL32_WHITE, "POWER PROS WII 2008");
    Text(dl, g_bold, 15, ImVec2(20, 41), ui::kYellow, "T E X T U R E   S T U D I O");
    // Language switch at the far right; hidden while the catalog is still being read on its thread.
    if (a.ready || a.gameMissing) {
        ImGui::SetCursorScreenPos(ImVec2(w - 100, 14));
        Lang lang = g_lang;
        if (ui::LangSwitch(&lang)) SetLanguage(a, lang);
    }
    if (!a.ready) return;

    // Right side: active mod, its state in the game, new mod.
    const float titleEnd = 18 + TextSize(g_title, 30, "POWER PROS WII 2008").x;
    ImGui::SetCursorScreenPos(ImVec2(std::max(w - 740, titleEnd + 24), 14));
    ImGui::SetNextItemWidth(250);
    const std::string current = a.activeMod >= 0 ? a.mods[a.activeMod].id : std::string(Tr("(ningún mod)", "(no mod)"));
    if (ImGui::BeginCombo("##mod", ("Mod: " + current).c_str())) {
        for (int i = 0; i < static_cast<int>(a.mods.size()); ++i) {
            if (ImGui::Selectable(a.mods[i].id.c_str(), i == a.activeMod)) {
                a.activeMod = i;
                a.replacements = FindReplacements(ModRoot(a));
                a.thumbs.Clear();
                Refilter(a);
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (a.activeMod >= 0) {
        const bool on = a.mods[a.activeMod].enabled;
        const ImVec2 p = ImGui::GetCursorScreenPos();
        dl->AddCircleFilled(ImVec2(p.x + 8, p.y + 13), 6, on ? ui::kGreenTop : ui::kGreyTop);
        ImGui::SetCursorScreenPos(ImVec2(p.x + 20, p.y + 2));
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(on ? ui::kGreenTop : ui::kMuted), "%s",
                           on ? Tr("Activado en el juego", "Enabled in the game") : Tr("No activado", "Not enabled"));
        if (!on) {
            ImGui::SameLine();
            ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, 8));
            if (ui::Pill(Tr("Activar", "Enable"), ui::kBlueTop, ui::kBlueBottom, true)) {
                SetModEnabled(a.paths.config, a.mods[a.activeMod].id, true);
                RefreshMods(a);
                Toast(a, Tr("Mod activado: se aplica la próxima vez que abras el juego.", "Mod enabled: it applies the next time you open the game."));
            }
        }
        ImGui::SameLine();
    }
    ImGui::SetCursorScreenPos(ImVec2(w - 250, 8));
    if (ui::Pill(Tr("+ Nuevo mod", "+ New mod"), ui::kGreenTop, ui::kGreenBottom, true)) {
        a.newModName[0] = 0;
        a.newModError.clear();
        a.openNewMod = true;
    }
}

void DrawCategories(App& a, float height) {
    ui::BeginPanel("##cats", Tr("CATEGORÍAS", "CATEGORIES"), ImVec2(250, height));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    auto row = [&](const std::string& label, int count, bool selected, bool sub) {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float w = ImGui::GetContentRegionAvail().x;
        const bool clicked = ImGui::InvisibleButton((label + (sub ? "##s" : "##g")).c_str(), ImVec2(w, 26));
        if (selected) {
            const int start = dl->VtxBuffer.Size;
            dl->AddRectFilled(ImVec2(p.x - 8, p.y), ImVec2(p.x + w - 4, p.y + 26), IM_COL32_WHITE, 13, ImDrawFlags_RoundCornersRight);
            ImGui::ShadeVertsLinearColorGradientKeepAlpha(dl, start, dl->VtxBuffer.Size, ImVec2(p.x, 0), ImVec2(p.x + w, 0),
                                                          ui::kBlueBottom, power_pros_ui::Rgba(28, 112, 214));
        } else if (ImGui::IsItemHovered()) {
            dl->AddRectFilled(ImVec2(p.x - 8, p.y), ImVec2(p.x + w - 4, p.y + 26), ui::kPanelHeader, 13, ImDrawFlags_RoundCornersRight);
        }
        const std::string n = std::to_string(count);
        // Long pack-derived names are cut with "..." so they never run into the count.
        const float room = w - (sub ? 16 : 0) - ImGui::CalcTextSize(n.c_str()).x - 24;
        std::string shown = label;
        if (ImGui::CalcTextSize(shown.c_str()).x > room) {
            while (!shown.empty() && ImGui::CalcTextSize((shown + "...").c_str()).x > room) {
                shown.pop_back();  // whole UTF-8 characters only
                while (!shown.empty() && (static_cast<unsigned char>(shown.back()) & 0xC0) == 0x80) shown.pop_back();
            }
            shown += "...";
        }
        dl->AddText(ImVec2(p.x + (sub ? 16 : 0), p.y + 3), sub ? ui::kMuted : IM_COL32_WHITE, shown.c_str());
        if (shown != label && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", label.c_str());
        dl->AddText(ImVec2(p.x + w - 12 - ImGui::CalcTextSize(n.c_str()).x, p.y + 3), selected ? IM_COL32_WHITE : ui::kMuted, n.c_str());
        return clicked;
    };
    int total = 0;
    for (const auto& [_, subs] : a.counts) for (const auto& [__, n] : subs) total += n;
    if (row(GroupLabel("Todo"), total, a.session == 0 && a.group == "Todo", false)) { a.group = "Todo"; a.subgroup.clear(); a.session = 0; Refilter(a); }
    for (const auto& group : GroupOrder()) {
        const auto it = a.counts.find(group);
        if (it == a.counts.end()) continue;
        int n = 0;
        for (const auto& [_, c] : it->second) n += c;
        const bool open = a.session == 0 && a.group == group;
        if (row(GroupLabel(group), n, open && a.subgroup.empty(), false)) { a.group = group; a.subgroup.clear(); a.session = 0; Refilter(a); }
        if (!open) continue;
        // Sorted by the shown name (in Spanish that is the key's own order).
        std::vector<std::pair<std::string, std::string>> subs;  // shown name, key
        for (const auto& [sub, _] : it->second) subs.emplace_back(SubgroupLabel(sub), sub);
        std::sort(subs.begin(), subs.end());
        for (const auto& [label, sub] : subs)
            if (row(label, it->second.at(sub), a.subgroup == sub, true)) { a.subgroup = sub; Refilter(a); }
    }
    if (!a.sessions.empty()) {
        ImGui::Spacing();
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::kYellow), "%s", Tr("CAPTURAS", "CAPTURES"));
        for (const auto& [n, files] : a.sessions)
            if (row(Tr("Sesión ", "Session ") + std::to_string(n), static_cast<int>(files.size()), a.session == n, true)) { a.session = n; Refilter(a); }
    }
    ui::EndPanel();
}

void DrawToolbar(App& a, float width) {
    ui::BeginPanel("##tools", nullptr, ImVec2(width, 52));
    ImGui::SetCursorPos(ImVec2(12, 12));
    ImGui::SetNextItemWidth(width - 420);
    if (ImGui::InputTextWithHint("##buscar", Tr("Buscar por nombre, pack o hash", "Search by name, pack or hash"), a.search, sizeof a.search)) Refilter(a);
    ImGui::SameLine(0, 18);
    if (ImGui::Checkbox(Tr("Solo reemplazadas###replaced", "Replaced only###replaced"), &a.onlyReplaced)) Refilter(a);
    ImGui::SameLine(0, 14);
    if (ImGui::Checkbox(Tr("Vistas en captura###seen", "Seen in a capture###seen"), &a.onlySeen)) Refilter(a);
    ui::EndPanel();
}

void DrawThumb(App& a, SDL_Renderer* r, ImDrawList* dl, int index, ImVec2 p, float size, bool replacement) {
    const CatalogTexture& t = a.catalog.Textures()[index];
    ui::Checker(dl, p, ImVec2(p.x + size, p.y + size), size > 120 ? 10.0f : 6.0f);
    int w = 0, h = 0;
    SDL_Texture* tex = nullptr;
    if (replacement) {
        const auto files = Replacements(a, t);
        if (files.empty()) return;
        const fs::path file = files.front();
        tex = a.thumbs.Get(r, "r:" + t.name, [file] { return LoadImage(file, nullptr).value_or(Image{}); }, &w, &h);
    } else {
        tex = a.thumbs.Get(r, "o:" + t.name, [&a, index] { return a.catalog.Original(a.catalog.Textures()[index]); }, &w, &h);
    }
    if (!tex) return;
    const auto [i0, i1] = ui::Fit(ImVec2(p.x + 3, p.y + 3), ImVec2(p.x + size - 3, p.y + size - 3), static_cast<float>(w),
                                  static_cast<float>(h));
    dl->AddImage(reinterpret_cast<ImTextureID>(tex), i0, i1);
}

void DrawGrid(App& a, SDL_Renderer* r, ImVec2 size) {
    int replaced = 0;
    for (int i : a.visible) replaced += Replaced(a, a.catalog.Textures()[i]) ? 1 : 0;
    std::string title = a.session > 0 ? Tr("Capturas > Sesión ", "Captures > Session ") + std::to_string(a.session)
                                      : GroupLabel(a.group) + (a.subgroup.empty() ? "" : " > " + SubgroupLabel(a.subgroup));
    title += " · " + std::to_string(a.visible.size()) + Tr(" texturas · ", " textures · ") + std::to_string(replaced) +
             Tr(" reemplazadas", " replaced");
    ui::BeginPanel("##grid", title.c_str(), size);
    ImGui::BeginChild("##cells", ImVec2(0, 0));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float cell = 112, thumb = 100;
    const int columns = std::max(1, static_cast<int>(ImGui::GetContentRegionAvail().x / cell));
    const int rows = (static_cast<int>(a.visible.size()) + columns - 1) / columns;
    ImGuiListClipper clipper;
    clipper.Begin(rows, cell);
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            for (int col = 0; col < columns; ++col) {
                const size_t k = static_cast<size_t>(row) * columns + col;
                if (k >= a.visible.size()) break;
                const int index = a.visible[k];
                const CatalogTexture& t = a.catalog.Textures()[index];
                ImGui::SetCursorPos(ImVec2(8 + col * cell, row * cell + 6));
                const ImVec2 p = ImGui::GetCursorScreenPos();
                ImGui::PushID(index);
                if (ImGui::InvisibleButton("##t", ImVec2(thumb, thumb))) a.selected = index;
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    a.selected = index;
                    a.pending = Pending::ExportOne;
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\n%s", SubgroupLabel(t.subgroup).c_str(), t.name.c_str());
                ImGui::PopID();
                const bool hasReplacement = Replaced(a, t);
                DrawThumb(a, r, dl, index, p, thumb, hasReplacement);
                const bool sel = index == a.selected;
                dl->AddRect(p, ImVec2(p.x + thumb, p.y + thumb), sel ? ui::kYellow : ui::kPanelBorder, 8, 0, sel ? 3.0f : 1.5f);
                if (hasReplacement) ui::Badge(dl, ImVec2(p.x + thumb - 11, p.y + 11), 9, false);
                if (!t.seenIn.empty()) ui::Badge(dl, ImVec2(p.x + 11, p.y + 11), 9, true);
                char dims[32];
                std::snprintf(dims, sizeof dims, "%ux%u", t.key.width, t.key.height);
                power_pros_ui::ShadowedText(dl, ImGui::GetFont(), 15, ImVec2(p.x + 5, p.y + thumb - 19), IM_COL32_WHITE, dims);
            }
        }
    }
    ImGui::EndChild();
    ui::EndPanel();
}

void DrawDetail(App& a, SDL_Renderer* r, ImVec2 size) {
    ui::BeginPanel("##detail", nullptr, size);
    if (a.selected < 0) {
        ImGui::SetCursorPos(ImVec2(16, 16));
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::kMuted), "%s", Tr("No hay texturas en esta vista.", "No textures in this view."));
        ui::EndPanel();
        return;
    }
    const CatalogTexture& t = a.catalog.Textures()[a.selected];
    const bool replaced = Replaced(a, t);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float box = 160;
    const ImVec2 o = ImGui::GetCursorScreenPos();
    const ImVec2 left(o.x + 8, o.y + 8), right(o.x + 8 + box + 46, o.y + 8);
    ImGui::SetCursorScreenPos(left);
    if (ImGui::InvisibleButton("##orig", ImVec2(box, box))) { a.zoomReplacement = false; a.openZoom = true; a.thumbs.Forget("zoom"); }
    DrawThumb(a, r, dl, a.selected, left, box, false);
    dl->AddRect(left, ImVec2(left.x + box, left.y + box), IM_COL32_WHITE, 8, 0, 2);
    dl->AddTriangleFilled(ImVec2(left.x + box + 12, left.y + box / 2 - 12), ImVec2(left.x + box + 34, left.y + box / 2),
                          ImVec2(left.x + box + 12, left.y + box / 2 + 12), ui::kYellow);
    ImGui::SetCursorScreenPos(right);
    if (ImGui::InvisibleButton("##repl", ImVec2(box, box)) && replaced) { a.zoomReplacement = true; a.openZoom = true; a.thumbs.Forget("zoom"); }
    DrawThumb(a, r, dl, a.selected, right, box, true);
    if (!replaced) {
        const char* none = Tr("Sin reemplazo", "No replacement");
        dl->AddText(ImVec2(right.x + (box - ImGui::CalcTextSize(none).x) / 2, right.y + box / 2 - 10), ui::kMuted, none);
    }
    dl->AddRect(right, ImVec2(right.x + box, right.y + box), IM_COL32_WHITE, 8, 0, 2);
    char caption[64];
    std::snprintf(caption, sizeof caption, "Original · %ux%u", t.key.width, t.key.height);
    dl->AddText(ImVec2(left.x, left.y + box + 6), ui::kMuted, caption);
    std::string replacedCaption = Tr("Reemplazo", "Replacement");
    if (replaced) {
        int w = 0, h = 0;
        if (a.thumbs.Get(r, "r:" + t.name, [] { return Image{}; }, &w, &h) && w > 0) {
            std::snprintf(caption, sizeof caption, Tr("Reemplazo · %dx%d (×%g)", "Replacement · %dx%d (×%g)"), w, h, static_cast<double>(w) / t.key.width);
            replacedCaption = caption;
        }
    }
    dl->AddText(ImVec2(right.x, right.y + box + 6), ui::kMuted, replacedCaption.c_str());

    // Facts and actions.
    const float x = right.x + box + 30;
    ImGui::SetCursorScreenPos(ImVec2(x, o.y + 6));
    ImGui::BeginGroup();
    auto fact = [](const char* label, const std::string& value) {
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::kYellow), "%s", label);
        ImGui::SameLine(110);
        ImGui::TextWrapped("%s", value.c_str());
    };
    std::string packs;
    std::set<std::string> seenPacks;
    for (const auto& l : t.locations)
        if (seenPacks.insert(a.catalog.Entries()[l.entry].name).second)
            packs += (packs.empty() ? "" : seenPacks.size() == 2 ? Tr(" · también en ", " · also in ") : ", ") + a.catalog.Entries()[l.entry].name;
    if (!t.captureFile.empty()) packs = Tr("No está en ningún pack (visto en el juego)", "Not in any pack (seen in the game)");
    fact("Pack", packs);
    std::string format = FormatName(t.key.format);
    if (t.key.hasPalette)
        format += t.key.paletteWildcard ? Tr(" · paleta en otro pack (se ve en gris)", " · palette in another pack (shown in grey)")
                                        : Tr(" · paleta de ", " · ") + std::to_string(t.key.format == 8 ? 16 : 256) +
                                              Tr(" colores", "-color palette");
    if (!t.paletteVariants.empty())
        format += " · " + std::to_string(t.paletteVariants.size() + 1) + Tr(" paletas vistas en el juego", " palettes seen in the game");
    fact(Tr("Formato", "Format"), format);
    fact(Tr("Archivo", "File"), t.name + ".png");
    fact(Tr("Estado", "Status"), replaced ? Tr("Reemplazada en «", "Replaced in «") + a.mods[a.activeMod].id + "»" : std::string("Original"));
    ImGui::Spacing();
    const bool hasMod = a.activeMod >= 0;
    if (ui::Pill(Tr("Exportar PNG", "Export PNG"), ui::kBlueTop, ui::kBlueBottom, true, IsDecodable(t.key.format) || !t.captureFile.empty()))
        a.pending = Pending::ExportOne;
    ImGui::SameLine();
    if (ui::Pill(Tr("Importar PNG...", "Import PNG..."), ui::kGreenTop, ui::kGreenBottom, true)) {
        if (hasMod) a.pending = Pending::ImportOne;
        else Toast(a, Tr("Crea o elige un mod arriba antes de importar.", "Create or choose a mod above before importing."), true);
    }
    ImGui::SameLine();
    if (ui::Pill(Tr("Quitar reemplazo", "Remove replacement"), ui::kRedTop, ui::kRedBottom, true, replaced)) {
        const size_t removed = RemoveReplacements(t, a.replacements);
        a.thumbs.Forget("r:" + t.name);
        a.replacements = FindReplacements(ModRoot(a));
        Toast(a, removed > 1 ? Tr("Reemplazo quitado (", "Replacement removed (") + std::to_string(removed) + Tr(" archivos).", " files).")
                             : std::string(Tr("Reemplazo quitado.", "Replacement removed.")));
    }
    ImGui::SameLine();
    if (ui::Pill(Tr("Abrir carpeta", "Open folder"), ui::kGreyTop, ui::kGreyBottom, true, hasMod))
        OpenInExplorer(replaced ? Replacements(a, t).front().parent_path() : ModRoot(a));
    if (t.key.hasPalette) {
        ImGui::Checkbox(Tr("Aplicar a todas las paletas (el reemplazo se usa con cualquier color de paleta)###allpalettes",
                           "Apply to all palettes (the replacement is used with any palette colors)###allpalettes"),
                        &a.allPalettes);
    }
    if (ImGui::SmallButton(Tr("Copiar nombre###copyname", "Copy name###copyname"))) ImGui::SetClipboardText((t.name + ".png").c_str());
    ImGui::EndGroup();
    ui::EndPanel();
}

void DrawFooter(App& a, float w, float y) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(0, y), ImVec2(w, y + 62), ui::kFooter);
    dl->AddLine(ImVec2(0, y), ImVec2(w, y), ui::kBlueBottom, 3);
    ImGui::SetCursorScreenPos(ImVec2(10, y + 8));
    const bool hasMod = a.activeMod >= 0;
    if (ui::Pill(Tr("Exportar esta vista", "Export this view"), ui::kOrangeTop, ui::kOrangeBottom)) a.pending = Pending::ExportCategory;
    ImGui::SameLine();
    if (ui::Pill(Tr("Importar carpeta...", "Import folder..."), ui::kGreenTop, ui::kGreenBottom)) {
        if (hasMod) a.pending = Pending::ImportFolder;
        else Toast(a, Tr("Crea o elige un mod arriba antes de importar.", "Create or choose a mod above before importing."), true);
    }
    ImGui::SameLine();
    if (ui::Pill(Tr("Capturar en juego", "Capture in game"), ui::kBlueTop, ui::kBlueBottom)) {
        std::string error;
        if (!fs::exists(a.paths.game)) {
            Toast(a, Tr("No se encuentra el ejecutable del juego.", "The game's executable was not found."), true);
        } else if (a.capture.Start(a.paths.game, a.paths.userData / "Cache" / "texture_dumps", &error)) {
            a.capturing = true;
        } else {
            Toast(a, error, true);
        }
    }
    ImGui::SameLine();
    if (ui::Pill(Tr("Empaquetar .zip", "Pack as .zip"), ui::kGreyTop, ui::kGreyBottom, false, hasMod)) a.pending = Pending::Zip;
    const std::string status = !a.toast.empty() ? a.toast
                                                : Tr("Índice: ", "Index: ") + std::to_string(a.catalog.Textures().size()) +
                                                      Tr(" texturas · project.bin de tu juego · listo",
                                                         " textures · your game's project.bin · ready");
    const ImVec2 ts = ImGui::CalcTextSize(status.c_str());
    dl->AddText(ImVec2(std::max(w - ts.x - 16, ImGui::GetCursorScreenPos().x + 20), y + 22),
                a.toastError ? ui::kRedTop : ui::kMuted, status.c_str());
}

void StartDialog(App& a, SDL_Window* window) {
    if (a.selected < 0 && (a.pending == Pending::ExportOne || a.pending == Pending::ImportOne)) {
        a.pending = Pending::None;
        return;
    }
    a.dialogOpen = true;
    // The filter lists must stay valid until the dialog answers: one static list per language.
    static const SDL_DialogFileFilter pngs[2][1] = {{{"Imágenes PNG o DDS", "png;dds"}}, {{"PNG or DDS images", "png;dds"}}};
    static const SDL_DialogFileFilter zips[2][1] = {{{"Archivo .zip", "zip"}}, {{".zip file", "zip"}}};
    const SDL_DialogFileFilter* png = pngs[power_pros_ui::g_lang == power_pros_ui::Lang::En];
    const SDL_DialogFileFilter* zip = zips[power_pros_ui::g_lang == power_pros_ui::Lang::En];
    switch (a.pending) {
    case Pending::ExportOne: {
        const std::string name = a.catalog.Textures()[a.selected].name + ".png";
        SDL_ShowSaveFileDialog(OnDialog, &a.dialog, window, png, 1, name.c_str());
        break;
    }
    case Pending::ImportOne: SDL_ShowOpenFileDialog(OnDialog, &a.dialog, window, png, 1, nullptr, false); break;
    case Pending::ExportCategory:
    case Pending::ImportFolder: SDL_ShowOpenFolderDialog(OnDialog, &a.dialog, window, nullptr, false); break;
    case Pending::Zip: {
        const std::string name = a.mods[a.activeMod].id + ".zip";
        SDL_ShowSaveFileDialog(OnDialog, &a.dialog, window, zip, 1, name.c_str());
        break;
    }
    case Pending::None: break;
    }
}

void FinishDialog(App& a, Pending what, const std::string& chosen) {
    if (chosen.empty()) return;
    const fs::path path = PathFromUtf8(chosen);
    std::string error;
    switch (what) {
    case Pending::ExportOne: {
        const Image img = a.catalog.Original(a.catalog.Textures()[a.selected]);
        if (img.Empty()) Toast(a, Tr("Este formato no se puede exportar.", "This format can't be exported."), true);
        else if (SavePng(path.extension().empty() ? fs::path(path).replace_extension(".png") : path, img, &error))
            Toast(a, Tr("Exportada: ", "Exported: ") + Utf8(path.filename()));
        else Toast(a, error, true);
        break;
    }
    case Pending::ImportOne: {
        const auto r = ImportOne(a.catalog, a.selected, path, ModRoot(a), a.allPalettes);
        a.thumbs.Forget("r:" + a.catalog.Textures()[a.selected].name);
        a.thumbs.Forget("zoom");
        a.replacements = FindReplacements(ModRoot(a));
        Toast(a, r.ok ? r.message : r.file + ": " + r.message, !r.ok);
        break;
    }
    case Pending::ExportCategory: {
        struct Result { size_t n = 0, grey = 0; std::vector<std::string> problems; };
        auto result = std::make_shared<Result>();
        StartJob(
            a, Tr("Exportando texturas...", "Exporting textures..."),
            [&a, path, result, textures = a.visible](Progress& p) {
                result->n = Export(a.catalog, textures, path, &result->problems, &p, &result->grey);
            },
            [&a, path, result] {
                std::string text = std::to_string(result->n) + Tr(" texturas exportadas en ", " textures exported to ") + Utf8(path);
                if (result->grey)
                    text += " · " + std::to_string(result->grey) +
                            Tr(" en gris (su paleta está en otro pack)", " in grey (their palette is in another pack)");
                if (!result->problems.empty())
                    text += " · " + std::to_string(result->problems.size()) + Tr(" no se pudieron exportar", " could not be exported");
                Toast(a, text, !result->problems.empty() && result->n == 0);
            });
        break;
    }
    case Pending::ImportFolder: {
        auto result = std::make_shared<std::vector<ImportResult>>();
        StartJob(
            a, Tr("Importando texturas...", "Importing textures..."),
            [&a, path, result, mod = ModRoot(a)](Progress& p) { *result = ImportFolder(a.catalog, path, mod, &p); },
            [&a, result] {
                a.report = std::move(*result);
                a.thumbs.Clear();
                a.replacements = FindReplacements(ModRoot(a));
                a.openReport = true;
                Refilter(a);
            });
        break;
    }
    case Pending::Zip: {
        auto result = std::make_shared<std::pair<bool, std::string>>();
        StartJob(
            a, Tr("Empaquetando el mod...", "Packing the mod..."),
            [path, result, mod = ModRoot(a)](Progress&) { result->first = WriteZip(mod, path, &result->second); },
            [&a, path, result] {
                if (result->first) Toast(a, Tr("Mod empaquetado: ", "Mod packed: ") + Utf8(path));
                else Toast(a, result->second, true);
            });
        break;
    }
    case Pending::None: break;
    }
    Refilter(a);
}

void DrawPopups(App& a, SDL_Renderer* r) {
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    if (a.job.running && a.job.done) {
        a.job.thread.join();
        a.job.running = false;
        if (a.job.finish) a.job.finish();
    }
    if (a.job.running) ImGui::OpenPopup("###working");
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(520, 0));
    if (ImGui::BeginPopupModal("###working", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove)) {
        const size_t done = a.job.progress.done, total = a.job.progress.total;
        ImGui::TextUnformatted(a.job.label.c_str());
        const std::string count = total ? std::to_string(done) + " / " + std::to_string(total) : std::string();
        ImGui::ProgressBar(total ? static_cast<float>(done) / static_cast<float>(total) : -static_cast<float>(ImGui::GetTime()),
                           ImVec2(-1, 0), count.c_str());
        if (!a.job.running) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (a.openNewMod) { ImGui::OpenPopup("###newmod"); a.openNewMod = false; }
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(Tr("Nuevo mod###newmod", "New mod###newmod"), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(Tr("Nombre del mod (será la carpeta en mods\\):", "Mod name (it becomes its folder in mods\\):"));
        ImGui::SetNextItemWidth(380);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        const bool enter = ImGui::InputText("##name", a.newModName, sizeof a.newModName, ImGuiInputTextFlags_EnterReturnsTrue);
        if (!a.newModError.empty()) ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::kRedTop), "%s", a.newModError.c_str());
        if (ui::Pill(Tr("Crear", "Create"), ui::kGreenTop, ui::kGreenBottom, true) || enter) {
            std::string error;
            if (CreateMod(a.paths.mods, a.newModName, &error)) {
                RefreshMods(a, *ValidModName(a.newModName, nullptr));
                a.thumbs.Clear();
                Refilter(a);
                Toast(a, Tr("Mod creado. Actívalo para verlo en el juego.", "Mod created. Enable it to see it in the game."));
                ImGui::CloseCurrentPopup();
            } else {
                a.newModError = error;
            }
        }
        ImGui::SameLine();
        if (ui::Pill(Tr("Cancelar", "Cancel"), ui::kGreyTop, ui::kGreyBottom, true)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (a.openReport) { ImGui::OpenPopup("###report"); a.openReport = false; }
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(900, 520), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal(Tr("Informe de importación###report", "Import report###report"))) {
        int ok = 0;
        for (const auto& x : a.report) ok += x.ok;
        ImGui::Text(Tr("%d importadas, %d con problemas", "%d imported, %d with problems"), ok, static_cast<int>(a.report.size()) - ok);
        if (ImGui::BeginTable("##report", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Borders,
                              ImVec2(0, 400))) {
            ImGui::TableSetupColumn(Tr("Archivo", "File"), ImGuiTableColumnFlags_WidthStretch, 2);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 40);
            ImGui::TableSetupColumn(Tr("Resultado", "Result"), ImGuiTableColumnFlags_WidthStretch, 3);
            ImGui::TableHeadersRow();
            for (const auto& x : a.report) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(x.file.c_str());
                ImGui::TableNextColumn();
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(x.ok ? ui::kGreenTop : ui::kRedTop), x.ok ? "OK" : "NO");
                ImGui::TableNextColumn();
                ImGui::TextWrapped("%s", x.message.c_str());
            }
            ImGui::EndTable();
        }
        if (ui::Pill(Tr("Cerrar", "Close"), ui::kBlueTop, ui::kBlueBottom, true)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (a.openZoom) { ImGui::OpenPopup("##zoom"); a.openZoom = false; }
    const ImVec2 vs = ImGui::GetMainViewport()->Size;
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(vs.x * 0.8f, vs.y * 0.8f));
    if (ImGui::BeginPopupModal("##zoom", nullptr, ImGuiWindowFlags_NoDecoration)) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetWindowPos(), s = ImGui::GetWindowSize();
        ui::Checker(dl, p, ImVec2(p.x + s.x, p.y + s.y), 16);
        if (a.selected >= 0) {
            const CatalogTexture& t = a.catalog.Textures()[a.selected];
            int w = 0, h = 0;
            a.thumbs.budget = std::max(a.thumbs.budget, 1);
            SDL_Texture* tex = a.thumbs.Get(
                r, "zoom",
                [&] {
                    if (!a.zoomReplacement) return a.catalog.Original(t);
                    const auto files = Replacements(a, t);
                    return files.empty() ? Image{} : LoadImage(files.front(), nullptr).value_or(Image{});
                },
                &w, &h, 0);
            if (tex) {
                SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);
                const auto [i0, i1] = ui::Fit(ImVec2(p.x + 16, p.y + 16), ImVec2(p.x + s.x - 16, p.y + s.y - 16), static_cast<float>(w),
                                              static_cast<float>(h));
                dl->AddImage(reinterpret_cast<ImTextureID>(tex), i0, i1);
            }
        }
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (a.capturing) ImGui::OpenPopup("###capturing");
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(Tr("Capturando###capturing", "Capturing###capturing"), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(Tr("Juega hasta la pantalla que quieras y cierra el juego.", "Play up to the screen you want, then close the game."));
        ImGui::TextUnformatted(Tr("Las texturas que aparezcan quedarán marcadas en una sesión nueva.",
                                   "The textures that appear will be marked in a new session."));
        if (!a.capture.Running()) {
            a.capturing = false;
            auto [n, files] = a.capture.Collect(a.paths.userData / "Cache" / "texture_studio" / "captures");
            if (n == 0) {
                Toast(a, Tr("El juego no mostró texturas nuevas en esta captura.", "The game showed no new textures in this capture."));
            } else {
                a.catalog.AddCapture(n, files);
                a.sessions.emplace_back(n, files);
                RebuildCounts(a);
                a.session = n;
                Refilter(a);
                Toast(a, Tr("Sesión ", "Session ") + std::to_string(n) + ": " + std::to_string(files.size()) + Tr(" texturas vistas.", " textures seen."));
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void ApplyStyle() {
    ImGuiStyle& s = ImGui::GetStyle();
    s.FrameRounding = 8;
    s.PopupRounding = 10;
    s.WindowRounding = 12;
    s.GrabRounding = 8;
    s.FramePadding = ImVec2(10, 6);
    s.ItemSpacing = ImVec2(8, 6);
    ImVec4* c = s.Colors;
    auto f = [](ImU32 v) { return ImGui::ColorConvertU32ToFloat4(v); };
    c[ImGuiCol_WindowBg] = f(ui::kWindow);
    c[ImGuiCol_PopupBg] = f(ui::kPanel);
    c[ImGuiCol_Border] = f(ui::kPanelBorder);
    c[ImGuiCol_FrameBg] = f(power_pros_ui::Rgba(12, 20, 34));
    c[ImGuiCol_FrameBgHovered] = f(ui::kPanelHeader);
    c[ImGuiCol_FrameBgActive] = f(ui::kPanelHeader);
    c[ImGuiCol_CheckMark] = f(ui::kYellow);
    c[ImGuiCol_Header] = f(ui::kBlueBottom);
    c[ImGuiCol_HeaderHovered] = f(ui::kPanelHeader);
    c[ImGuiCol_Text] = f(IM_COL32_WHITE);
    c[ImGuiCol_TitleBgActive] = f(ui::kBandTop);
    c[ImGuiCol_TitleBg] = f(ui::kBandBottom);
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.55f);
    c[ImGuiCol_TableHeaderBg] = f(ui::kPanelHeader);
    c[ImGuiCol_TableRowBgAlt] = f(power_pros_ui::Rgba(28, 40, 64));
    c[ImGuiCol_ScrollbarBg] = f(ui::kPanel);
}

} // namespace

int main(int argc, char** argv) {
    const std::vector<std::string> args = WideArgs(argc, argv);
    for (const auto& arg : args)
        if (arg == "--selftest") return RunSelftest();
    if (const int dev = DevMode(args); dev >= 0) return dev;

#if defined(_WIN32)
    SetProcessDPIAware();
#endif
    if (!SDL_Init(SDL_INIT_VIDEO)) return 1;
    SDL_Window* window = SDL_CreateWindow("Power Pros Wii 2008 Texture Studio", 1400, 860,
                                          SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    SDL_SetWindowMinimumSize(window, 1100, 700);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    SDL_SetRenderVSync(renderer, 1);
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);
    power_pros_ui::LoadFonts();
    ImGui::GetIO().FontDefault = power_pros_ui::g_menu;
    ApplyStyle();

    auto app = std::make_unique<App>();
    App& a = *app;
    a.paths = ResolvePaths(args, &a.gameMissing);
    LoadLanguage(a.paths.config);
    for (size_t i = 0; i + 1 < args.size(); ++i)  // --lang es|en: this session only (screenshot tests)
        if (args[i] == "--lang") power_pros_ui::g_lang = power_pros_ui::LangFromSetting(args[i + 1]);
    if (!a.gameMissing)
        a.builder = std::thread([&a] {
            try {
                a.catalog.Build(a.paths.projectBin, &a.progress, &a.buildError);
            } catch (const std::exception& e) {
                a.buildError = std::string(Tr("No se pudo leer project.bin: ", "Could not read project.bin: ")) + e.what();
            }
            a.built = true;
        });

    bool quit = false;
    while (!quit) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT) quit = true;
        }
        if (a.built && !a.ready) {
            a.ready = true;
            if (!a.buildError.empty()) a.fatal = a.buildError;
            a.sessions = LoadSessions(a.paths.userData / "Cache" / "texture_studio" / "captures");
            for (const auto& [n, files] : a.sessions) a.catalog.AddCapture(n, files);
            RebuildCounts(a);
            RefreshMods(a);
            // Opening view (shortcuts and screenshot tests): --view <group>[/<subgroup>] --search <text> --select <tex1 name> --mod <id>
            for (size_t i = 0; i + 1 < args.size(); ++i) {
                if (args[i] == "--view") {
                    const size_t slash = args[i + 1].find('/');
                    a.group = args[i + 1].substr(0, slash);
                    a.subgroup = slash == std::string::npos ? "" : args[i + 1].substr(slash + 1);
                } else if (args[i] == "--search") {
                    std::snprintf(a.search, sizeof a.search, "%s", args[i + 1].c_str());
                }
            }
            Refilter(a);
            for (size_t i = 0; i + 1 < args.size(); ++i) {
                if (args[i] == "--select") a.selected = a.catalog.Find(args[i + 1]);
                if (args[i] == "--mod") RefreshMods(a, args[i + 1]);
            }
        }
        bool answered = false;
        std::string chosen;
        {
            std::lock_guard lock(a.dialog.mutex);
            answered = a.dialog.ready;
            a.dialog.ready = false;
            chosen = a.dialog.path;
        }
        if (answered) {
            const Pending what = a.pending;
            a.pending = Pending::None;
            a.dialogOpen = false;
            FinishDialog(a, what, chosen);
        }

        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        a.thumbs.budget = 24;
        const ImVec2 vs = ImGui::GetMainViewport()->Size;
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(vs);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::Begin("##root", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                           ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PopStyleVar();
        DrawBand(a, vs.x);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (a.gameMissing || !a.fatal.empty()) {
            ImGui::SetCursorPos(ImVec2(vs.x * 0.2f, vs.y * 0.35f));
            ImGui::PushTextWrapPos(vs.x * 0.8f);
            ImGui::TextUnformatted(a.gameMissing ? GameMissingText() : a.fatal.c_str());
            ImGui::PopTextWrapPos();
            ImGui::SetCursorPos(ImVec2(vs.x * 0.2f, vs.y * 0.35f + 120));
            if (ui::Pill(Tr("Salir", "Quit"), ui::kGreyTop, ui::kGreyBottom)) quit = true;
        } else if (!a.ready) {
            const float w = vs.x * 0.5f, x = (vs.x - w) / 2, y = vs.y * 0.48f;
            char label[96];
            std::snprintf(label, sizeof label, Tr("Leyendo las texturas del juego... %d %%", "Reading the game's textures... %d %%"), static_cast<int>(a.progress * 100));
            dl->AddText(ImVec2(x, y - 34), IM_COL32_WHITE, label);
            dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + 18), ui::kPanel, 9);
            dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w * a.progress, y + 18), ui::kBlueTop, 9);
        } else {
            const float top = 76, footer = 62, gap = 10, left = 262;
            const float bodyH = vs.y - top - footer - gap;
            ImGui::SetCursorPos(ImVec2(10, top));
            DrawCategories(a, bodyH);
            const float mainW = vs.x - left - 10;
            ImGui::SetCursorPos(ImVec2(left, top));
            DrawToolbar(a, mainW);
            const float detailH = 236;
            ImGui::SetCursorPos(ImVec2(left, top + 52 + gap));
            DrawGrid(a, renderer, ImVec2(mainW, bodyH - 52 - detailH - 2 * gap));
            ImGui::SetCursorPos(ImVec2(left, vs.y - footer - gap - detailH));
            DrawDetail(a, renderer, ImVec2(mainW, detailH));
            DrawFooter(a, vs.x, vs.y - footer);
            DrawPopups(a, renderer);
            if (a.pending != Pending::None && !a.dialogOpen) StartDialog(a, window);
        }
        ImGui::End();

        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 16, 24, 38, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
    }

    if (a.builder.joinable()) a.builder.join();
    if (a.job.thread.joinable()) a.job.thread.join();
    a.thumbs.Clear();
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
